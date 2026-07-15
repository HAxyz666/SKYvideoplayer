#pragma once

#include <QObject>
#include <QSettings>
#include <QMap>

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

    // 判断是否值得恢复：position > 5s 且 < 95% duration
    bool shouldResume(const QString &filePath, double duration) const;

    void removeEntry(const QString &filePath);
    void clearAll();

    void loadFromSettings();
    void saveToSettings();

private:
    explicit PlaybackHistory(QObject *parent = nullptr);
    ~PlaybackHistory() override = default;
    Q_DISABLE_COPY_MOVE(PlaybackHistory)

    QSettings m_settings;
    QMap<QString, double> m_progressMap; // filePath -> position (seconds)
    static constexpr int kMaxEntries = 200;
};
