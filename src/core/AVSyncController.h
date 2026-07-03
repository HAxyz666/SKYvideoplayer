#pragma once

#include <QObject>
#include <QMutex>

enum class SyncMode {
    AudioMaster,
    VideoMaster
};

class AVSyncController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)

public:
    explicit AVSyncController(QObject *parent = nullptr);

    double computeFrameDelay(double videoPts) const;
    void onFrameDisplayed(double videoPts);
    void updateAudioClock(double pts);
    double audioClock() const;

    void setSpeed(double speed);
    double speed() const;

    void reset();
    void setSyncMode(SyncMode mode);
    SyncMode syncMode() const;

signals:
    void speedChanged(double speed);

private:
    mutable QMutex m_mutex;
    SyncMode m_syncMode{SyncMode::AudioMaster};

    double m_audioClock{0.0};
    mutable double m_frameLastPts{0.0};
    mutable double m_frameLastDelay{0.04};
    double m_speed{1.0};
    bool m_firstFrame{true};
};
