#pragma once

#include <QSettings>
#include <QVariantMap>

// 集中管理用户偏好设置，基于 QSettings 实现持久化。
// 单例模式 — 通过 instance() 全局访问。
class SettingsManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap subtitleStyle READ subtitleStyle WRITE setSubtitleStyle NOTIFY subtitleStyleChanged)
    Q_PROPERTY(QString screenshotPath READ screenshotPath WRITE setScreenshotPath NOTIFY screenshotPathChanged)
    Q_PROPERTY(QVariantMap shortcuts READ shortcuts WRITE setShortcuts NOTIFY shortcutsChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    // 画面调节（全局持久化）：亮度/对比度/饱和度 -100~100（0=原始），缩放模式 0=Fit 1=Fill 2=Stretch
    Q_PROPERTY(int brightness READ brightness WRITE setBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(int contrast READ contrast WRITE setContrast NOTIFY contrastChanged)
    Q_PROPERTY(int saturation READ saturation WRITE setSaturation NOTIFY saturationChanged)
    Q_PROPERTY(int scaleMode READ scaleMode WRITE setScaleMode NOTIFY scaleModeChanged)

public:
    static SettingsManager &instance();

    QVariantMap subtitleStyle() const;
    void setSubtitleStyle(const QVariantMap &style);

    QString screenshotPath() const;
    void setScreenshotPath(const QString &path);

    QVariantMap shortcuts() const;
    void setShortcuts(const QVariantMap &shortcuts);

    QString language() const;
    void setLanguage(const QString &lang);

    int brightness() const;
    void setBrightness(int value);
    int contrast() const;
    void setContrast(int value);
    int saturation() const;
    void setSaturation(int value);
    int scaleMode() const;
    void setScaleMode(int mode);

signals:
    void subtitleStyleChanged(const QVariantMap &style);
    void screenshotPathChanged(const QString &path);
    void shortcutsChanged(const QVariantMap &shortcuts);
    void languageChanged(const QString &lang);
    void brightnessChanged(int value);
    void contrastChanged(int value);
    void saturationChanged(int value);
    void scaleModeChanged(int mode);
    void settingsChanged();

private:
    SettingsManager(QObject *parent = nullptr);
    ~SettingsManager() override = default;
    Q_DISABLE_COPY_MOVE(SettingsManager)

    void load();

    QSettings m_settings;

    QVariantMap m_subtitleStyle;
    QString m_screenshotPath;
    QVariantMap m_shortcuts;
    QString m_language;
    int m_brightness{0};
    int m_contrast{0};
    int m_saturation{0};
    int m_scaleMode{0};
};

