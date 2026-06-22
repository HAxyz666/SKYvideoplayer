#pragma once

#include <QObject>
#include <atomic>
#include <cstdint>

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
    void stop();
    void reset();
    void closeDevice();

    double volume() const;
    bool muted() const;
    double getAudioClock() const;

private:
    static void sdlAudioCallback(void *userdata, Uint8 *stream, int len);
    void fillAudioFifo();

    SDL_AudioDeviceID m_audioDeviceID;
    SDL_AudioSpec m_audioSpec;
    FrameQueue *m_frameQueue;
    std::atomic<double> m_volume;
    std::atomic<bool> m_muted;
    AVSyncController *m_syncController;

    AVFifo *m_audioFifo;
    static constexpr int kFifoSize = 4 * 1024 * 1024;
};
