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
    if (m_videoQueue) m_videoQueue->flush();
    if (m_audioQueue) m_audioQueue->flush();
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
