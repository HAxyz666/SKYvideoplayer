#pragma once

#include <QThread>
#include <atomic>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavfilter/avfilter.h>
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
    bool initFilterGraph(double tempo);
    void destroyFilterGraph();
    void applySpeed();

    AVCodecContext *m_codecCtx;
    PacketQueue *m_packetQueue;
    FrameQueue *m_frameQueue;
    SwrContext *m_swrCtx;
    std::atomic<bool> m_quit;
    const std::atomic<bool> *m_paused;
    AVRational m_timeBase;

    AVFilterGraph *m_filterGraph;
    AVFilterContext *m_abufferCtx;
    AVFilterContext *m_aresampleInCtx;
    AVFilterContext *m_atempoCtx;
    AVFilterContext *m_aresampleOutCtx;
    AVFilterContext *m_abuffersinkCtx;

    std::atomic<bool> m_speedDirty;
    double m_pendingSpeed;
    std::atomic<double> m_currentSpeed{1.0};
    std::mutex m_speedMutex;
    std::atomic<int> m_outputSampleRate{0};
};
