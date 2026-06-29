#include "AudioOutput.h"
#include "FrameQueue.h"
#include "AVSyncController.h"
#include <QDebug>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
#include <libavutil/time.h>
}

AudioOutput::AudioOutput(QObject *parent)
    : QObject(parent), m_audioDeviceID(0), m_frameQueue(nullptr),
      m_volume(100.0), m_muted(false), m_syncController(nullptr),
      m_audioFifo(av_fifo_alloc2(kFifoSize, 1, 0)) {}

AudioOutput::~AudioOutput()
{
    closeDevice();
    if (m_audioFifo) av_fifo_freep2(&m_audioFifo);
    if (SDL_WasInit(SDL_INIT_AUDIO)) SDL_QuitSubSystem(SDL_INIT_AUDIO);
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
    m_audioDeviceID = SDL_OpenAudioDevice(nullptr, 0, &wantedSpec, &obtainedSpec,
                                          SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (m_audioDeviceID == 0) {
        qCritical() << "SDL_OpenAudioDevice failed:" << SDL_GetError();
        return false;
    }
    m_audioSpec = obtainedSpec;
    m_bytesPerSecond = obtainedSpec.freq * obtainedSpec.channels * sizeof(int16_t);
    // 一个 SDL 回调 buffer 的播放时长。SDL 内部硬件缓冲通常就是这个量。
    m_hardwareLatencySecs = static_cast<double>(obtainedSpec.samples)
                          / obtainedSpec.freq;
    qDebug("AudioOutput: freq=%d samples=%d fmt=0x%x B/s=%.0f hwLat=%.4fs",
           obtainedSpec.freq, obtainedSpec.samples, obtainedSpec.format,
           m_bytesPerSecond, m_hardwareLatencySecs);
    SDL_PauseAudioDevice(m_audioDeviceID, 0);
    return true;
}

void AudioOutput::setFrameQueue(FrameQueue *queue) {
    if (m_audioDeviceID != 0) SDL_LockAudioDevice(m_audioDeviceID);
    m_frameQueue = queue;
    if (m_audioDeviceID != 0) SDL_UnlockAudioDevice(m_audioDeviceID);
}
void AudioOutput::setSyncController(AVSyncController *c) { m_syncController = c; }
void AudioOutput::setVolume(double v)  { m_volume.store(qBound(0., v, 100.)); }
void AudioOutput::setMuted(bool m)     { m_muted.store(m); }
void AudioOutput::pause()              { if (m_audioDeviceID) SDL_PauseAudioDevice(m_audioDeviceID,1); }
void AudioOutput::resume()             { if (m_audioDeviceID) SDL_PauseAudioDevice(m_audioDeviceID,0); }
void AudioOutput::stop()               { closeDevice(); }

void AudioOutput::closeDevice() {
    if (m_audioDeviceID) {
        SDL_LockAudioDevice(m_audioDeviceID);
        m_frameQueue = nullptr;
        SDL_CloseAudioDevice(m_audioDeviceID);
        m_audioDeviceID = 0;
    }
    if (m_audioFifo) av_fifo_reset2(m_audioFifo);
    m_markers.clear();
    m_bytesWritten = 0;
}
void AudioOutput::reset() {
    if (m_audioDeviceID) SDL_LockAudioDevice(m_audioDeviceID);
    if (m_audioFifo) av_fifo_reset2(m_audioFifo);
    m_markers.clear();
    m_bytesWritten = 0;
    m_lastUpdateUs = 0;
    if (m_audioDeviceID) SDL_UnlockAudioDevice(m_audioDeviceID);
}
double AudioOutput::volume() const { return m_volume.load(); }
bool   AudioOutput::muted()  const { return m_muted.load(); }
double AudioOutput::getAudioClock() const { return 0.0; }
void AudioOutput::setSpeed(double s) {
    double newSpeed = qBound(0.5, s, 2.0);
    if (qFuzzyCompare(m_speed.load(), newSpeed))
        return;

    // The byte-offset <-> content-time relationship changes with speed.
    // Old markers become invalid and must be discarded; otherwise the audio
    // clock drifts and the video syncs to a lagging/leading clock.
    if (m_audioDeviceID) SDL_LockAudioDevice(m_audioDeviceID);
    m_speed.store(newSpeed, std::memory_order_relaxed);
    m_markers.clear();
    if (m_syncController) {
        m_lastClock = m_syncController->audioClock();
        m_lastUpdateUs = av_gettime();
    } else {
        m_lastClock = 0.0;
        m_lastUpdateUs = 0;
    }
    // atempo pipeline 延迟：1.0x 不走 atempo 滤波器无延迟；
    // 0.5x / 1.5x / 2.0x 都走 atempo，OLA 窗口引入 ~200ms 延迟。
    // 经验值，可在实测中调整（ffplay 测的 50ms，但 aresample+atempo+
    // aresample 三层滤镜链可能更长）。
    m_atempoPipelineLatencySecs = qFuzzyCompare(newSpeed, 1.0) ? 0.0 : 0.200;
    if (m_audioDeviceID) SDL_UnlockAudioDevice(m_audioDeviceID);
}

// ── Frame-marker based audio clock ─────────────────────────────────

void AudioOutput::updateAudioClock()
{
    if (!m_syncController || m_bytesPerSecond <= 0.0)
        return;

    int64_t consumed = m_bytesWritten
                       - static_cast<int64_t>(av_fifo_can_read(m_audioFifo));
    double clock = 0.0;
    bool    valid = false;

    // Find the marker pair that brackets the current consumed position.
    // Using the correct pair (not just the first marker) is required when
    // speed changes or frame sizes vary, otherwise the audio clock drifts.
    for (size_t i = 0; i + 1 < m_markers.size(); ++i) {
        if (consumed >= m_markers[i].byteOffset &&
            consumed < m_markers[i + 1].byteOffset) {
            double fb = static_cast<double>(m_markers[i + 1].byteOffset
                                            - m_markers[i].byteOffset);
            if (fb > 0.0) {
                double frac = static_cast<double>(consumed - m_markers[i].byteOffset)
                              / fb;
                double span = m_markers[i + 1].pts - m_markers[i].pts;
                if (span > 0.0 && span < 1.0) {
                    clock = m_markers[i].pts + frac * span;
                    valid = true;
                }
            }
            break;
        }
    }

    if (!valid && !m_markers.empty()) {
        if (consumed >= m_markers.back().byteOffset) {
            clock = m_markers.back().pts;
            valid = true;
        } else if (consumed < m_markers.front().byteOffset) {
            clock = m_markers.front().pts;
            valid = true;
        }
    }

    // If markers can't give us a clock (e.g. speed change cleared old markers
    // or FIFO drained past the last marker), project forward from the last
    // known clock using the current speed. This prevents the audio clock from
    // stalling, which would make computeFrameDelay see a growing diff and
    // over-correct.
    if (!valid && m_lastUpdateUs != 0) {
        double elapsed = static_cast<double>(av_gettime() - m_lastUpdateUs)
                         / 1000000.0;
        double speed = m_speed.load(std::memory_order_relaxed);
        clock = m_lastClock + elapsed * speed;
        valid = true;
    }

    if (valid) {
        // 减去 SDL 内部硬件缓冲延迟，让 audioClock 反映"扬声器正在发声"
        // 的媒体时间，与 videoPts 同维度。
        // 否则 audioClock 偏小，videoPts - audioClock 偏正，video 误以为
        // 自己领先音频，触发 delay = diff 让画面持续推迟显示——长期累积
        // 成可观的画面领先音频漂移（1x 下曾观察到 ~620ms）。
        // 这里不乘 speed：SDL 内部硬件缓冲量是物理量，与播放速率无关。
        clock -= m_hardwareLatencySecs;

        // 减去 atempo pipeline 延迟。0.5x / 1.5x / 2.0x 下 atempo OLA
        // 窗口让第一个有效 output frame 落后 demux PTS 一个窗口量
        // （~200ms），导致 audioClock 推进率正确但值持续落后 videoPts，
        // video 主动 catch up 不够时画面会稳态领先音频（0.5x 下特别
        // 严重，可达 >500ms）。1.0x 不走 atempo，无需补偿。
        clock -= m_atempoPipelineLatencySecs;

        m_lastClock = clock;
        m_lastUpdateUs = av_gettime();
        m_syncController->updateAudioClock(clock);
    }
}

// ── FIFO fill ──────────────────────────────────────────────────────

void AudioOutput::fillAudioFifo()
{
    if (!m_frameQueue || !m_audioFifo) return;

    while (true) {
        AVFrame *frame = m_frameQueue->peek();
        if (!frame) break;

        size_t frameBytes = static_cast<size_t>(frame->nb_samples)
                            * static_cast<size_t>(frame->ch_layout.nb_channels)
                            * sizeof(int16_t);
        if (av_fifo_can_write(m_audioFifo) < static_cast<int>(frameBytes)) break;

        frame = m_frameQueue->tryPop(0);
        if (!frame) break;

        av_fifo_write(m_audioFifo, frame->data[0], frameBytes);

        m_markers.push_back({frame->pts * av_q2d(frame->time_base),
                             m_bytesWritten});
        m_bytesWritten += static_cast<int64_t>(frameBytes);
        while (m_markers.size() > 16) m_markers.pop_front();

        av_frame_free(&frame);
    }
}

// ── SDL callback ───────────────────────────────────────────────────

void AudioOutput::sdlAudioCallback(void *userdata, Uint8 *stream, int len)
{
    auto *out = static_cast<AudioOutput *>(userdata);
    SDL_memset(stream, 0, len);
    if (!out->m_frameQueue || !out->m_audioFifo) return;

    double vol = out->m_volume.load(std::memory_order_relaxed);
    bool muted = out->m_muted.load(std::memory_order_relaxed);
    int v = muted ? 0 : static_cast<int>(vol / 100.0 * SDL_MIX_MAXVOLUME);

    out->fillAudioFifo();

    uint8_t buf[4096];
    while (len > 0) {
        size_t a = av_fifo_can_read(out->m_audioFifo);
        if (a <= 0) break;
        int n = qMin(len, qMin((int)a, (int)sizeof(buf)));
        av_fifo_read(out->m_audioFifo, buf, n);
        SDL_MixAudioFormat(stream, buf, out->m_audioSpec.format, (Uint32)n, v);
        stream += n; len -= n;
    }

    out->updateAudioClock();
}
