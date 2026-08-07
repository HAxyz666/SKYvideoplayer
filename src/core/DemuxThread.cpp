#include "DemuxThread.h"
#include "PacketQueue.h"
#include <QDebug>

DemuxThread::DemuxThread(QObject *parent)
    : QThread(parent)
    , m_fmtCtx(nullptr)
    , m_videoStreamIdx(-1)
    , m_videoQueue(nullptr)
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
    m_subtitleQueue.store(subtitleQueue, std::memory_order_release);
}

void DemuxThread::stopRead()
{
    m_quit = true;
    // 请求所有队列退出，唤醒阻塞的 push() 调用
    if (m_videoQueue) {
        m_videoQueue->requestQuit();
        m_videoQueue->flush();
        m_videoQueue->setFinished(true);
    }
    if (auto *subQueue = m_subtitleQueue.load(std::memory_order_acquire)) {
        subQueue->requestQuit();
        subQueue->flush();
        subQueue->setFinished(true);
    }
    if (m_audioQueue) {
        m_audioQueue->requestQuit();
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
            av_packet_unref(pkt);
            if (ret == AVERROR_EOF) {
                // 通知解码线程不再有新包，然后等待队列排空
                if (m_videoQueue) m_videoQueue->setFinished(true);
                if (m_audioQueue) m_audioQueue->setFinished(true);
                if (auto *subQueue = m_subtitleQueue.load(std::memory_order_acquire))
                    subQueue->setFinished(true);

                // 等待解码线程消费完队列中的剩余包（最多10秒）
                for (int i = 0; i < 1000 && !m_quit; ++i) {
                    bool videoDone = !m_videoQueue || m_videoQueue->size() == 0;
                    bool audioDone = !m_audioQueue || m_audioQueue->size() == 0;
                    if (videoDone && audioDone)
                        break;
                    msleep(10);
                }

                emit eofReached();
                break;
            }
            // 检查是否被中断（网络超时或主动中断）
            if (ret == AVERROR_EXIT) {
                qWarning() << "DemuxThread: av_read_frame interrupted";
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
        } else if (pkt->stream_index == m_subtitleStreamIdx.load(std::memory_order_acquire)
                   && m_subtitleQueue.load(std::memory_order_acquire)) {
            m_subtitleQueue.load(std::memory_order_acquire)->push(pkt);
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);

    if (m_videoQueue) m_videoQueue->setFinished(true);
    if (m_audioQueue) m_audioQueue->setFinished(true);
    if (auto *subQueue = m_subtitleQueue.load(std::memory_order_acquire))
        subQueue->setFinished(true);
}
