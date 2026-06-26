#pragma once

#include <QObject>
#include <QMutex>

enum class SyncMode {
    AudioMaster,
    VideoMaster,
    ExternalClock
};

// Audio-master A/V sync controller.
//
// Audio clock is updated from the audio output thread via updateAudioClock().
// Video display caller (GUI thread) invokes computeFrameDelay() with each
// frame's pts (seconds) and waits for the returned delay before rendering.
class AVSyncController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)

public:
    explicit AVSyncController(QObject *parent = nullptr);

    // Returns delay (seconds) the caller should wait before displaying the
    // video frame with the given pts (in seconds). <= 0 means display now.
    // Does NOT mutate internal state — call onFrameDisplayed() after the
    // frame is actually consumed so the next frame's interval is correct.
    double computeFrameDelay(double videoPts) const;

    // Must be called after the video frame with the given pts is actually
    // displayed. Updates the internal PTS tracker and frame-interval estimate.
    void onFrameDisplayed(double videoPts);

    // Called from audio output thread when a frame starts playing.
    void updateAudioClock(double pts);

    void setSpeed(double speed);
    double speed() const;

    void reset();
    void setSyncMode(SyncMode mode);

signals:
    void speedChanged(double speed);

private:
    mutable QMutex m_mutex;
    SyncMode m_syncMode{SyncMode::AudioMaster};

    double m_audioClock{0.0};      // pts (s) of audio currently playing
    double m_frameLastPts{0.0};    // pts (s) of last displayed video frame
    double m_frameLastDelay{0.04}; // last frame interval (s), for fallback
    double m_speed{1.0};
    bool m_firstFrame{true};
};
