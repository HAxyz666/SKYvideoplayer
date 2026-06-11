#include "DemuxThread.h"
#include "PacketQueue.h"
#include <QDebug>

DemuxThread::DemuxThread(QObject *parent)
    : QThread(parent)
    , m_fmtCtx(nullptr)
    , m_videoStreamIdx(-1)
    , m_audioStreamIdx(-1)
    , m_videoQueue(nullptr)
    , m_audioQueue(nullptr)
    , m_quit(false)
    , m_paused(nullptr)
{
}

DemuxThread::~DemuxThread()
{
    stopRead();
    wait();
}

void DemuxThread::setFormatContext(AVFormatContext *ctx)
{
    m_fmtCtx = ctx;
}

void DemuxThread::setStreamIndices(int videoIdx, int audioIdx)
{
    m_videoStreamIdx = videoIdx;
    m_audioStreamIdx = audioIdx;
}

void DemuxThread::setPacketQueues(PacketQueue *videoQueue, PacketQueue *audioQueue)
{
    m_videoQueue = videoQueue;
    m_audioQueue = audioQueue;
}

void DemuxThread::stopRead()
{
    m_quit = true;
    if (m_videoQueue) {
        m_videoQueue->flush();
        m_videoQueue->setFinished(true);
    }
    if (m_audioQueue) {
        m_audioQueue->flush();
        m_audioQueue->setFinished(true);
    }
}

void DemuxThread::setPausedRef(const std::atomic<bool> &paused)
{
    m_paused = &paused;
}

void DemuxThread::run()
{
    if (!m_fmtCtx)
        return;

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        emit errorOccurred("Failed to allocate AVPacket");
        return;
    }

    while (!m_quit) {
        if (m_paused && m_paused->load()) {
            while (!m_quit && m_paused->load())
                msleep(10);
            if (m_quit) break;
            continue;
        }

        int ret = av_read_frame(m_fmtCtx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                emit eofReached();
                break;
            }
            emit errorOccurred(QString("av_read_frame error: %1").arg(ret));
            msleep(10);
            continue;
        }

        if (pkt->stream_index == m_videoStreamIdx && m_videoQueue) {
            m_videoQueue->push(pkt);
        } else if (pkt->stream_index == m_audioStreamIdx && m_audioQueue) {
            m_audioQueue->push(pkt);
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);

    if (m_videoQueue) m_videoQueue->setFinished(true);
    if (m_audioQueue) m_audioQueue->setFinished(true);
}
