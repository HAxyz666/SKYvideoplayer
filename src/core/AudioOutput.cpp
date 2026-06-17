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
    // 使用 memory_order_relaxed 仅需原子性，无需顺序一致性屏障
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
    // 先关设备 — SDL_CloseAudioDevice 阻塞直到回调退出
    if (m_audioDeviceID != 0) {
        SDL_CloseAudioDevice(m_audioDeviceID);
        m_audioDeviceID = 0;
    }

    // 回调已退出，安全释放资源
    if (m_currentFrame) {
        av_frame_free(&m_currentFrame);
        m_audioBuf = nullptr;
    }
    m_audioBufSize = 0;
    m_audioBufIndex = 0;
    m_frameQueue = nullptr;
}

void AudioOutput::reset()
{
    if (m_audioDeviceID != 0)
        SDL_LockAudioDevice(m_audioDeviceID);
    if (m_currentFrame) {
        av_frame_free(&m_currentFrame);
        m_audioBuf = nullptr;
    }
    m_audioBufSize = 0;
    m_audioBufIndex = 0;
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

void AudioOutput::sdlAudioCallback(void *userdata, Uint8 *stream, int len)
{
    auto *output = static_cast<AudioOutput *>(userdata);

    // 先静默输出（输出静音 PCM），这样即使下方 break 也能保证静音不爆音
    SDL_memset(stream, 0, len);

    FrameQueue *fq = output->m_frameQueue;
    if (!fq)
        return;

    // 静音时音量强制为 0，但仍然消费帧，避免 FrameQueue 堆积 → 阻塞解码管线 → 视频卡死
    // 显式 load() 避免隐式 operator T() 的潜在问题
    double vol = output->m_volume.load(std::memory_order_relaxed);
    bool muted = output->m_muted.load(std::memory_order_relaxed);
    int volume = muted ? 0
        : static_cast<int>(vol / 100.0 * SDL_MIX_MAXVOLUME);

    while (len > 0) {
        if (output->m_audioBufIndex >= output->m_audioBufSize) {
            if (output->m_currentFrame) {
                av_frame_free(&output->m_currentFrame);
                output->m_audioBuf = nullptr;
            }

            AVFrame *frame = fq->tryPop(0);
            if (!frame)
                break;

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
