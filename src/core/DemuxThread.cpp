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

void DemuxThread::requestSeek(int64_t targetUs)
{
    m_seekTargetUs.store(targetUs, std::memory_order_release);
    m_seekDone.store(false, std::memory_order_release);
    m_seekRequested.store(true, std::memory_order_release);
    // 若本线程正阻塞于满队列 push（队列满且解码线程暂停），
    // 通知其立即醒来处理请求，seek 不被拖到下一次 pop 才执行。
    if (m_videoQueue)
        m_videoQueue->notifySeekWaiters();
    if (m_audioQueue)
        m_audioQueue->notifySeekWaiters();
    if (auto *subQueue = m_subtitleQueue.load(std::memory_order_acquire))
        subQueue->notifySeekWaiters();
}

void DemuxThread::stopRead()
{
    m_quit = true;
    // 请求所有队列退出，唤醒阻塞的 push() 调用
    teardownQueue(m_videoQueue);
    teardownQueue(m_subtitleQueue.load(std::memory_order_acquire));
    teardownQueue(m_audioQueue);
}

// 拆卸单个队列：请求退出 + 清空 + 置完成，唤醒阻塞中的 push/pop。
void DemuxThread::teardownQueue(PacketQueue *queue)
{
    if (!queue)
        return;
    queue->requestQuit();
    queue->flush();
    queue->setFinished(true);
}

// 置位全部队列完成：EOF 或线程退出时通知解码线程不再有新包。
void DemuxThread::finishAllQueues()
{
    if (m_videoQueue) m_videoQueue->setFinished(true);
    if (m_audioQueue) m_audioQueue->setFinished(true);
    if (auto *subQueue = m_subtitleQueue.load(std::memory_order_acquire))
        subQueue->setFinished(true);
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
        emit fatalErrorOccurred("Failed to allocate AVPacket");
        return;
    }

    while (!m_quit) {
        if (m_paused && m_paused->load()) {
            // 步进请求（帧步进）/ seek 请求时放行本轮：持续读取直至复位标志
            if (!m_stepRequested.load(std::memory_order_acquire)
                && !m_seekRequested.load(std::memory_order_acquire)) {
                while (!m_quit && m_paused->load()
                       && !m_stepRequested.load(std::memory_order_acquire)
                       && !m_seekRequested.load(std::memory_order_acquire))
                    msleep(10);
                if (m_quit) break;
                if (!m_stepRequested.load(std::memory_order_acquire)
                    && !m_seekRequested.load(std::memory_order_acquire))
                    continue;
            }
        }

        // 轻量 seek 请求：容器 seek + 清空包队列 + 投入冲刷标记。
        // 解码线程按 FIFO 顺序消费：标记之前的旧包已清空，标记之后的包为
        // 新位置内容；暂停中的解码线程由 MediaEngine 在 seekDone 后置位
        // 各自的冲刷唤醒标志，消费标记并冲刷编解码器/帧队列。
        if (m_seekRequested.load(std::memory_order_acquire)) {
            m_seekRequested.store(false, std::memory_order_release);
            int64_t targetUs = m_seekTargetUs.load(std::memory_order_acquire);

            int ret = avformat_seek_file(m_fmtCtx, -1,
                                         INT64_MIN, targetUs, targetUs,
                                         AVSEEK_FLAG_BACKWARD);
            if (ret < 0)
                qWarning() << "DemuxThread seek failed:" << ret;

            auto flushQueue = [](PacketQueue *q) {
                if (!q)
                    return;
                // 轻量 seek 仅在 demux 存活时发生，队列 finished 恒为 false
                // （仅 EOF 退出前/拆卸时置位），无需 resetFinished。
                q->clearSeekNotify();
                q->flush();
                q->pushFlushMarker();
            };
            flushQueue(m_videoQueue);
            flushQueue(m_audioQueue);
            flushQueue(m_subtitleQueue.load(std::memory_order_acquire));

            m_seekDone.store(true, std::memory_order_release);
            continue;
        }

        int ret = av_read_frame(m_fmtCtx, pkt);
        if (ret < 0) {
            av_packet_unref(pkt);
            if (ret == AVERROR_EOF) {
                // 通知解码线程不再有新包，然后等待队列排空
                finishAllQueues();

                // 等待解码线程消费完队列中的剩余包（最多10秒）。
                // 此期间不响应 seek 请求（isDraining 供 MediaEngine 检测后
                // 回退重路径，避免请求滞留导致 seek 静默丢失）。
                m_draining.store(true, std::memory_order_release);
                for (int i = 0; i < 1000 && !m_quit; ++i) {
                    bool videoDone = !m_videoQueue || m_videoQueue->size() == 0;
                    bool audioDone = !m_audioQueue || m_audioQueue->size() == 0;
                    if (videoDone && audioDone)
                        break;
                    msleep(10);
                }
                m_draining.store(false, std::memory_order_release);

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
                emit fatalErrorOccurred("Too many consecutive read errors");
                break;
            }
            char errbuf[128] = {0};
            av_strerror(ret, errbuf, sizeof(errbuf));
            // 瞬时读取错误：只记日志并重试，不上报 UI（避免把正常重连误判为
            // 播放结束/失败，导致 UI 回主菜单但引擎仍在播放）。
            qWarning() << "DemuxThread: av_read_frame error:" << errbuf;
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

    finishAllQueues();
}
