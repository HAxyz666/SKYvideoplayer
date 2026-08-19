#include "PlaybackHistory.h"

PlaybackHistory &PlaybackHistory::instance()
{
    static PlaybackHistory inst;
    return inst;
}

PlaybackHistory::PlaybackHistory(QObject *parent)
    : QObject(parent)
{
    loadFromSettings();
}

void PlaybackHistory::savePosition(const QString &filePath, double position, double duration)
{
    if (filePath.isEmpty())
        return;

    // 跳过开头和结尾
    if (position < 5.0)
        return;
    if (duration > 0 && position >= duration * 0.95)
        return;

    m_progressMap.insert(filePath, position);
    saveToSettings();
}

double PlaybackHistory::getPosition(const QString &filePath) const
{
    return m_progressMap.value(filePath, 0.0);
}

void PlaybackHistory::saveSubtitleDelay(const QString &filePath, qint64 delayMs)
{
    if (filePath.isEmpty())
        return;
    if (delayMs == 0)
        m_subtitleDelayMap.remove(filePath);
    else
        m_subtitleDelayMap.insert(filePath, delayMs);
    saveToSettings();
}

qint64 PlaybackHistory::getSubtitleDelay(const QString &filePath) const
{
    return m_subtitleDelayMap.value(filePath, 0);
}

void PlaybackHistory::savePrefs(const QString &filePath, const QVariantMap &prefs)
{
    if (filePath.isEmpty())
        return;
    if (prefs.isEmpty())
        m_prefsMap.remove(filePath);
    else
        m_prefsMap.insert(filePath, prefs);
    saveToSettings();
}

QVariantMap PlaybackHistory::prefs(const QString &filePath) const
{
    return m_prefsMap.value(filePath);
}

void PlaybackHistory::removeEntry(const QString &filePath)
{
    if (m_progressMap.remove(filePath) > 0)
        saveToSettings();
}

void PlaybackHistory::loadFromSettings()
{
    m_progressMap.clear();
    m_subtitleDelayMap.clear();
    m_prefsMap.clear();
    m_settings.beginGroup("PlaybackHistory");
    int count = m_settings.value("count", 0).toInt();
    for (int i = 0; i < count; ++i) {
        QString prefix = QStringLiteral("item%1_").arg(i);
        QString path = m_settings.value(prefix + "path").toString();
        double pos = m_settings.value(prefix + "position", 0.0).toDouble();
        qint64 delay = m_settings.value(prefix + "subtitleDelay", 0).toLongLong();
        QVariant prefs = m_settings.value(prefix + "prefs");
        if (!path.isEmpty() && pos > 0.0)
            m_progressMap.insert(path, pos);
        if (!path.isEmpty() && delay != 0)
            m_subtitleDelayMap.insert(path, delay);
        if (!path.isEmpty() && prefs.isValid() && !prefs.toMap().isEmpty())
            m_prefsMap.insert(path, prefs.toMap());
    }
    m_settings.endGroup();
}

void PlaybackHistory::saveToSettings()
{
    m_settings.beginGroup("PlaybackHistory");
    m_settings.remove(""); // 清除整个组

    // 进度 / 字幕延迟 / 播放偏好共享同一组，条目集合取并集，
    // 避免某文件只有延迟或偏好记录时被丢弃
    QStringList keys = m_progressMap.keys();
    for (const QString &path : m_subtitleDelayMap.keys()) {
        if (!keys.contains(path))
            keys.append(path);
    }
    for (const QString &path : m_prefsMap.keys()) {
        if (!keys.contains(path))
            keys.append(path);
    }
    int count = qMin(keys.size(), kMaxEntries);
    m_settings.setValue("count", count);

    for (int i = 0; i < count; ++i) {
        QString prefix = QStringLiteral("item%1_").arg(i);
        m_settings.setValue(prefix + "path", keys.at(i));
        m_settings.setValue(prefix + "position", m_progressMap.value(keys.at(i)));
        m_settings.setValue(prefix + "subtitleDelay", m_subtitleDelayMap.value(keys.at(i)));
        m_settings.setValue(prefix + "prefs", m_prefsMap.value(keys.at(i)));
    }
    m_settings.endGroup();
}
