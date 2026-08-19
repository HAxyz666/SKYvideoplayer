#pragma once

#include <QThread>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
#ifdef ENABLE_HWACCEL
#include <libavutil/hwcontext.h>
#endif
}

class PacketQueue;
class FrameQueue;

// Pure video decoder thread. Decodes packets as fast as possible and pushes
// decoded frames into the FrameQueue. All display timing is handled by the
// MediaEngine displayLoop + AVSyncController.
class VideoDecodeThread : public QThread
{
    Q_OBJECT

public:
    explicit VideoDecodeThread(QObject *parent = nullptr);
    ~VideoDecodeThread() override;

    void setCodecContext(AVCodecContext *ctx);
    void setPacketQueue(PacketQueue *queue);
    void setFrameQueue(FrameQueue *queue);
    void stopDecode();
    void setPausedRef(const std::atomic<bool> &paused);
    void setTimeBase(AVRational tb);

    // 暂停态下单步解码（帧步进用）：置位后暂停循环放行，解码一个包并产出
    // 至少一帧后回到暂停等待（产出帧时自动复位，无需外部复位）。
    void requestStep() { m_stepRequested.store(true, std::memory_order_release); }
    void clearStepRequest() { m_stepRequested.store(false, std::memory_order_release); }
    // 冲刷唤醒（轻量 seek）：demux 完成容器 seek 并投入冲刷标记后置位，
    // 使暂停中的本线程醒来消费标记，就地冲刷编解码器与帧队列。
    // 在门控放行与标记消费处都会复位，不会残留导致"暂停中线程不暂停"。
    void wakeFlush() { m_flushWake.store(true, std::memory_order_release); }
    // PTS 丢弃阈值（秒，视频流原始时间基）：seek/音轨切换后 backward seek 落在
    // 上一关键帧，解码出的锚点前帧直接丢弃，不进入显示队列。
    // 否则旧画面会以正常节奏走完整个 GOP（画面卡旧内容），
    // 且视频队列积压会阻塞 demux，饿死音频队列（声音卡住）。
    void setPtsDropBefore(double sec);
#ifdef ENABLE_HWACCEL
    void setHwContext(AVBufferRef *ctx, AVPixelFormat pixFmt);
#endif

protected:
    void run() override;

private:
    AVCodecContext *m_codecCtx;
    PacketQueue *m_packetQueue;
    FrameQueue *m_frameQueue;
    std::atomic<bool> m_quit;
    const std::atomic<bool> *m_paused;
    std::atomic<bool> m_stepRequested{false};
    std::atomic<bool> m_flushWake{false};
    AVRational m_timeBase{1, 90000};
    std::atomic<double> m_ptsDropBefore{-1.0};
#ifdef ENABLE_HWACCEL
    AVBufferRef *m_hwDeviceCtx{nullptr};
    AVPixelFormat m_hwPixFmt{AV_PIX_FMT_NONE};
#endif
};
