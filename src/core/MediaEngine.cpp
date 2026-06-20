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
#include <libavutil/time.h>
}

#ifdef ENABLE_HWACCEL
bool MediaEngine::createHwDeviceContext(const AVCodec *codec)
{
    AVHWDeviceType hwTypes[] = {
#if defined(Q_OS_LINUX)
        AV_HWDEVICE_TYPE_VAAPI,
#elif defined(Q_OS_WIN)
        AV_HWDEVICE_TYPE_D3D11VA,
        AV_HWDEVICE_TYPE_DXVA2,
#elif defined(Q_OS_MACOS)
        AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
#endif
    };

    for (AVHWDeviceType type : hwTypes) {
        // 查找该 codec 是否支持此硬件类型的像素格式
        for (int i = 0; ; i++) {
            const AVCodecHWConfig *hwcfg = avcodec_get_hw_config(codec, i);
            if (!hwcfg) break;

            bool typeMatch = false;
            switch (type) {
            case AV_HWDEVICE_TYPE_VAAPI:     typeMatch = (hwcfg->pix_fmt == AV_PIX_FMT_VAAPI); break;
            case AV_HWDEVICE_TYPE_D3D11VA:   typeMatch = (hwcfg->pix_fmt == AV_PIX_FMT_D3D11); break;
            case AV_HWDEVICE_TYPE_DXVA2:     typeMatch = (hwcfg->pix_fmt == AV_PIX_FMT_DXVA2_VLD); break;
            case AV_HWDEVICE_TYPE_VIDEOTOOLBOX: typeMatch = (hwcfg->pix_fmt == AV_PIX_FMT_VIDEOTOOLBOX); break;
            default: break;
            }
            if (!typeMatch) continue;

            AVBufferRef *deviceCtx = nullptr;
            int ret = av_hwdevice_ctx_create(&deviceCtx, type, nullptr, nullptr, 0);
            if (ret < 0) {
                qDebug() << "Failed to create hw device" << av_hwdevice_get_type_name(type)
                         << ":" << av_err2str(ret);
                continue;
            }
            m_hwDeviceCtx = deviceCtx;
            m_hwPixFmt = hwcfg->pix_fmt;
            m_hwDeviceType = type;
            m_useHardwareDecode = true;
            qDebug() << "HW decode available:" << av_hwdevice_get_type_name(type)
                     << "fmt:" << av_get_pix_fmt_name(hwcfg->pix_fmt);
            return true;
        }
    }

    qDebug() << "No HW decoder available, using software decoding";
    return false;
}
#endif

MediaEngine::MediaEngine(QObject *parent)
    : QObject(parent)
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
    , m_paused(false)
    , m_position(0.0)
    , m_duration(0.0)
    , m_startTimeUs(0)
    , m_pausedDurationUs(0)
    , m_pauseStartUs(0)
    , m_positionTimer(new QTimer(this))
    , m_volume(100.0)       // 默认音量 100%
    , m_muted(false)        // 默认非静音
    , m_audioOutputReady(false)
    , m_speed(1.0)
{
    connect(m_frameQueueDrainTimer, &QTimer::timeout, this, [this]() {
        if (!m_videoFrameQueue) return;
        while (m_videoFrameQueue->size() > 0) {
            AVFrame *frame = m_videoFrameQueue->tryPop(0);
            if (frame) av_frame_free(&frame);
            else break;
        }
    });

    m_positionTimer->setInterval(250);
    connect(m_positionTimer, &QTimer::timeout, this, &MediaEngine::updatePosition);
}

MediaEngine::~MediaEngine()
{
    stop();
}

bool MediaEngine::initFFmpeg(const QString &filename)
{
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;

    if (avformat_open_input(&m_fmtCtx, filename.toUtf8().constData(), nullptr, nullptr) != 0) {
        qCritical() << "Could not open file:" << filename;
        return false;
    }

    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        qCritical() << "Could not find stream info";
        avformat_close_input(&m_fmtCtx);
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
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    AVCodecParameters *videoPar = m_fmtCtx->streams[m_videoStreamIndex]->codecpar;
    const AVCodec *videoCodec = avcodec_find_decoder(videoPar->codec_id);
    if (!videoCodec) {
        qCritical() << "Video codec not found";
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    m_videoCodecCtx = avcodec_alloc_context3(videoCodec);
    if (!m_videoCodecCtx) {
        qCritical() << "Could not allocate video codec context";
        avformat_close_input(&m_fmtCtx);
        return false;
    }
    avcodec_parameters_to_context(m_videoCodecCtx, videoPar);

#ifdef ENABLE_HWACCEL
    if (createHwDeviceContext(videoCodec)) {
        m_videoCodecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
        m_videoCodecCtx->pix_fmt = m_hwPixFmt;
    }
#endif

    if (avcodec_open2(m_videoCodecCtx, videoCodec, nullptr) < 0) {
#ifdef ENABLE_HWACCEL
        if (m_hwDeviceCtx) {
            qWarning() << "HW decode failed, falling back to software";
            avcodec_free_context(&m_videoCodecCtx);
            m_videoCodecCtx = avcodec_alloc_context3(videoCodec);
            avcodec_parameters_to_context(m_videoCodecCtx, videoPar);
            m_videoCodecCtx->hw_device_ctx = nullptr;
            m_hwPixFmt = AV_PIX_FMT_NONE;

            if (avcodec_open2(m_videoCodecCtx, videoCodec, nullptr) < 0) {
                qCritical() << "Could not open video codec (both HW and SW failed)";
                avcodec_free_context(&m_videoCodecCtx);
                avformat_close_input(&m_fmtCtx);
                return false;
            }
        } else {
            qCritical() << "Could not open video codec";
            avcodec_free_context(&m_videoCodecCtx);
            avformat_close_input(&m_fmtCtx);
            return false;
        }
#else
        qCritical() << "Could not open video codec";
        avcodec_free_context(&m_videoCodecCtx);
        avformat_close_input(&m_fmtCtx);
        return false;
#endif
    }

#ifdef ENABLE_HWACCEL
    if (m_hwPixFmt != AV_PIX_FMT_NONE) {
        m_useHardwareDecode = true;
        qDebug() << "Hardware decoding active:" << av_get_pix_fmt_name(m_hwPixFmt);
    } else {
        m_useHardwareDecode = false;
        qDebug() << "Software decoding (hw device not used by codec)";
    }
#endif

    if (m_audioStreamIndex != -1) {
        AVCodecParameters *audioPar = m_fmtCtx->streams[m_audioStreamIndex]->codecpar;
        const AVCodec *audioCodec = avcodec_find_decoder(audioPar->codec_id);

        if (audioCodec) {
            m_audioCodecCtx = avcodec_alloc_context3(audioCodec);
            if (m_audioCodecCtx) {
                avcodec_parameters_to_context(m_audioCodecCtx, audioPar);

                if (avcodec_open2(m_audioCodecCtx, audioCodec, nullptr) < 0) {
                    qWarning() << "Could not open audio codec, playing without audio";
                    avcodec_free_context(&m_audioCodecCtx);
                    m_audioCodecCtx = nullptr;
                }
            }
        }
    }

    if (!m_audioCodecCtx)
        m_audioStreamIndex = -1;

    av_dump_format(m_fmtCtx, 0, filename.toUtf8().constData(), 0);

    m_duration = m_fmtCtx->duration != AV_NOPTS_VALUE
        ? m_fmtCtx->duration / (double)AV_TIME_BASE
        : 0.0;
    emit durationChanged(m_duration);

    return true;
}

void MediaEngine::start()
{
    if (!initFFmpeg(m_filename))
        return;

    m_audioOutputReady = false;
    if (m_audioCodecCtx) {
        SDL_AudioSpec spec;
        std::memset(&spec, 0, sizeof(spec));
        spec.freq = 44100;
        spec.format = AUDIO_S16SYS;
        spec.channels = 2;
        spec.samples = 1024;

        m_audioOutputReady = m_audioOutput->initialize(spec);
        if (m_audioOutputReady)
            m_audioOutput->setSyncController(m_syncController);
    }

    m_syncController->reset();
    m_syncController->setSpeed(m_speed);

    m_position = 0.0;
    m_startTimeUs = av_gettime();
    m_pausedDurationUs = 0;
    m_pauseStartUs = 0;

    startThreads();

    if (m_videoThread)
        m_videoThread->setSpeed(m_speed);
    if (m_audioThread)
        m_audioThread->setSpeed(m_speed);

    m_positionTimer->start();
}

void MediaEngine::stop()
{
    m_positionTimer->stop();
    m_paused = false;
    stopThreads();
    cleanup();
    m_position = 0.0;
    m_duration = 0.0;
    emit positionChanged(0.0);
    emit durationChanged(0.0);
}

bool MediaEngine::open(const QString &url)
{
    stop();
    QString path = url;
    if (path.startsWith("file://"))
        path = path.mid(7);
    m_filename = path;
    start();
    return m_fmtCtx != nullptr;
}

void MediaEngine::pause()
{
    if (m_paused.exchange(true))
        return;
    m_audioOutput->pause();
    m_pauseStartUs = av_gettime();
    emit pausedChanged(true);
}

void MediaEngine::resume()
{
    if (!m_paused.exchange(false))
        return;
    m_audioOutput->resume();
    m_pausedDurationUs += av_gettime() - m_pauseStartUs;
    m_pauseStartUs = 0;
    emit pausedChanged(false);
}

void MediaEngine::togglePause()
{
    if (m_paused)
        resume();
    else
        pause();
}

bool MediaEngine::isPaused() const
{
    return m_paused;
}

void MediaEngine::seek(double seconds)
{
    if (!m_fmtCtx)
        return;

    m_positionTimer->stop();

    bool wasPaused = m_paused;
    if (!wasPaused)
        pause();

    m_audioOutput->reset();
    stopThreads();

    int64_t targetUs = static_cast<int64_t>(seconds * AV_TIME_BASE);
    if (targetUs < 0) targetUs = 0;
    if (m_duration > 0 && targetUs > static_cast<int64_t>(m_duration * AV_TIME_BASE))
        targetUs = static_cast<int64_t>(m_duration * AV_TIME_BASE);

    int64_t targetStreamTs = av_rescale_q(targetUs, AV_TIME_BASE_Q,
                                          m_fmtCtx->streams[m_videoStreamIndex]->time_base);
    int ret = avformat_seek_file(m_fmtCtx, m_videoStreamIndex,
                                 INT64_MIN, targetStreamTs, targetStreamTs, 0);
    if (ret < 0)
        qWarning() << "Seek failed:" << ret;

    avcodec_flush_buffers(m_videoCodecCtx);
    if (m_audioCodecCtx)
        avcodec_flush_buffers(m_audioCodecCtx);

    qint64 now = av_gettime();
    m_startTimeUs = now - static_cast<qint64>(seconds * 1000000 / qMax(m_speed, 0.1));
    m_pausedDurationUs = 0;
    m_position = seconds;
    emit positionChanged(m_position);

    m_pauseStartUs = now;
    startThreads();

    if (m_videoThread)
        m_videoThread->setSpeed(m_speed);
    if (m_audioThread)
        m_audioThread->setSpeed(m_speed);

    if (!wasPaused)
        resume();

    m_positionTimer->start();
}

double MediaEngine::position() const
{
    return m_position;
}

double MediaEngine::duration() const
{
    return m_duration;
}

// --- 速度控制 ---

void MediaEngine::setSpeed(double speed)
{
    speed = qBound(0.1, speed, 5.0);
    if (qFuzzyCompare(m_speed, speed))
        return;
    double oldSpeed = m_speed;
    m_speed = speed;
    if (m_startTimeUs != 0 && !m_paused) {
        qint64 now = av_gettime();
        double currentPos = (now - m_startTimeUs - m_pausedDurationUs) * oldSpeed / 1000000.0;
        m_startTimeUs = now - m_pausedDurationUs - static_cast<qint64>(currentPos * 1000000.0 / speed);
    }
    // Clear all queues to prevent stale frames from causing burst playback
    // and to unblock the audio decode pipeline.
    if (m_videoFrameQueue)
        m_videoFrameQueue->clear();
    if (m_audioFrameQueue)
        m_audioFrameQueue->clear();
    m_syncController->setSpeed(speed);
    if (m_videoThread)
        m_videoThread->setSpeed(speed);
    if (m_audioThread)
        m_audioThread->setSpeed(speed);
    emit speedChanged(m_speed);
}

double MediaEngine::speed() const
{
    return m_speed;
}

// --- 音量控制实现，转发到 AudioOutput ---

void MediaEngine::setVolume(double vol)
{
    vol = qBound(0.0, vol, 100.0);
    if (qFuzzyCompare(m_volume, vol))
        return;
    m_volume = vol;
    m_audioOutput->setVolume(vol);          // 应用到 SDL 混音
    emit volumeChanged(m_volume);
}

void MediaEngine::setMuted(bool muted)
{
    if (m_muted == muted)
        return;
    m_muted = muted;
    m_audioOutput->setMuted(muted);         // 应用到 SDL 回调
    emit mutedChanged(m_muted);
}

double MediaEngine::volume() const
{
    return m_volume;
}

bool MediaEngine::muted() const
{
    return m_muted;
}

void MediaEngine::updatePosition()
{
    if (m_paused || m_startTimeUs == 0)
        return;

    double pos = (av_gettime() - m_startTimeUs - m_pausedDurationUs) * m_speed / 1000000.0;
    if (pos < 0) pos = 0;
    if (m_duration > 0 && pos > m_duration) pos = m_duration;

    if (qAbs(pos - m_position) > 0.01) {
        m_position = pos;
        emit positionChanged(m_position);
    }
}

void MediaEngine::startThreads()
{
    m_videoPacketQueue = new PacketQueue(64);
    m_audioPacketQueue = new PacketQueue(64);
    m_videoFrameQueue = new FrameQueue(24);

    if (m_audioCodecCtx && m_audioOutputReady) {
        m_audioFrameQueue = new FrameQueue(24);
        m_audioOutput->setFrameQueue(m_audioFrameQueue);
    }

    m_demuxThread = new DemuxThread(this);
    m_demuxThread->setFormatContext(m_fmtCtx);
    m_demuxThread->setStreamIndices(m_videoStreamIndex, m_audioStreamIndex);
    m_demuxThread->setPacketQueues(m_videoPacketQueue, m_audioPacketQueue);
    m_demuxThread->setPausedRef(m_paused);
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
    m_videoThread->setPausedRef(m_paused);
    connect(m_videoThread, &VideoDecodeThread::frameReady, this, &MediaEngine::frameReady);

#ifdef ENABLE_HWACCEL
    if (m_useHardwareDecode && m_hwDeviceCtx) {
        m_videoThread->setHwContext(m_hwDeviceCtx, m_hwPixFmt);
    }
#endif

    if (m_audioCodecCtx && m_audioOutputReady) {
        m_audioThread = new AudioDecodeThread(this);
        m_audioThread->setCodecContext(m_audioCodecCtx);
        m_audioThread->setPacketQueue(m_audioPacketQueue);
        m_audioThread->setFrameQueue(m_audioFrameQueue);
        m_audioThread->setPausedRef(m_paused);
        m_audioThread->setTimeBase(m_fmtCtx->streams[m_audioStreamIndex]->time_base);
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

    m_audioOutput->pause();
    m_audioOutput->reset();
    m_audioOutput->setFrameQueue(nullptr);

    delete m_videoPacketQueue; m_videoPacketQueue = nullptr;
    delete m_audioPacketQueue; m_audioPacketQueue = nullptr;
    delete m_videoFrameQueue; m_videoFrameQueue = nullptr;
    delete m_audioFrameQueue; m_audioFrameQueue = nullptr;
}

void MediaEngine::cleanup()
{
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

#ifdef ENABLE_HWACCEL
    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwDeviceCtx = nullptr;
    }
    m_hwPixFmt = AV_PIX_FMT_NONE;
    m_hwDeviceType = AV_HWDEVICE_TYPE_NONE;
    m_useHardwareDecode = false;
#endif
}
