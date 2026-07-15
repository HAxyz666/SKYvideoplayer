#include "AVSyncController.h"
#include <QtGlobal>

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

    // 如果音频时钟超过 1 秒没更新（网络流缓冲中），跳过音视频同步，
    // 直接用帧间间隔驱动，避免视频因等待音频而极度变慢。
    qint64 now = av_gettime();
    bool audioClockStale = (now - m_lastAudioClockUpdateUs) > 1000000; // 1 秒

    if (!audioClockStale) {
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

    if (m_speed > 0.0) {
        // 使用音频实际推进速率替代固定倍速。
        // 过渡期间 m_actualAudioRate 在旧速和新速之间渐变，
        // 视频跟随此实际速率，实现平滑变速。
        double effectiveRate = qMax(m_actualAudioRate, 0.1);
        delay /= effectiveRate;
    }

    if (delay > 0.5)
        delay = 0.5;
    if (delay < 0.005)
        delay = 0.005;

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
    qint64 now = av_gettime();

    // 测量音频实际推进速率（content秒 / 墙钟秒）。
    // 只在回调间隔 >10ms 时更新，跳过同一次 SDL 回调内的批量帧（wallDelta≈0）。
    // 使用快速响应的指数平滑，使过渡期间视频迅速跟随音频实际速率。
    if (m_lastAudioClockUpdateUs > 0) {
        double wallDelta = static_cast<double>(now - m_lastAudioClockUpdateUs) / 1e6;
        if (wallDelta > 0.01) {
            double clockDelta = pts - m_lastAudioClockValue;
            if (clockDelta > 0.0 && clockDelta < 5.0) {
                double instantRate = clockDelta / wallDelta;
                instantRate = qBound(0.1, instantRate, 4.0);
                // 快速响应：80% 新测量 + 20% 旧值，约2-3次回调（~100ms）内收敛
                m_actualAudioRate = m_actualAudioRate * 0.2 + instantRate * 0.8;
            }
            m_lastAudioClockValue = pts;
            m_lastAudioClockUpdateUs = now;
        }
    } else {
        m_lastAudioClockValue = pts;
        m_lastAudioClockUpdateUs = now;
    }

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
        m_actualAudioRate = m_speed; // 过渡期从旧速度开始渐变
        m_speed = speed;
    }
}

void AVSyncController::reset()
{
    QMutexLocker lock(&m_mutex);
    m_audioClock = 0.0;
    m_lastAudioClockUpdateUs = 0;
    m_lastAudioClockValue = 0.0;
    m_actualAudioRate = 1.0;
    m_frameLastPts = 0.0;
    m_frameLastDelay = 0.04;
    m_firstFrame = true;
}
