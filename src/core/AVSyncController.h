#pragma once

#include <QObject>
#include <QMutex>

class AVSyncController : public QObject
{
    Q_OBJECT
public:
    explicit AVSyncController(QObject *parent = nullptr);

    double computeFrameDelay(double videoPts) const;
    void onFrameDisplayed(double videoPts);
    void updateAudioClock(double pts);
    double audioClock() const;

    void setSpeed(double speed);

    void reset();

signals:
    void speedChanged(double speed);

private:
    mutable QMutex m_mutex;
    double m_audioClock{0.0};
    mutable double m_frameLastPts{0.0};
    mutable double m_frameLastDelay{0.04};
    double m_speed{1.0};
    bool m_firstFrame{true};
};
