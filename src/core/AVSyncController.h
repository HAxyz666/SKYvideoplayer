#pragma once

#include <QObject>
#include <QMutex>

enum class SyncMode {
    AudioMaster,
    VideoMaster
};

// 音频主时钟音视频同步控制器
//
// 音频时钟由音频输出线程通过 updateAudioClock() 更新。
// 视频显示调用者（GUI 线程）对每帧的 pts（秒）调用 computeFrameDelay()
// 并等待返回的延迟时间后再渲染。
class AVSyncController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double speed READ speed WRITE setSpeed)

public:
    explicit AVSyncController(QObject *parent = nullptr);

    // 返回调用者在显示给定 pts（秒）的视频帧之前应等待的延迟时间（秒）。
    // <= 0 表示立即显示。
    // 不修改内部状态——在帧实际被消费后调用 onFrameDisplayed()
    // 以确保下一帧的间隔正确。
    double computeFrameDelay(double videoPts) const;

    // 必须在给定 pts 的视频帧实际显示后调用。
    // 更新内部 PTS 跟踪器和帧间隔估计。
    void onFrameDisplayed(double videoPts);

    // 由音频输出线程在帧开始播放时调用
    void updateAudioClock(double pts);

    // 返回当前音频主时钟（秒，原始时间线）。
    // 用于需要与主时钟对齐但无需经过 computeFrameDelay() 的组件（如字幕查找）。
    double audioClock() const;

    void setSpeed(double speed);
    double speed() const;

    void reset();
    void setSyncMode(SyncMode mode);
    SyncMode syncMode() const;


private:
    mutable QMutex m_mutex;
    SyncMode m_syncMode{SyncMode::AudioMaster};

    double m_audioClock{0.0};      // 当前播放音频的 pts（秒）
    mutable double m_frameLastPts{0.0};    // 上一帧显示的视频帧 pts（秒）
    mutable double m_frameLastDelay{0.04}; // 上一帧间隔（秒），用于回退
    double m_speed{1.0};
    bool m_firstFrame{true};
};
