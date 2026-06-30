#include "AVSyncController.h"
#include <QtGlobal>

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

    // 如果是第一帧，立即显示
    if (m_firstFrame)
        return 0.0;

    double delay = videoPts - m_frameLastPts;
    if (delay <= 0.0 || delay > 1.0) {
        // pts 不连续——回退到上一个已知间隔
        delay = m_frameLastDelay;
    }

    // 音频主时钟同步修正
    //
    // 在低倍速（< 1.0×）下跳过视频超前修正，
    // 因为 atempo 滤镜有固定的管线偏移延迟。
    // 在 ≥ 1.0× 时延迟可忽略，双向同步是安全的。
    //
    // 速度变化导致的失同步由上游 MediaEngine::setSpeed()
    // 通过延迟视频加速直到音频管线完成切换来防止。
    if (m_syncMode == SyncMode::AudioMaster) {
        double diff = videoPts - m_audioClock;
        // ffplay 风格的固定阈值（一帧）。delay/m_speed 那种"按倍速缩放"
        // 的公式在 ≥1.0x 下会让阈值过紧，频繁触发 catch-up 把 video
        // 持续推迟，导致画面"不到倍速"。
        double syncThreshold = 0.040;

        // 音频时钟跳变超过 1 秒——速度变化瞬态
        // （atempo 重建/首帧 PTS 异常）。
        // 重置帧跟踪从当前帧重新同步。
        if (qAbs(diff) > 1.0) {
            m_frameLastPts = videoPts;
            m_frameLastDelay = 0.04;
            return 0.0;
        }

        if (qAbs(diff) > syncThreshold) {
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

    // 安全上限：如果音视频时钟偏离（如音频欠载），
    // 不让延迟无限增长——否则画面会冻结数秒。
    // 500 ms 足够任何实际的追赶。
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
    QMutexLocker lock(&m_mutex);
    if (qFuzzyCompare(m_speed, speed))
        return;
    m_speed = speed;
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
