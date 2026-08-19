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
            { QStringLiteral("abLoop"), QStringLiteral("A") },
            { QStringLiteral("stepFrame"), QStringLiteral(".") },
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

// 通用 setter：值未变化则跳过；写入设置并发出对应信号
template <typename T, typename Signal>
void SettingsManager::setSetting(T &member, const T &value, const char *key, Signal changed)
{
    if (member == value)
        return;
    member = value;
    m_settings.beginGroup("SettingsManager");
    m_settings.setValue(key, member);
    m_settings.endGroup();
    (this->*changed)(member);
}

// 钳制到 [min,max] 的 int 设置：值未变化则跳过；写入设置并发出对应信号
void SettingsManager::setClampedInt(int &member, int value, int min, int max, const char *key,
                                    void (SettingsManager::*changed)(int))
{
    int v = qBound(min, value, max);
    if (member == v)
        return;
    member = v;
    m_settings.beginGroup("SettingsManager");
    m_settings.setValue(key, member);
    m_settings.endGroup();
    (this->*changed)(member);
}

QVariantMap SettingsManager::subtitleStyle() const
{
    return m_subtitleStyle;
}

void SettingsManager::setSubtitleStyle(const QVariantMap &style)
{
    setSetting(m_subtitleStyle, style, "subtitleStyle", &SettingsManager::subtitleStyleChanged);
}

QString SettingsManager::screenshotPath() const
{
    return m_screenshotPath;
}

void SettingsManager::setScreenshotPath(const QString &path)
{
    setSetting(m_screenshotPath, path, "screenshotPath", &SettingsManager::screenshotPathChanged);
}

QVariantMap SettingsManager::shortcuts() const
{
    return m_shortcuts;
}

void SettingsManager::setShortcuts(const QVariantMap &shortcuts)
{
    setSetting(m_shortcuts, shortcuts, "shortcuts", &SettingsManager::shortcutsChanged);
}

QString SettingsManager::language() const
{
    return m_language;
}

void SettingsManager::setLanguage(const QString &lang)
{
    setSetting(m_language, lang, "language", &SettingsManager::languageChanged);
}

int SettingsManager::brightness() const { return m_brightness; }
int SettingsManager::contrast() const { return m_contrast; }
int SettingsManager::saturation() const { return m_saturation; }
int SettingsManager::scaleMode() const { return m_scaleMode; }

void SettingsManager::setBrightness(int value)
{
    setClampedInt(m_brightness, value, -100, 100, "brightness", &SettingsManager::brightnessChanged);
}

void SettingsManager::setContrast(int value)
{
    setClampedInt(m_contrast, value, -100, 100, "contrast", &SettingsManager::contrastChanged);
}

void SettingsManager::setSaturation(int value)
{
    setClampedInt(m_saturation, value, -100, 100, "saturation", &SettingsManager::saturationChanged);
}

void SettingsManager::setScaleMode(int mode)
{
    setClampedInt(m_scaleMode, mode, 0, 2, "scaleMode", &SettingsManager::scaleModeChanged);
}
