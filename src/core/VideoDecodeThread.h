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

struct YUVFrame;

class PacketQueue;
class FrameQueue;

class VideoDecodeThread : public QThread
{
    Q_OBJECT

public:
    explicit VideoDecodeThread(QObject *parent = nullptr);
    ~VideoDecodeThread() override;

    void setCodecContext(AVCodecContext *ctx);
    void setPacketQueue(PacketQueue *queue);
    void setFrameQueue(FrameQueue *queue);
    void setTimeBase(AVRational tb);
    void stopDecode();
    void setPausedRef(const std::atomic<bool> &paused);
    void adjustStartTime(int64_t offset = -1);
    void setSpeed(double speed);
#ifdef ENABLE_HWACCEL
    void setHwContext(AVBufferRef *ctx, AVPixelFormat pixFmt);
#endif

protected:
    void run() override;

signals:
    void frameReady(const YUVFrame &frame);

private:
    AVCodecContext *m_codecCtx;
    PacketQueue *m_packetQueue;
    FrameQueue *m_frameQueue;
    AVRational m_timeBase;
    std::atomic<int64_t> m_startTime;
    std::atomic<int64_t> m_pauseStartTime{0};
    std::atomic<bool> m_quit;
    const std::atomic<bool> *m_paused;
    std::atomic<bool> m_firstFrame;
    std::atomic<double> m_speed{1.0};
    int64_t m_driftCompensation{0};
#ifdef ENABLE_HWACCEL
    AVBufferRef *m_hwDeviceCtx{nullptr};
    AVPixelFormat m_hwPixFmt{AV_PIX_FMT_NONE};
#endif
};
