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
    , m_syncController(nullptr)
    , m_audioFifo(av_fifo_alloc2(kFifoSize, 1, 0))
{
}

AudioOutput::~AudioOutput()
{
    closeDevice();
    if (m_audioFifo)
        av_fifo_freep2(&m_audioFifo);
    if (SDL_WasInit(SDL_INIT_AUDIO))
        SDL_Quit();
}

bool AudioOutput::initialize(const SDL_AudioSpec &spec)
{
    if (m_audioDeviceID != 0) {
        m_audioSpec = spec;
        SDL_PauseAudioDevice(m_audioDeviceID, 0);
        return true;
    }

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
        return false;
    }

    m_audioSpec = obtainedSpec;
    qDebug("AudioOutput: wanted freq=%d got freq=%d samples=%d format=0x%x",
           wantedSpec.freq, obtainedSpec.freq,
           obtainedSpec.samples, obtainedSpec.format);
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
    m_volume.store(qBound(0.0, vol, 100.0), std::memory_order_relaxed);
}

void AudioOutput::setMuted(bool muted)
{
    m_muted.store(muted, std::memory_order_relaxed);
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
}

void AudioOutput::closeDevice()
{
    if (m_audioDeviceID != 0) {
        SDL_CloseAudioDevice(m_audioDeviceID);
        m_audioDeviceID = 0;
    }

    if (m_audioFifo)
        av_fifo_reset2(m_audioFifo);
    m_frameQueue = nullptr;
}

void AudioOutput::reset()
{
    if (m_audioDeviceID != 0)
        SDL_LockAudioDevice(m_audioDeviceID);
    if (m_audioFifo)
        av_fifo_reset2(m_audioFifo);
    if (m_audioDeviceID != 0)
        SDL_UnlockAudioDevice(m_audioDeviceID);
}

double AudioOutput::volume() const
{
    return m_volume.load(std::memory_order_relaxed);
}

bool AudioOutput::muted() const
{
    return m_muted.load(std::memory_order_relaxed);
}

double AudioOutput::getAudioClock() const
{
    if (m_syncController)
        return m_syncController->getAudioClock();
    return 0.0;
}

void AudioOutput::fillAudioFifo()
{
    if (!m_frameQueue || !m_audioFifo)
        return;

    while (av_fifo_can_write(m_audioFifo) >= 4096) {
        AVFrame *frame = m_frameQueue->tryPop(0);
        if (!frame)
            break;

        // NB: linesize[0] includes alignment padding; use actual data size.
        av_fifo_write(m_audioFifo, frame->data[0],
                      frame->nb_samples * frame->ch_layout.nb_channels
                      * static_cast<int>(sizeof(int16_t)));

        if (m_syncController)
            m_syncController->updateAudioClock(
                frame->pts * av_q2d(frame->time_base));

        av_frame_free(&frame);
    }
}

void AudioOutput::sdlAudioCallback(void *userdata, Uint8 *stream, int len)
{
    auto *output = static_cast<AudioOutput *>(userdata);

    SDL_memset(stream, 0, len);

    if (!output->m_frameQueue || !output->m_audioFifo)
        return;

    double vol = output->m_volume.load(std::memory_order_relaxed);
    bool muted = output->m_muted.load(std::memory_order_relaxed);
    int volume = muted ? 0
        : static_cast<int>(vol / 100.0 * SDL_MIX_MAXVOLUME);

    // Pre-fill FIFO from FrameQueue when running low
    output->fillAudioFifo();

    // Read from FIFO and mix into stream
    uint8_t buf[4096];
    while (len > 0) {
        size_t available = av_fifo_can_read(output->m_audioFifo);
        if (available <= 0)
            break;

        int toRead = qMin(len, qMin((int)available, (int)sizeof(buf)));
        av_fifo_read(output->m_audioFifo, buf, toRead);
        SDL_MixAudioFormat(stream, buf, output->m_audioSpec.format,
                           (Uint32)toRead, volume);
        stream += toRead;
        len -= toRead;
    }
}
