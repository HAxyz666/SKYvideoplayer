#include "AVSyncController.h"
#include <QtGlobal>
#include <QDebug>

AVSyncController::AVSyncController(QObject *parent)
    : QObject(parent)
{
}

double AVSyncController::computeSyncThreshold(double frameInterval) const
{
    return qBound(
        m_syncParams.thresholdMin,
        frameInterval * m_syncParams.thresholdScale,
        m_syncParams.thresholdMax
    );
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
    QMutexLocker lock(&m_mutex);
    // 过渡期从旧速度开始，由 updateAudioClock 的瞬时速率测量平滑收敛到新速度，
    // 使视频 delay 折算跟随音频实际推进速率，避免变速瞬间跳变。
    m_actualAudioRate = m_speed;
    m_speed = speed;
}

SyncResult AVSyncController::computeFrameSync(double videoPts) const
{
    QMutexLocker lock(&m_mutex);
    SyncResult result{SyncAction::ShowNormal, 0.04};

    if (m_firstFrame)
        return result;

    double frameInterval = videoPts - m_frameLastPts;
    if (frameInterval <= 0.0 || frameInterval > 1.0)
        frameInterval = m_frameLastDelay;

    // 自适应阈值
    double syncThreshold = computeSyncThreshold(frameInterval);
    double maxFrameDuration = 1.0;  // 异常抖动判定（秒）
    // 方案C：视频落后超过该阈值直接丢帧追赶，
    // 避免原先 delay=0 快进方式造成的过渡期卡顿/跳变感。
    double skipThreshold = qMax(2.0 * frameInterval, 0.020);

    // 如果音频时钟超过 1 秒没更新（纯视频流/缓冲中），跳过同步，
    // 直接按目标倍速驱动画面节奏。
    qint64 now = av_gettime();
    bool audioClockStale = (now - m_lastAudioClockUpdateUs) > 1000000;
    if (audioClockStale) {
        result.delay = frameInterval / qMax(m_speed, 0.1);
        return result;
    }

    double diff = videoPts - m_audioClock;  // 正=视频超前，负=视频落后

    if (qAbs(diff) > maxFrameDuration) {
        // 异常差值，重置帧间统计
        m_frameLastPts = videoPts;
        m_frameLastDelay = 0.04;
        if (diff < 0.0) {
            // 视频远远落后：丢帧追赶，不显示重定位点之前的旧帧
            result.action = SyncAction::SkipFrame;
            result.delay = 0;
        }
        return result;
    }

    if (qAbs(diff) <= syncThreshold) {
        // 已同步：正常显示，delay = 帧间隔
        result.action = SyncAction::ShowNormal;
        result.delay = frameInterval;
    }
    else if (diff < -syncThreshold) {
        // 视频落后
        if (-diff > skipThreshold) {
            // 落后超过阈值：丢弃当前帧追赶（快速对齐，避免快进画面）
            result.action = SyncAction::SkipFrame;
            result.delay = 0;
        } else {
            // 轻微落后：立即显示（delay≈0），但不丢帧
            double newDelay = qMax(0.0, frameInterval + diff);
            result.action = (newDelay <= 0.001) ? SyncAction::DelayFrame : SyncAction::ShowNormal;
            result.delay = newDelay;
        }
    }
    else if (diff > syncThreshold) {
        // 视频超前：等待音频
        if (frameInterval > 0.040) {
            // 上一帧间隔已很长：微调
            result.action = SyncAction::DelayFrame;
            result.delay = frameInterval + diff;
        } else {
            // 正常超前：翻倍delay让视频放慢；限制单次时长，避免异常时画面长时间定格
            result.action = SyncAction::RepeatFrame;
            result.delay = qMin(frameInterval * 2.0, 0.100);
        }
    }

    // 变速补偿
    if (m_speed > 0.0) {
        result.delay /= qMax(m_actualAudioRate, 0.1);
    }
    result.delay = qBound(0.002, result.delay, 0.5);

    return result;
}

void AVSyncController::updateFrameRate(double fps)
{
    QMutexLocker lock(&m_mutex);
    if (fps > 0.0 && fps < 240.0) {
        // 根据帧率动态调整阈值缩放系数
        // 高帧率使用更小的缩放，低帧率使用更大的缩放
        if (fps >= 60.0) {
            m_syncParams.thresholdScale = 0.05;
        } else if (fps >= 30.0) {
            m_syncParams.thresholdScale = 0.1;
        } else {
            m_syncParams.thresholdScale = 0.15;
        }
    }
}

void AVSyncController::recordSyncEvent(SyncAction action, double diff)
{
    QMutexLocker lock(&m_mutex);

    m_syncStats.currentDiff = diff;

    // 首次更新或重置后
    if (m_syncStats.lastResetTimeUs == 0) {
        m_syncStats.minDiff = diff;
        m_syncStats.maxDiff = diff;
        m_syncStats.avgDiff = diff;
        m_syncStats.lastResetTimeUs = av_gettime();
        return;
    }

    // 更新极值
    if (diff < m_syncStats.minDiff) m_syncStats.minDiff = diff;
    if (diff > m_syncStats.maxDiff) m_syncStats.maxDiff = diff;

    // 指数平滑平均
    m_syncStats.avgDiff = 0.95 * m_syncStats.avgDiff + 0.05 * diff;

    // 统计动作
    switch (action) {
    case SyncAction::SkipFrame: m_syncStats.skipCount++; break;
    case SyncAction::RepeatFrame: m_syncStats.repeatCount++; break;
    default: break;
    }

    // 每60秒输出一次诊断日志
    qint64 now = av_gettime();
    if (now - m_syncStats.lastResetTimeUs > 60000000) {
        qDebug("[Sync] A-V: cur=%.1fms min=%.1fms max=%.1fms avg=%.1fms skip=%d repeat=%d",
               diff * 1000, m_syncStats.minDiff * 1000, m_syncStats.maxDiff * 1000,
               m_syncStats.avgDiff * 1000, m_syncStats.skipCount, m_syncStats.repeatCount);
        resetStats();
    }
}

void AVSyncController::resetStats()
{
    // 注意：调用者需要已持有锁或在锁外调用
    m_syncStats = SyncStats{};
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
    m_syncStats = SyncStats{};
}
