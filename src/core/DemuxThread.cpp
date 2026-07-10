#include "DemuxThread.h"
#include "PacketQueue.h"
#include <QDebug>

DemuxThread::DemuxThread(QObject *parent)
    : QThread(parent)
    , m_fmtCtx(nullptr)
    , m_videoStreamIdx(-1)
    , m_audioStreamIdx(-1)
    , m_subtitleStreamIdx(-1)
    , m_videoQueue(nullptr)
    , m_audioQueue(nullptr)
    , m_subtitleQueue(nullptr)
    , m_quit(false)
    , m_paused(nullptr)
{
}DemuxThread::~DemuxThread()
{
    stopRead();
    wait();
}

void DemuxThread::setFormatContext(AVFormatContext *ctx)
{
    m_fmtCtx = ctx;
}

void DemuxThread::setStreamIndices(int videoIdx, int audioIdx, int subtitleIdx)
{
    m_videoStreamIdx = videoIdx;
    m_audioStreamIdx = audioIdx;
    m_subtitleStreamIdx.store(subtitleIdx, std::memory_order_release);
}

void DemuxThread::setPacketQueues(PacketQueue *videoQueue, PacketQueue *audioQueue, PacketQueue *subtitleQueue)
{
    m_videoQueue = videoQueue;
    m_audioQueue = audioQueue;
    m_subtitleQueue = subtitleQueue;
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
    if (m_subtitleQueue) {
        m_subtitleQueue->flush();
        m_subtitleQueue->setFinished(true);
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
            av_packet_unref(pkt);
            if (ret == AVERROR_EOF) {
                emit eofReached();
                break;
            }
            m_consecutiveErrors++;
            if (m_consecutiveErrors > 50) {
                qWarning() << "DemuxThread: too many consecutive errors, stopping";
                emit errorOccurred("Too many consecutive read errors");
                break;
            }
            char errbuf[128] = {0};
            av_strerror(ret, errbuf, sizeof(errbuf));
            emit errorOccurred(QString("av_read_frame error: %1").arg(errbuf));
            msleep(10);
            continue;
        }

        m_consecutiveErrors = 0;

        if (pkt->stream_index == m_videoStreamIdx && m_videoQueue) {
            m_videoQueue->push(pkt);
        } else if (pkt->stream_index == m_audioStreamIdx && m_audioQueue) {
            m_audioQueue->push(pkt);
        } else if (pkt->stream_index == m_subtitleStreamIdx.load(std::memory_order_acquire) && m_subtitleQueue) {
            m_subtitleQueue->push(pkt);
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);

    if (m_videoQueue) m_videoQueue->setFinished(true);
    if (m_audioQueue) m_audioQueue->setFinished(true);
    if (m_subtitleQueue) m_subtitleQueue->setFinished(true);
}
