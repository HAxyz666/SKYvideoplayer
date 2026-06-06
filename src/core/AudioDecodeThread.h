#pragma once

#include <QThread>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

class PacketQueue;
class AudioOutput;

class AudioDecodeThread : public QThread
{
    Q_OBJECT

public:
    explicit AudioDecodeThread(QObject *parent = nullptr);
    ~AudioDecodeThread() override;

    void setCodecContext(AVCodecContext *ctx);
    void setPacketQueue(PacketQueue *queue);
    void setAudioOutput(AudioOutput *output);
    void stopDecode();

protected:
    void run() override;

private:
    bool initSwrContext();
    AVFrame *resampleFrame(AVFrame *frame);

    AVCodecContext *m_codecCtx;
    PacketQueue *m_packetQueue;
    AudioOutput *m_audioOutput;
    SwrContext *m_swrCtx;
    std::atomic<bool> m_quit;
};
