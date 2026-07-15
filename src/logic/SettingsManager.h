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

signals:
    void subtitleStyleChanged(const QVariantMap &style);
    void screenshotPathChanged(const QString &path);
    void shortcutsChanged(const QVariantMap &shortcuts);
    void languageChanged(const QString &lang);
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
};

