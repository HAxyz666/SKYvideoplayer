#pragma once

#include <QThread>
#include <QImage>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/rational.h>
}

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

protected:
    void run() override;

signals:
    void frameReady(const QImage &image);

private:
    AVCodecContext *m_codecCtx;
    PacketQueue *m_packetQueue;
    FrameQueue *m_frameQueue;
    AVRational m_timeBase;
    int64_t m_startTime;
    int64_t m_pauseStartTime;
    std::atomic<bool> m_quit;
    const std::atomic<bool> *m_paused;
    bool m_firstFrame;
};
