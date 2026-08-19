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
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
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
    m_bytesPerSecond = obtainedSpec.freq * obtainedSpec.channels * sizeof(int16_t);
    qDebug("AudioOutput: wanted freq=%d got freq=%d samples=%d format=0x%x",
           wantedSpec.freq, obtainedSpec.freq,
           obtainedSpec.samples, obtainedSpec.format);
    SDL_PauseAudioDevice(m_audioDeviceID, 0);
    return true;
}

// SDL 音频设备锁 RAII：设备未初始化（id == 0）时为空操作。
// 回调线程同样经由 SDL 内部锁访问这些状态，配对加锁避免竞争。
class SdlDeviceLock
{
public:
    explicit SdlDeviceLock(SDL_AudioDeviceID id)
        : m_id(id)
    {
        if (m_id != 0)
            SDL_LockAudioDevice(m_id);
    }
    ~SdlDeviceLock()
    {
        if (m_id != 0)
            SDL_UnlockAudioDevice(m_id);
    }

private:
    SDL_AudioDeviceID m_id;
};

void AudioOutput::setFrameQueue(FrameQueue *queue)
{
    SdlDeviceLock lock(m_audioDeviceID);
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

void AudioOutput::closeDevice()
{
    if (m_audioDeviceID != 0) {
        SDL_LockAudioDevice(m_audioDeviceID);
        m_frameQueue = nullptr;
        SDL_CloseAudioDevice(m_audioDeviceID);
        m_audioDeviceID = 0;
    }

    if (m_audioFifo)
        av_fifo_reset2(m_audioFifo);
}

bool AudioOutput::fifoEmpty() const
{
    return m_audioFifo == nullptr || av_fifo_can_read(m_audioFifo) == 0;
}

void AudioOutput::reset()
{
    SdlDeviceLock lock(m_audioDeviceID);
    if (m_audioFifo)
        av_fifo_reset2(m_audioFifo);
    m_oldBytesRemaining.store(0.0, std::memory_order_relaxed);
    m_oldSpeed.store(m_speed.load(std::memory_order_relaxed), std::memory_order_relaxed);
    m_transitionNotified.store(true, std::memory_order_relaxed);
}

void AudioOutput::setSpeed(double speed)
{
    speed = qBound(0.5, speed, 2.0);
    double oldSpeed = m_speed.load(std::memory_order_relaxed);
    if (qFuzzyCompare(oldSpeed, speed))
        return;
    m_speed.store(speed, std::memory_order_relaxed);
    m_oldSpeed.store(oldSpeed, std::memory_order_relaxed);

    // 旧速度预缓冲 = FIFO 中已填充字节 + 解码帧队列中剩余字节。
    // 帧队列必须计入：sonic 切速是异步的，切速瞬间队列里还有一批旧速度音频，
    // 若按新速度折算会让时钟超前，视频因此落后卡顿。
    SdlDeviceLock lock(m_audioDeviceID);
    double queuedBytes = m_frameQueue ? static_cast<double>(m_frameQueue->totalBytes()) : 0.0;
    double fifoBytes = static_cast<double>(av_fifo_can_read(m_audioFifo));
    m_oldBytesRemaining.store(fifoBytes + queuedBytes, std::memory_order_relaxed);
    m_transitionNotified.store(false, std::memory_order_relaxed);
}

void AudioOutput::fillAudioFifo()
{
    if (!m_frameQueue || !m_audioFifo)
        return;

    while (true) {
        AVFrame *frame = m_frameQueue->peek();
        if (!frame)
            break;

        size_t frameBytes = static_cast<size_t>(frame->nb_samples)
                            * static_cast<size_t>(frame->ch_layout.nb_channels)
                            * sizeof(int16_t);
        if (av_fifo_can_write(m_audioFifo) < static_cast<int>(frameBytes))
            break;

        frame = m_frameQueue->tryPop(0);
        if (!frame)
            break;

        av_fifo_write(m_audioFifo, frame->data[0], frameBytes);

        if (m_syncController) {
            double clock = frame->pts * av_q2d(frame->time_base);
            if (m_bytesPerSecond > 0.0) {
                double bufferedBytes = static_cast<double>(av_fifo_can_read(m_audioFifo));
                double speed = m_speed.load(std::memory_order_relaxed);
                double oldBytes = qMin(bufferedBytes, m_oldBytesRemaining.load(std::memory_order_relaxed));
                double newBytes = bufferedBytes - oldBytes;
                clock -= (oldBytes * m_oldSpeed.load(std::memory_order_relaxed)
                          + newBytes * speed) / m_bytesPerSecond;
            }
            m_syncController->updateAudioClock(clock);
        }

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

    output->fillAudioFifo();

    uint8_t buf[4096];
    while (len > 0) {
        size_t available = av_fifo_can_read(output->m_audioFifo);
        if (available <= 0)
            break;

        int toRead = qMin(len, qMin((int)available, (int)sizeof(buf)));
        av_fifo_read(output->m_audioFifo, buf, toRead);

        // 消费数据后递减旧速度剩余量，确保时钟计算准确
        double oldRemaining = output->m_oldBytesRemaining.load(std::memory_order_relaxed);
        if (oldRemaining > 0.0) {
            oldRemaining = qMax(0.0, oldRemaining - toRead);
            output->m_oldBytesRemaining.store(oldRemaining, std::memory_order_relaxed);

            // 旧速度预缓冲耗尽：通知主线程旧数据已播完，节奏已切换到新速度。
            if (oldRemaining <= 0.0) {
                bool expected = false;
                if (output->m_transitionNotified.compare_exchange_strong(expected, true,
                                                                        std::memory_order_relaxed))
                    emit output->speedTransitionFinished();
            }
        }

        SDL_MixAudioFormat(stream, buf, output->m_audioSpec.format,
                           (Uint32)toRead, volume);
        stream += toRead;
        len -= toRead;
    }
}
