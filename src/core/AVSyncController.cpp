#include "AVSyncController.h"
#include <QtGlobal>
#include <cmath>

AVSyncController::AVSyncController(QObject *parent)
    : QObject(parent)
    , m_syncMode(SyncMode::AudioMaster)
    , m_audioClock(0)
    , m_videoClock(0)
    , m_frameTimer(0)
    , m_frameLastPts(0)
    , m_frameLastDelay(0)
    , m_speed(1.0)
    , m_audioDiffThreshold(0.1)
    , m_audioDiffAvgCoef(0.03)
    , m_audioDiffAvgCount(0)
{
}

double AVSyncController::computeFrameDelay(double videoPts)
{
    updateVideoClock(videoPts);
    return synchronizeVideo();
}

void AVSyncController::updateAudioClock(double pts)
{
    m_audioClock = pts;
}

void AVSyncController::updateVideoClock(double pts)
{
    m_videoClock = pts;
}

double AVSyncController::getAudioClock() const
{
    return m_audioClock;
}

double AVSyncController::getVideoClock() const
{
    return m_videoClock;
}

void AVSyncController::setSpeed(double speed)
{
    m_speed = speed;
}

void AVSyncController::reset()
{
    m_audioClock = 0;
    m_videoClock = 0;
    m_frameTimer = 0;
    m_frameLastPts = 0;
    m_frameLastDelay = 0;
    m_speed = 1.0;
    m_audioDiffAvgCount = 0;
}

void AVSyncController::setSyncMode(SyncMode mode)
{
    m_syncMode = mode;
}

double AVSyncController::synchronizeVideo()
{
    double frameDelay = m_videoClock - m_frameLastPts;
    m_frameLastPts = m_videoClock;

    if (frameDelay <= 0.0 || frameDelay >= 1.0)
        frameDelay = 0.04;

    m_frameLastDelay = frameDelay;
    m_frameTimer = m_videoClock;

    if (m_syncMode == SyncMode::AudioMaster) {
        double diff = m_videoClock - m_audioClock;
        if (std::fabs(diff) > m_audioDiffThreshold) {
            frameDelay -= diff;
        }
        m_audioDiffAvgCount++;
        if (m_audioDiffAvgCount > 0) {
            double avgDiff = m_audioDiffAvgCoef * diff;
            frameDelay -= avgDiff;
        }
    }

    if (frameDelay < 0.0)
        frameDelay = 0.0;

    return frameDelay;
}

int AVSyncController::synchronizeAudio()
{
    double diff = m_audioClock - m_videoClock;

    if (std::fabs(diff) > m_audioDiffThreshold) {
        if (diff > 0) {
            return -static_cast<int>(diff * 44100);
        } else {
            return static_cast<int>(-diff * 44100);
        }
    }
    return 0;
}

