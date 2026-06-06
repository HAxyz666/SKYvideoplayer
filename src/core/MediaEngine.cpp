#include "MediaEngine.h"
#include "DemuxThread.h"
#include "VideoDecodeThread.h"
#include "AudioDecodeThread.h"
#include "PacketQueue.h"
#include "FrameQueue.h"
#include "AVSyncController.h"
#include "AudioOutput.h"

#include <QDebug>
#include <QFileInfo>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

MediaEngine::MediaEngine(const QString &filename, QObject *parent)
    : QObject(parent)
    , m_filename(filename)
    , m_fmtCtx(nullptr)
    , m_videoStreamIndex(-1)
    , m_audioStreamIndex(-1)
    , m_videoCodecCtx(nullptr)
    , m_audioCodecCtx(nullptr)
    , m_demuxThread(nullptr)
    , m_videoThread(nullptr)
    , m_audioThread(nullptr)
    , m_videoPacketQueue(nullptr)
    , m_audioPacketQueue(nullptr)
    , m_videoFrameQueue(nullptr)
    , m_syncController(new AVSyncController(this))
    , m_audioOutput(new AudioOutput(this))
    , m_frameQueueDrainTimer(new QTimer(this))
{
    connect(m_frameQueueDrainTimer, &QTimer::timeout, this, [this]() {
        if (!m_videoFrameQueue) return;
        while (m_videoFrameQueue->size() > 0) {
            AVFrame *frame = m_videoFrameQueue->tryPop(0);
            if (frame) av_frame_free(&frame);
            else break;
        }
    });
}

MediaEngine::~MediaEngine()
{
    stop();
}

bool MediaEngine::initFFmpeg(const QString &filename)
{
    if (avformat_open_input(&m_fmtCtx, filename.toUtf8().constData(), nullptr, nullptr) != 0) {
        qCritical() << "Could not open file:" << filename;
        return false;
    }

    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        qCritical() << "Could not find stream info";
        return false;
    }

    for (unsigned int i = 0; i < m_fmtCtx->nb_streams; i++) {
        if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && m_videoStreamIndex == -1) {
            m_videoStreamIndex = i;
        } else if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && m_audioStreamIndex == -1) {
            m_audioStreamIndex = i;
        }
    }

    if (m_videoStreamIndex == -1) {
        qCritical() << "No video stream found";
        return false;
    }

    AVCodecParameters *videoPar = m_fmtCtx->streams[m_videoStreamIndex]->codecpar;
    const AVCodec *videoCodec = avcodec_find_decoder(videoPar->codec_id);
    if (!videoCodec) {
        qCritical() << "Video codec not found";
        return false;
    }

    m_videoCodecCtx = avcodec_alloc_context3(videoCodec);
    avcodec_parameters_to_context(m_videoCodecCtx, videoPar);

    if (avcodec_open2(m_videoCodecCtx, videoCodec, nullptr) < 0) {
        qCritical() << "Could not open video codec";
        return false;
    }

    if (m_audioStreamIndex != -1) {
        AVCodecParameters *audioPar = m_fmtCtx->streams[m_audioStreamIndex]->codecpar;
        const AVCodec *audioCodec = avcodec_find_decoder(audioPar->codec_id);

        if (audioCodec) {
            m_audioCodecCtx = avcodec_alloc_context3(audioCodec);
            avcodec_parameters_to_context(m_audioCodecCtx, audioPar);

            if (avcodec_open2(m_audioCodecCtx, audioCodec, nullptr) < 0) {
                qWarning() << "Could not open audio codec, playing without audio";
                avcodec_free_context(&m_audioCodecCtx);
                m_audioCodecCtx = nullptr;
            }
        }
    }

    av_dump_format(m_fmtCtx, 0, filename.toUtf8().constData(), 0);

    return true;
}

void MediaEngine::start()
{
    if (!initFFmpeg(m_filename))
        return;

    bool audioInited = m_audioOutput->init(m_audioCodecCtx);

    m_syncController->reset();

    startThreads();
}

void MediaEngine::stop()
{
    stopThreads();
    cleanup();
}

void MediaEngine::startThreads()
{
    m_videoPacketQueue = new PacketQueue(64);
    m_audioPacketQueue = new PacketQueue(64);
    m_videoFrameQueue = new FrameQueue(24);

    m_demuxThread = new DemuxThread(this);
    m_demuxThread->setFormatContext(m_fmtCtx);
    m_demuxThread->setStreamIndices(m_videoStreamIndex, m_audioStreamIndex);
    m_demuxThread->setPacketQueues(m_videoPacketQueue, m_audioPacketQueue);
    connect(m_demuxThread, &DemuxThread::eofReached, this, [this]() {
        emit playbackFinished();
    });
    connect(m_demuxThread, &DemuxThread::errorOccurred, this, [this](const QString &msg) {
        qWarning() << "Demux error:" << msg;
    });

    m_videoThread = new VideoDecodeThread(this);
    m_videoThread->setCodecContext(m_videoCodecCtx);
    m_videoThread->setPacketQueue(m_videoPacketQueue);
    m_videoThread->setFrameQueue(m_videoFrameQueue);
    m_videoThread->setTimeBase(m_fmtCtx->streams[m_videoStreamIndex]->time_base);
    connect(m_videoThread, &VideoDecodeThread::frameReady, this, &MediaEngine::frameReady);

    if (m_audioCodecCtx) {
        m_audioThread = new AudioDecodeThread(this);
        m_audioThread->setCodecContext(m_audioCodecCtx);
        m_audioThread->setPacketQueue(m_audioPacketQueue);
        m_audioThread->setAudioOutput(m_audioOutput);
    }

    m_frameQueueDrainTimer->start(100);

    m_demuxThread->start();
    m_videoThread->start();
    if (m_audioThread)
        m_audioThread->start();
}

void MediaEngine::stopThreads()
{
    m_frameQueueDrainTimer->stop();

    if (m_demuxThread) {
        m_demuxThread->stopRead();
        m_demuxThread->wait();
        delete m_demuxThread;
        m_demuxThread = nullptr;
    }
    if (m_videoThread) {
        m_videoThread->stopDecode();
        m_videoThread->wait();
        delete m_videoThread;
        m_videoThread = nullptr;
    }
    if (m_audioThread) {
        m_audioThread->stopDecode();
        m_audioThread->wait();
        delete m_audioThread;
        m_audioThread = nullptr;
    }

    delete m_videoPacketQueue; m_videoPacketQueue = nullptr;
    delete m_audioPacketQueue; m_audioPacketQueue = nullptr;
    delete m_videoFrameQueue; m_videoFrameQueue = nullptr;
}

void MediaEngine::cleanup()
{
    m_audioOutput->stop();

    if (m_videoCodecCtx) {
        avcodec_free_context(&m_videoCodecCtx);
        m_videoCodecCtx = nullptr;
    }
    if (m_audioCodecCtx) {
        avcodec_free_context(&m_audioCodecCtx);
        m_audioCodecCtx = nullptr;
    }
    if (m_fmtCtx) {
        avformat_close_input(&m_fmtCtx);
        m_fmtCtx = nullptr;
    }
}
