#include "AVSyncController.h"
#include <QtGlobal>
#include <cmath>

extern "C" {
#include <libavutil/time.h>
}

AVSyncController::AVSyncController(QObject *parent)
    : QObject(parent)
{
}

double AVSyncController::computeFrameDelay(double videoPts) const
{
    QMutexLocker lock(&m_mutex);

    // If this is the very first frame, display it immediately.
    if (m_firstFrame)
        return 0.0;

    double delay = videoPts - m_frameLastPts;
    if (delay <= 0.0 || delay > 1.0) {
        // pts discontinuity — fall back to last known interval
        delay = m_frameLastDelay;
    }

    // Audio-master sync correction.
    //
    // At fractional speeds (< 1.0×) we skip video-ahead correction
    // because the atempo filter lags by a constant pipeline offset.
    // At ≥ 1.0× the latency is negligible; full bidirectional sync is safe.
    //
    // Speed-change desync is prevented upstream in MediaEngine::setSpeed()
    // by delaying the video acceleration until the audio pipeline has
    // finished switching.
    if (m_syncMode == SyncMode::AudioMaster) {
        double diff = videoPts - m_audioClock;
        // ffplay 风格的固定阈值（一帧）。delay/m_speed 那种"按倍速缩放"
        // 的公式在 ≥1.0x 下会让阈值过紧，频繁触发 catch-up 把 video
        // 持续推迟，导致画面"不到倍速"。
        double syncThreshold = 0.040;

        // Audio clock jumped by more than 1 second — speed-change
        // transient (atempo rebuild / first-frame PTS glitch).
        // Reset frame tracking to re-sync from the current frame.
        if (std::fabs(diff) > 1.0) {
            m_frameLastPts = videoPts;
            m_frameLastDelay = 0.04;
            return 0.0;
        }

        if (std::fabs(diff) > syncThreshold) {
            if (diff < 0.0) {
                // video 落后于音频 → 立即显示以追上
                delay = 0.0;
            } else {
                // video 领先于音频 → 等到追上
                // 所有倍速下都做这个修正（之前 0.5x 不修正导致严重领先）
                delay = diff;
            }
        }
    }

    if (m_speed > 0.0)
        delay /= m_speed;

    // Safety cap: if A/V clocks diverge (e.g. audio underflow), don't
    // let delay grow unboundedly — that would freeze the picture for
    // seconds at a time.  500 ms is enough for any realistic catch-up.
    if (delay > 0.5)
        delay = 0.5;

    // DEBUG: log sync state every ~30 frames at non-1× speed
    {
        static int cnt = 0;
        if (!qFuzzyCompare(m_speed, 1.0) && (++cnt % 30 == 0)) {
            double d = videoPts - m_audioClock;
            qDebug("[sync] spd=%.2f vPts=%.4f aClk=%.4f diff=%+.4f | thr=%.4f raw=%.4f final=%.4f",
                   m_speed, videoPts, m_audioClock, d,
                   (m_speed > 0 ? (videoPts - m_frameLastPts) / m_speed : 0),
                   videoPts - m_frameLastPts, delay);
        }
    }

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
        m_speedChangeTimeUs = av_gettime();
    }
    emit speedChanged(speed);
}

double AVSyncController::speed() const
{
    QMutexLocker lock(&m_mutex);
    return m_speed;
}

double AVSyncController::audioClock() const
{
    QMutexLocker lock(&m_mutex);
    return m_audioClock;
}

void AVSyncController::reset()
{
    QMutexLocker lock(&m_mutex);
    m_audioClock = 0.0;
    m_frameLastPts = 0.0;
    m_frameLastDelay = 0.04;
    m_firstFrame = true;
}

void AVSyncController::resetFrameTracking()
{
    QMutexLocker lock(&m_mutex);
    m_frameLastPts = 0.0;
    m_frameLastDelay = 0.04;
    m_firstFrame = true;
    // m_audioClock stays as-is
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
