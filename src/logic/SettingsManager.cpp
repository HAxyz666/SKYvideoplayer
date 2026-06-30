#include "SettingsManager.h"

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

    m_muted = m_settings.value("muted", false).toBool();
    m_theme = m_settings.value("theme", QStringLiteral("system")).toString();
    m_autoLoadSubtitle = m_settings.value("autoLoadSubtitle", true).toBool();

    // 字幕样式默认值: 字体/颜色/位置
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

    m_settings.endGroup();
}

bool SettingsManager::muted() const
{
    return m_muted;
}

void SettingsManager::setMuted(bool muted)
{
    if (m_muted == muted)
        return;

    m_muted = muted;
    m_settings.beginGroup("SettingsManager");
    m_settings.setValue("muted", m_muted);
    m_settings.endGroup();

    emit mutedChanged(m_muted);
    emit settingsChanged();
}

QString SettingsManager::theme() const
{
    return m_theme;
}

void SettingsManager::setTheme(const QString &theme)
{
    if (m_theme == theme)
        return;

    m_theme = theme;
    m_settings.beginGroup("SettingsManager");
    m_settings.setValue("theme", m_theme);
    m_settings.endGroup();

    emit themeChanged(m_theme);
    emit settingsChanged();
}

bool SettingsManager::autoLoadSubtitle() const
{
    return m_autoLoadSubtitle;
}

void SettingsManager::setAutoLoadSubtitle(bool autoLoad)
{
    if (m_autoLoadSubtitle == autoLoad)
        return;

    m_autoLoadSubtitle = autoLoad;
    m_settings.beginGroup("SettingsManager");
    m_settings.setValue("autoLoadSubtitle", m_autoLoadSubtitle);
    m_settings.endGroup();

    emit autoLoadSubtitleChanged(m_autoLoadSubtitle);
    emit settingsChanged();
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

void SettingsManager::sync()
{
    m_settings.sync();
}
