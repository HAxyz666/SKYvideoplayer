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

    if (m_settings.contains("shortcuts")) {
        m_shortcuts = m_settings.value("shortcuts").toMap();
    } else {
        m_shortcuts = {
            { QStringLiteral("volumeUp"), QStringLiteral("Up") },
            { QStringLiteral("volumeDown"), QStringLiteral("Down") },
            { QStringLiteral("toggleMute"), QStringLiteral("M") },
            { QStringLiteral("togglePlayback"), QStringLiteral("Space") },
            { QStringLiteral("stepBackward"), QStringLiteral("Left") },
            { QStringLiteral("stepForward"), QStringLiteral("Right") },
            { QStringLiteral("stepBackwardLarge"), QStringLiteral("Ctrl+Left") },
            { QStringLiteral("stepForwardLarge"), QStringLiteral("Ctrl+Right") },
            { QStringLiteral("toggleFullscreen"), QStringLiteral("F11") },
            { QStringLiteral("exitFullscreen"), QStringLiteral("Escape") }
        };
    }

    m_language = m_settings.value("language", QStringLiteral("en")).toString();

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

QVariantMap SettingsManager::shortcuts() const
{
    return m_shortcuts;
}

void SettingsManager::setShortcuts(const QVariantMap &shortcuts)
{
    if (m_shortcuts == shortcuts)
        return;

    m_shortcuts = shortcuts;
    m_settings.beginGroup("SettingsManager");
    m_settings.setValue("shortcuts", m_shortcuts);
    m_settings.endGroup();

    emit shortcutsChanged(m_shortcuts);
    emit settingsChanged();
}

QString SettingsManager::language() const
{
    return m_language;
}

void SettingsManager::setLanguage(const QString &lang)
{
    if (m_language == lang)
        return;

    m_language = lang;
    m_settings.beginGroup("SettingsManager");
    m_settings.setValue("language", m_language);
    m_settings.endGroup();

    emit languageChanged(m_language);
    emit settingsChanged();
}
