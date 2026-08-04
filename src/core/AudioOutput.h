#pragma once

#include <QObject>
#include <atomic>

extern "C" {
#include <SDL2/SDL.h>
#include <libavutil/fifo.h>
}

class FrameQueue;
class AVSyncController;
struct AVFrame;

class AudioOutput : public QObject
{
    Q_OBJECT

public:
    explicit AudioOutput(QObject *parent = nullptr);
    ~AudioOutput();

    bool initialize(const SDL_AudioSpec &spec);
    void setFrameQueue(FrameQueue *queue);
    void setSyncController(AVSyncController *ctrl);
    void setVolume(double vol);
    void setMuted(bool muted);
    void pause();
    void resume();
    void reset();

    void setSpeed(double speed);

signals:
    // 变速后旧速度预缓冲（FIFO + 解码帧队列）全部被消费完毕，
    // 此时管线内只剩新速度数据，播放节奏已完全切换（SDL 音频线程发出）。
    void speedTransitionFinished();

private:
    static void sdlAudioCallback(void *userdata, Uint8 *stream, int len);
    void fillAudioFifo();
    void closeDevice();

    SDL_AudioDeviceID m_audioDeviceID;
    SDL_AudioSpec m_audioSpec;
    FrameQueue *m_frameQueue;
    std::atomic<double> m_volume;
    std::atomic<bool> m_muted;
    AVSyncController *m_syncController;

    AVFifo *m_audioFifo;
    static constexpr int kFifoSize = 96 * 1024;
    double m_bytesPerSecond = 0.0;
    std::atomic<double> m_speed{1.0};
    std::atomic<double> m_oldSpeed{1.0};
    std::atomic<double> m_oldBytesRemaining{0.0}; // 旧速度数据（FIFO+帧队列）剩余字节
    std::atomic<bool> m_transitionNotified{false}; // 本次变速的耗尽信号是否已发出
};
