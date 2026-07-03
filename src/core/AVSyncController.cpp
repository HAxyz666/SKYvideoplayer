#include "AVSyncController.h"
#include <QtGlobal>
#include <cmath>

AVSyncController::AVSyncController(QObject *parent)
    : QObject(parent)
{
}

double AVSyncController::computeFrameDelay(double videoPts) const
{
    QMutexLocker lock(&m_mutex);

    if (m_firstFrame)
        return 0.0;

    double delay = videoPts - m_frameLastPts;
    if (delay <= 0.0 || delay > 1.0) {
        delay = m_frameLastDelay;
    }

    if (m_syncMode == SyncMode::AudioMaster) {
        double diff = videoPts - m_audioClock;
        double syncThreshold = 0.040;

        if (qAbs(diff) > 1.0) {
            m_frameLastPts = videoPts;
            m_frameLastDelay = 0.04;
            return 0.0;
        }

        if (qAbs(diff) > syncThreshold) {
            if (diff < 0.0) {
                delay = 0.0;
            } else {
                delay = diff;
            }
        }
    }

    if (m_speed > 0.0)
        delay /= m_speed;

    if (delay > 0.5)
        delay = 0.5;

    return delay;
}

void AVSyncController::onFrameDisplayed(double videoPts)
{
    QMutexLocker lock(&m_mutex);

    if (m_firstFrame) {
        m_frameLastPts = videoPts;
        m_frameLastDelay = 0.04;
        m_firstFrame = false;
        return;
    }

    double interval = videoPts - m_frameLastPts;
    if (interval > 0.0 && interval <= 1.0)
        m_frameLastDelay = interval;

    m_frameLastPts = videoPts;
}

void AVSyncController::updateAudioClock(double pts)
{
    QMutexLocker lock(&m_mutex);
    m_audioClock = pts;
}

double AVSyncController::audioClock() const
{
    QMutexLocker lock(&m_mutex);
    return m_audioClock;
}

void AVSyncController::setSpeed(double speed)
{
    {
        QMutexLocker lock(&m_mutex);
        if (qFuzzyCompare(m_speed, speed))
            return;
        m_speed = speed;
    }
    emit speedChanged(speed);
}

double AVSyncController::speed() const
{
    QMutexLocker lock(&m_mutex);
    return m_speed;
}

void AVSyncController::reset()
{
    QMutexLocker lock(&m_mutex);
    m_audioClock = 0.0;
    m_frameLastPts = 0.0;
    m_frameLastDelay = 0.04;
    m_firstFrame = true;
}

void AVSyncController::setSyncMode(SyncMode mode)
{
    QMutexLocker lock(&m_mutex);
    m_syncMode = mode;
}

SyncMode AVSyncController::syncMode() const
{
    QMutexLocker lock(&m_mutex);
    return m_syncMode;
}
