#pragma once

#include <QObject>
#include <atomic>
#include <cstdint>
#include <deque>

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
    void setSpeed(double speed);
    int sampleRate() const { return m_audioSpec.freq; }

private:
    static void sdlAudioCallback(void *userdata, Uint8 *stream, int len);
    void fillAudioFifo();
    void updateAudioClock();

    SDL_AudioDeviceID m_audioDeviceID;
    SDL_AudioSpec m_audioSpec;
    FrameQueue *m_frameQueue;
    std::atomic<double> m_volume;
    std::atomic<bool> m_muted;
    AVSyncController *m_syncController;

    AVFifo *m_audioFifo;
    static constexpr int kFifoSize = 32 * 1024;
    double m_bytesPerSecond = 0.0;
    std::atomic<double> m_speed{1.0};

    // Exact audio clock via frame markers.
    // Each push records (PTS, cumulative byte offset).  The clock is
    // the PTS of the frame covering the current consumed position,
    // linearly interpolated by byte progress.  Independent of speed,
    // atempo compression ratio, and FIFO data mix.
    struct Marker { double pts; int64_t byteOffset; };
    std::deque<Marker> m_markers;
    int64_t m_bytesWritten{0};

    double m_lastClock{0.0};
    qint64 m_lastUpdateUs{0};

    // SDL 内部硬件缓冲对应秒数。一个回调 buffer 量。
    // ffplay 的 get_audio_clock 减这一项让 audio_clock 反映"扬声器
    // 正在发声那一刻"的媒体时间。我们这里也照做：audioClock 表示
    // "SDL FIFO 游标位置"，比真实发声早约一个 hw buffer 量。
    // 在 updateAudioClock() 末尾减去，让 audioClock 与 videoPts 同维度。
    double m_hardwareLatencySecs{0.0};

    // atempo filter 在当前倍速下的 pipeline 延迟（秒）。
    // atempo 用 OLA 算法做时间拉伸，0.5x 拉伸 2x / 2.0x 压缩 0.5x，
    // OLA 窗口需要看未来几帧做重叠，导致启动时第一个有效 output frame
    // 落后 demux 第一个 packet 的 PTS 一个 OLA 窗口量（约 200ms）。
    // 让 audioClock 推进率正确但**值**落后 videoPts，video 持续领先。
    // 在 updateAudioClock() 末尾减这个值，让 audioClock 与 videoPts
    // 在启动瞬间就处于同一时间维度。
    double m_atempoPipelineLatencySecs{0.0};
};
