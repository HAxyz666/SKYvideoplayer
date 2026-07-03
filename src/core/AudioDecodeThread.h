#pragma once

#include <QThread>
#include <atomic>

#include <sonic.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/rational.h>
}

class PacketQueue;
class FrameQueue;

class AudioDecodeThread : public QThread
{
    Q_OBJECT

public:
    explicit AudioDecodeThread(QObject *parent = nullptr);
    ~AudioDecodeThread() override;

    void setCodecContext(AVCodecContext *ctx);
    void setPacketQueue(PacketQueue *queue);
    void setFrameQueue(FrameQueue *queue);
    void setTimeBase(AVRational tb);
    void stopDecode();
    void setPausedRef(const std::atomic<bool> &paused);
    void setSpeed(double speed);
    void setOutputSampleRate(int rate);

protected:
    void run() override;

private:
    bool initSwrContext();
    AVFrame *resampleFrame(AVFrame *frame);
    void flushSonic();
    void destroySonic();

    AVCodecContext *m_codecCtx;
    PacketQueue *m_packetQueue;
    FrameQueue *m_frameQueue;
    SwrContext *m_swrCtx;
    std::atomic<bool> m_quit;
    const std::atomic<bool> *m_paused;
    AVRational m_timeBase;

    sonicStream m_sonicStream{nullptr};
    std::atomic<double> m_currentSpeed{1.0};
    std::atomic<int> m_outputSampleRate{0};
    double m_lastSpeed{1.0};
    double m_sonicOutputPts{0.0};
};
