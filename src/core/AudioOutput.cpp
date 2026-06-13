#include "AudioOutput.h"
#include "FrameQueue.h"
#include "AVSyncController.h"
#include <QDebug>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
}

AudioOutput::AudioOutput(QObject *parent)
    : QObject(parent)
    , m_audioDeviceID(0)
    , m_frameQueue(nullptr)
    , m_volume(100.0)
    , m_muted(false)
    , m_audioBuf(nullptr)
    , m_audioBufSize(0)
    , m_audioBufIndex(0)
    , m_syncController(nullptr)
    , m_currentFrame(nullptr)
{
}

AudioOutput::~AudioOutput()
{
    closeDevice();
    if (SDL_WasInit(SDL_INIT_AUDIO))
        SDL_Quit();
}

bool AudioOutput::initialize(const SDL_AudioSpec &spec)
{
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        qCritical() << "SDL_Init failed:" << SDL_GetError();
        return false;
    }

    SDL_AudioSpec wantedSpec = spec;
    wantedSpec.callback = sdlAudioCallback;
    wantedSpec.userdata = this;

    SDL_AudioSpec obtainedSpec;
    std::memset(&obtainedSpec, 0, sizeof(obtainedSpec));

    m_audioDeviceID = SDL_OpenAudioDevice(nullptr, 0, &wantedSpec, &obtainedSpec, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (m_audioDeviceID == 0) {
        qCritical() << "SDL_OpenAudioDevice failed:" << SDL_GetError();
        SDL_Quit();
        return false;
    }

    m_audioSpec = obtainedSpec;
    SDL_PauseAudioDevice(m_audioDeviceID, 0);
    return true;
}

void AudioOutput::setFrameQueue(FrameQueue *queue)
{
    m_frameQueue = queue;
}

void AudioOutput::setSyncController(AVSyncController *ctrl)
{
    m_syncController = ctrl;
}

void AudioOutput::setVolume(double vol)
{
    m_volume = qBound(0.0, vol, 100.0);
}

void AudioOutput::setMuted(bool muted)
{
    m_muted = muted;
}

void AudioOutput::pause()
{
    if (m_audioDeviceID != 0)
        SDL_PauseAudioDevice(m_audioDeviceID, 1);
}

void AudioOutput::resume()
{
    if (m_audioDeviceID != 0)
        SDL_PauseAudioDevice(m_audioDeviceID, 0);
}

void AudioOutput::stop()
{
    closeDevice();
    if (SDL_WasInit(SDL_INIT_AUDIO))
        SDL_Quit();
}

void AudioOutput::closeDevice()
{
    if (m_frameQueue)
        m_frameQueue->setFinished(true);

    if (m_audioDeviceID != 0) {
        SDL_CloseAudioDevice(m_audioDeviceID);
        m_audioDeviceID = 0;
    }

    if (m_currentFrame) {
        av_frame_free(&m_currentFrame);
        m_audioBuf = nullptr;
    }
    m_audioBufSize = 0;
    m_audioBufIndex = 0;
}

void AudioOutput::reset()
{
    if (m_currentFrame) {
        av_frame_free(&m_currentFrame);
        m_audioBuf = nullptr;
    }
    m_audioBufSize = 0;
    m_audioBufIndex = 0;
}

double AudioOutput::volume() const
{
    return m_volume;
}

bool AudioOutput::muted() const
{
    return m_muted;
}

double AudioOutput::getAudioClock() const
{
    if (m_syncController)
        return m_syncController->getAudioClock();
    return 0.0;
}

void AudioOutput::sdlAudioCallback(void *userdata, Uint8 *stream, int len)
{
    auto *output = static_cast<AudioOutput *>(userdata);

    SDL_memset(stream, 0, len);

    if (output->m_muted || !output->m_frameQueue)
        return;

    int volume = static_cast<int>(output->m_volume / 100.0 * SDL_MIX_MAXVOLUME);

    while (len > 0) {
        if (output->m_audioBufIndex >= output->m_audioBufSize) {
            if (output->m_currentFrame) {
                av_frame_free(&output->m_currentFrame);
                output->m_audioBuf = nullptr;
            }

            AVFrame *frame = output->m_frameQueue->tryPop(0);
            if (!frame)
                return;

            output->m_currentFrame = frame;
            output->m_audioBuf = frame->data[0];
            output->m_audioBufSize = frame->linesize[0];
            output->m_audioBufIndex = 0;

            if (output->m_syncController)
                output->m_syncController->updateAudioClock(
                    frame->pts * av_q2d(frame->time_base));
        }

        int remaining = output->m_audioBufSize - output->m_audioBufIndex;
        int toCopy = qMin(len, remaining);

        SDL_MixAudioFormat(stream,
                           output->m_audioBuf + output->m_audioBufIndex,
                           output->m_audioSpec.format,
                           toCopy, volume);

        output->m_audioBufIndex += toCopy;
        stream += toCopy;
        len -= toCopy;
    }
}
