#pragma once

#include <QObject>
#include <QSettings>
#include <QMap>
#include <QVariantMap>

// 记录并持久化每个文件的播放位置，用于实现"记住播放进度"功能。
// 单例模式 — 通过 instance() 全局访问。
class PlaybackHistory : public QObject
{
    Q_OBJECT

public:
    static PlaybackHistory &instance();

    // 保存播放位置（秒），同时校验 duration 避免保存结尾位置
    void savePosition(const QString &filePath, double position, double duration);

    // 读取上次播放位置（秒），文件无记录时返回 0
    double getPosition(const QString &filePath) const;

    // 保存/读取每个文件的字幕延迟（毫秒，正值 = 字幕推迟出现）
    void saveSubtitleDelay(const QString &filePath, qint64 delayMs);
    qint64 getSubtitleDelay(const QString &filePath) const;

    // 保存/读取每个文件的播放偏好（音轨/字幕轨/倍速/旋转/翻转等）。
    // 支持键：audioStream(int, 音轨列表索引)、subtitleStream(int, -1=关闭)、
    // speed(double)、rotation(int, 0/90/180/270)、flipVertical(bool)。
    void savePrefs(const QString &filePath, const QVariantMap &prefs);
    QVariantMap prefs(const QString &filePath) const;

    void removeEntry(const QString &filePath);

    void loadFromSettings();
    void saveToSettings();

private:
    explicit PlaybackHistory(QObject *parent = nullptr);
    ~PlaybackHistory() override = default;
    Q_DISABLE_COPY_MOVE(PlaybackHistory)

    QSettings m_settings;
    QMap<QString, double> m_progressMap; // filePath -> position (seconds)
    QMap<QString, qint64> m_subtitleDelayMap; // filePath -> 字幕延迟 (ms)
    QMap<QString, QVariantMap> m_prefsMap; // filePath -> 播放偏好
    static constexpr int kMaxEntries = 200;
};
