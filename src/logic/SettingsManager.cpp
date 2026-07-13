#include "SettingsManager.h"

#include <QDir>

SettingsManager &SettingsManager::instance()
{
    static SettingsManager inst;
    return inst;
}

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
    load();
}

void SettingsManager::load()
{
    m_settings.beginGroup("SettingsManager");

    if (m_settings.contains("subtitleStyle")) {
        m_subtitleStyle = m_settings.value("subtitleStyle").toMap();
    } else {
        m_subtitleStyle = {
            { QStringLiteral("fontFamily"), QStringLiteral("Sans Serif") },
            { QStringLiteral("fontSize"),   18 },
            { QStringLiteral("color"),      QStringLiteral("#FFFFFF") },
            { QStringLiteral("position"),   QStringLiteral("bottom") }
        };
    }

    m_screenshotPath = m_settings.value("screenshotPath",
        QDir::homePath()
    ).toString();

    m_settings.endGroup();
}

QVariantMap SettingsManager::subtitleStyle() const
{
    return m_subtitleStyle;
}

void SettingsManager::setSubtitleStyle(const QVariantMap &style)
{
    if (m_subtitleStyle == style)
        return;

    m_subtitleStyle = style;
    m_settings.beginGroup("SettingsManager");
    m_settings.setValue("subtitleStyle", m_subtitleStyle);
    m_settings.endGroup();

    emit subtitleStyleChanged(m_subtitleStyle);
    emit settingsChanged();
}

QString SettingsManager::screenshotPath() const
{
    return m_screenshotPath;
}

void SettingsManager::setScreenshotPath(const QString &path)
{
    if (m_screenshotPath == path)
        return;

    m_screenshotPath = path;
    m_settings.beginGroup("SettingsManager");
    m_settings.setValue("screenshotPath", m_screenshotPath);
    m_settings.endGroup();

    emit screenshotPathChanged(m_screenshotPath);
    emit settingsChanged();
}
