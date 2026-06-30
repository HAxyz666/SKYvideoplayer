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

// 纯视频解码线程。尽可能快地解码包并将解码后的帧推入 FrameQueue。
// 所有显示定时由 MediaEngine 视频刷新定时器 + AVSyncController 处理。
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
#ifdef ENABLE_HWACCEL
    AVBufferRef *m_hwDeviceCtx{nullptr};
    AVPixelFormat m_hwPixFmt{AV_PIX_FMT_NONE};
#endif
};
