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
    // 切换音轨时：把新音轨的 PTS 重基到该时刻的主时钟（音频时钟连续性，
    // 不同音轨在容器内可能使用不同的时间戳基）。
    void setPtsAnchor(double sec);
    // 切换音轨时：丢弃原始 PTS 早于该值的解码帧（秒）。
    // seek 以视频关键帧为落点，新音轨可能从锚点之前的内容起播，
    // 不丢弃的话重基后音频内容会整体落后于画面（音画不同步）。
    // -1 表示不丢弃（正常播放/seek）。
    void setPtsDropBefore(double sec);

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
    std::atomic<double> m_ptsAnchor{-1.0};
    std::atomic<double> m_ptsDropBefore{-1.0};
};
