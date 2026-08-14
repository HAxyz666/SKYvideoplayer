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
            { QStringLiteral("exitFullscreen"), QStringLiteral("Escape") },
            { QStringLiteral("subtitleDelayBackward"), QStringLiteral("[") },
            { QStringLiteral("subtitleDelayForward"), QStringLiteral("]") },
            { QStringLiteral("brightnessDown"), QStringLiteral("3") },
            { QStringLiteral("brightnessUp"), QStringLiteral("4") },
            { QStringLiteral("contrastDown"), QStringLiteral("5") },
            { QStringLiteral("contrastUp"), QStringLiteral("6") },
            { QStringLiteral("saturationDown"), QStringLiteral("7") },
            { QStringLiteral("saturationUp"), QStringLiteral("8") },
            { QStringLiteral("cycleAspectMode"), QStringLiteral("Z") }
        };
    }

    m_language = m_settings.value("language", QStringLiteral("en")).toString();

    m_brightness = qBound(-100, m_settings.value("brightness", 0).toInt(), 100);
    m_contrast = qBound(-100, m_settings.value("contrast", 0).toInt(), 100);
    m_saturation = qBound(-100, m_settings.value("saturation", 0).toInt(), 100);
    m_scaleMode = qBound(0, m_settings.value("scaleMode", 0).toInt(), 2);

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

int SettingsManager::brightness() const { return m_brightness; }
int SettingsManager::contrast() const { return m_contrast; }
int SettingsManager::saturation() const { return m_saturation; }
int SettingsManager::scaleMode() const { return m_scaleMode; }

void SettingsManager::setBrightness(int value)
{
    int v = qBound(-100, value, 100);
    if (m_brightness == v) return;
    m_brightness = v;
    m_settings.beginGroup("SettingsManager");
    m_settings.setValue("brightness", m_brightness);
    m_settings.endGroup();
    emit brightnessChanged(m_brightness);
    emit settingsChanged();
}

void SettingsManager::setContrast(int value)
{
    int v = qBound(-100, value, 100);
    if (m_contrast == v) return;
    m_contrast = v;
    m_settings.beginGroup("SettingsManager");
    m_settings.setValue("contrast", m_contrast);
    m_settings.endGroup();
    emit contrastChanged(m_contrast);
    emit settingsChanged();
}

void SettingsManager::setSaturation(int value)
{
    int v = qBound(-100, value, 100);
    if (m_saturation == v) return;
    m_saturation = v;
    m_settings.beginGroup("SettingsManager");
    m_settings.setValue("saturation", m_saturation);
    m_settings.endGroup();
    emit saturationChanged(m_saturation);
    emit settingsChanged();
}

void SettingsManager::setScaleMode(int mode)
{
    int m = qBound(0, mode, 2);
    if (m_scaleMode == m) return;
    m_scaleMode = m;
    m_settings.beginGroup("SettingsManager");
    m_settings.setValue("scaleMode", m_scaleMode);
    m_settings.endGroup();
    emit scaleModeChanged(m_scaleMode);
    emit settingsChanged();
}
