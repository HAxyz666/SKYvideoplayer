#pragma once

#include <QThread>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
}

class PacketQueue;

class DemuxThread : public QThread
{
    Q_OBJECT

public:
    explicit DemuxThread(QObject *parent = nullptr);
    ~DemuxThread() override;

    void setFormatContext(AVFormatContext *ctx);
    void setStreamIndices(int videoIdx, int audioIdx, int subtitleIdx);
    void setPacketQueues(PacketQueue *videoQueue, PacketQueue *audioQueue, PacketQueue *subtitleQueue);
    // m_subtitleStreamIdx may change during playback (user switches subtitle
    // track) from the GUI thread while run() reads it here — atomic needed.
    void setSubtitleStreamIndex(int idx) { m_subtitleStreamIdx.store(idx, std::memory_order_release); }
    // m_subtitleQueue 同样在播放中可变（切换字幕流时被替换/清空），须原子访问
    void setSubtitleQueue(PacketQueue *queue) { m_subtitleQueue.store(queue, std::memory_order_release); }
    void clearSubtitleQueue() { m_subtitleQueue.store(nullptr, std::memory_order_release); }
    void stopRead();
    void setPausedRef(const std::atomic<bool> &paused);

    // 暂停态下单步推进（帧步进用）：置位后本线程暂停循环放行并持续读取，
    // 直到 MediaEngine（显示线程取到步进帧后）调用 clearStepRequest 复位。
    void requestStep() { m_stepRequested.store(true, std::memory_order_release); }
    void clearStepRequest() { m_stepRequested.store(false, std::memory_order_release); }

    // 轻量 seek 请求（本地文件、解码线程常驻时由 MediaEngine 发起）：
    // 置位后暂停循环放行；run() 内由本线程执行 avformat_seek_file +
    // 清空包队列 + 投入冲刷标记，完成后置位 m_seekDone。
    void requestSeek(int64_t targetUs);
    bool seekDone() const { return m_seekDone.load(std::memory_order_acquire); }
    // EOF 排空循环进行中：此期间不响应 seek 请求（排空循环不检查请求），
    // MediaEngine 检测到后应立即回退重路径，避免请求滞留导致 seek 丢失。
    bool isDraining() const { return m_draining.load(std::memory_order_acquire); }

protected:
    void run() override;

signals:
    void eofReached();
    void errorOccurred(const QString &message);

private:
    // 拆卸单个队列：请求退出 + 清空 + 置完成（stopRead 用）
    void teardownQueue(PacketQueue *queue);
    // 置位全部队列完成（EOF/线程退出时用）
    void finishAllQueues();
    AVFormatContext *m_fmtCtx;
    int m_videoStreamIdx;
    int m_audioStreamIdx{-1};
    std::atomic<int> m_subtitleStreamIdx{-1};
    PacketQueue *m_videoQueue;
    PacketQueue *m_audioQueue{nullptr};
    std::atomic<PacketQueue *> m_subtitleQueue{nullptr};

    std::atomic<bool> m_quit;
    const std::atomic<bool> *m_paused;
    std::atomic<bool> m_stepRequested{false};
    std::atomic<bool> m_seekRequested{false};
    std::atomic<int64_t> m_seekTargetUs{0};
    std::atomic<bool> m_seekDone{false};
    std::atomic<bool> m_draining{false};
    int m_consecutiveErrors{0};
};
