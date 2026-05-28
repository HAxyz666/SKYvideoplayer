#pragma once

#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
}

class AudioOutput : public QObject
{
    Q_OBJECT

public:
    explicit AudioOutput(QObject *parent = nullptr);
    ~AudioOutput();

    bool init(AVCodecContext *audioCodecCtx);
    void enqueue(uint8_t *data, int size);
    void stop();
    bool isInited() const;

private:
    struct AudioPacket {
        uint8_t *data;
        int size;
    };

    static void sdlAudioCallback(void *userdata, Uint8 *stream, int len);

    QQueue<AudioPacket> m_audioQueue;
    QMutex m_audioMutex;
    QWaitCondition m_audioCond;

    uint8_t *m_audioBuffer;
    int m_audioBufferSize;
    int m_audioBufferIndex;

    bool m_sdlInited;
};
