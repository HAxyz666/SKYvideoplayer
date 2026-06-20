#pragma once

#include <QObject>
#include <atomic>
#include <cstdint>

extern "C" {
#include <SDL2/SDL.h>
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

    SDL_AudioDeviceID m_audioDeviceID;
    SDL_AudioSpec m_audioSpec;
    FrameQueue *m_frameQueue;
    std::atomic<double> m_volume;
    std::atomic<bool> m_muted;
    uint8_t *m_audioBuf;
    uint32_t m_audioBufSize;
    uint32_t m_audioBufIndex;
    AVSyncController *m_syncController;
    AVFrame *m_currentFrame;
};
