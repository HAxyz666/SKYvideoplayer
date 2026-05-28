#include "AudioOutput.h"
#include <QDebug>
#include <cstring>

extern "C" {
#include <libavutil/opt.h>
}

AudioOutput::AudioOutput(QObject *parent)
    : QObject(parent)
    , m_audioBuffer(nullptr)
    , m_audioBufferSize(0)
    , m_audioBufferIndex(0)
    , m_sdlInited(false)
{
}

AudioOutput::~AudioOutput()
{
    stop();
}

bool AudioOutput::init(AVCodecContext *audioCodecCtx)
{
    if (!audioCodecCtx)
        return false;

    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        qCritical() << "SDL_Init failed:" << SDL_GetError();
        return false;
    }

    SDL_AudioSpec wantedSpec;
    std::memset(&wantedSpec, 0, sizeof(wantedSpec));
    wantedSpec.freq = 44100;
    wantedSpec.format = AUDIO_S16SYS;
    wantedSpec.channels = 2;
    wantedSpec.samples = 1024;
    wantedSpec.callback = sdlAudioCallback;
    wantedSpec.userdata = this;

    if (SDL_OpenAudio(&wantedSpec, nullptr) < 0) {
        qCritical() << "SDL_OpenAudio failed:" << SDL_GetError();
        SDL_Quit();
        return false;
    }

    m_sdlInited = true;
    SDL_PauseAudio(0);
    return true;
}

void AudioOutput::enqueue(uint8_t *data, int size)
{
    QMutexLocker locker(&m_audioMutex);
    AudioPacket pkt;
    pkt.data = data;
    pkt.size = size;
    m_audioQueue.enqueue(pkt);
    m_audioCond.wakeAll();
}

void AudioOutput::stop()
{
    {
        QMutexLocker locker(&m_audioMutex);
        while (!m_audioQueue.isEmpty()) {
            AudioPacket pkt = m_audioQueue.dequeue();
            av_free(pkt.data);
        }
    }

    if (m_audioBuffer) {
        av_free(m_audioBuffer);
        m_audioBuffer = nullptr;
    }

    if (m_sdlInited) {
        SDL_CloseAudio();
        SDL_Quit();
        m_sdlInited = false;
    }
}

bool AudioOutput::isInited() const
{
    return m_sdlInited;
}

void AudioOutput::sdlAudioCallback(void *userdata, Uint8 *stream, int len)
{
    AudioOutput *output = static_cast<AudioOutput *>(userdata);

    SDL_memset(stream, 0, len);

    while (len > 0) {
        if (output->m_audioBufferIndex >= output->m_audioBufferSize) {
            QMutexLocker locker(&output->m_audioMutex);

            if (output->m_audioQueue.isEmpty()) {
                return;
            }

            AudioPacket pkt = output->m_audioQueue.dequeue();
            output->m_audioBuffer = pkt.data;
            output->m_audioBufferSize = pkt.size;
            output->m_audioBufferIndex = 0;
        }

        int remaining = output->m_audioBufferSize - output->m_audioBufferIndex;
        int toCopy = (len < remaining) ? len : remaining;

        SDL_MixAudio(stream, output->m_audioBuffer + output->m_audioBufferIndex, toCopy, SDL_MIX_MAXVOLUME);

        output->m_audioBufferIndex += toCopy;
        stream += toCopy;
        len -= toCopy;

        if (output->m_audioBufferIndex >= output->m_audioBufferSize) {
            av_free(output->m_audioBuffer);
            output->m_audioBuffer = nullptr;
        }
    }
}
