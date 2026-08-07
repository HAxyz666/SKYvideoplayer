#pragma once

#include <QObject>
#include <QMutex>

extern "C" {
#include <libavutil/time.h>
}

// 自适应同步阈值参数
struct SyncParams {
    double thresholdMin{0.005};    // 最小阈值 5ms（高帧率）
    double thresholdMax{0.050};    // 最大阈值 50ms（低帧率）
    double thresholdScale{0.1};    // 阈值 = frameInterval * scale
};

// 同步动作类型
enum class SyncAction {
    ShowNormal,      // 正常显示当前帧
    RepeatFrame,     // 重复显示上一帧（等待音频）
    SkipFrame,       // 丢弃当前帧（追赶音频）
    DelayFrame       // 延迟显示（微调）
};

// 同步计算结果
struct SyncResult {
    SyncAction action;
    double delay;
};

// 同步统计信息
struct SyncStats {
    double currentDiff{0.0};      // 当前 A-V diff
    double minDiff{0.0};          // 最小diff（本次播放）
    double maxDiff{0.0};          // 最大diff
    double avgDiff{0.0};          // 平均diff（指数平滑）
    int skipCount{0};             // 丢帧次数
    int repeatCount{0};           // 帧重复次数
    qint64 lastResetTimeUs{0};    // 上次重置时间
};

class AVSyncController : public QObject
{
    Q_OBJECT
public:
    explicit AVSyncController(QObject *parent = nullptr);

    // 返回同步动作和延迟
    SyncResult computeFrameSync(double videoPts) const;

    void onFrameDisplayed(double videoPts);
    void updateAudioClock(double pts);
    // 直接设置主时钟（seek/切换音轨时使用）：只更新时钟值，不参与实际速率
    // 测量。若用 updateAudioClock 重设时钟，seek 间隙（首帧音频尚未入队，
    // 墙钟走了几百毫秒而时钟增量仅一个音频帧）会测得虚假的低速率，
    // 导致画面以极慢速度播放（卡顿/冻结）。
    void setClock(double pts);
    double audioClock() const;

    void setSpeed(double speed);

    // 帧率更新（由 displayLoop 调用）
    void updateFrameRate(double fps);

    // 统计相关
    void recordSyncEvent(SyncAction action, double diff);
    void resetStats();

    void reset();

private:
    // 计算自适应阈值
    double computeSyncThreshold(double frameInterval) const;

    mutable QMutex m_mutex;
    double m_audioClock{0.0};
    qint64 m_lastAudioClockUpdateUs{0};   // 上次音频时钟更新的墙钟时间
    double m_lastAudioClockValue{0.0};     // 上次音频时钟的 PTS 值
    double m_actualAudioRate{1.0};         // 音频实际推进速率（content秒/墙钟秒）
    mutable double m_frameLastPts{0.0};
    mutable double m_frameLastDelay{0.04};
    double m_speed{1.0};
    bool m_firstFrame{true};

    // 自适应阈值
    SyncParams m_syncParams;

    // 同步统计
    SyncStats m_syncStats;
};
