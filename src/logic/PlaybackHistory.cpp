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

bool PlaybackHistory::shouldResume(const QString &filePath, double duration) const
{
    double pos = getPosition(filePath);
    if (pos < 5.0)
        return false;
    if (duration > 0 && pos >= duration * 0.95)
        return false;
    return true;
}

void PlaybackHistory::removeEntry(const QString &filePath)
{
    if (m_progressMap.remove(filePath) > 0)
        saveToSettings();
}

void PlaybackHistory::clearAll()
{
    if (m_progressMap.isEmpty())
        return;
    m_progressMap.clear();
    saveToSettings();
}

void PlaybackHistory::loadFromSettings()
{
    m_progressMap.clear();
    m_settings.beginGroup("PlaybackHistory");
    int count = m_settings.value("count", 0).toInt();
    for (int i = 0; i < count; ++i) {
        QString prefix = QStringLiteral("item%1_").arg(i);
        QString path = m_settings.value(prefix + "path").toString();
        double pos = m_settings.value(prefix + "position", 0.0).toDouble();
        if (!path.isEmpty() && pos > 0.0)
            m_progressMap.insert(path, pos);
    }
    m_settings.endGroup();
}

void PlaybackHistory::saveToSettings()
{
    m_settings.beginGroup("PlaybackHistory");
    m_settings.remove(""); // 清除整个组

    QStringList keys = m_progressMap.keys();
    int count = qMin(keys.size(), kMaxEntries);
    m_settings.setValue("count", count);

    for (int i = 0; i < count; ++i) {
        QString prefix = QStringLiteral("item%1_").arg(i);
        m_settings.setValue(prefix + "path", keys.at(i));
        m_settings.setValue(prefix + "position", m_progressMap.value(keys.at(i)));
    }
    m_settings.endGroup();
}
