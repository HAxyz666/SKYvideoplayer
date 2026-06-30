#pragma once

#include <QSettings>
#include <QVariantMap>

// 集中管理用户偏好设置，基于 QSettings 实现持久化。
// 单例模式 — 通过 instance() 全局访问。
class SettingsManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool autoLoadSubtitle READ autoLoadSubtitle WRITE setAutoLoadSubtitle NOTIFY autoLoadSubtitleChanged)
    Q_PROPERTY(QVariantMap subtitleStyle READ subtitleStyle WRITE setSubtitleStyle NOTIFY subtitleStyleChanged)

public:
    static SettingsManager &instance();

    bool muted() const;
    void setMuted(bool muted);

    QString theme() const;
    void setTheme(const QString &theme);

    bool autoLoadSubtitle() const;
    void setAutoLoadSubtitle(bool autoLoad);

    QVariantMap subtitleStyle() const;
    void setSubtitleStyle(const QVariantMap &style);

    void sync();

signals:
    void mutedChanged(bool muted);
    void themeChanged(const QString &theme);
    void autoLoadSubtitleChanged(bool autoLoad);
    void subtitleStyleChanged(const QVariantMap &style);
    void settingsChanged(); // 任意设置变化

private:
    SettingsManager(QObject *parent = nullptr);
    ~SettingsManager() override = default;
    Q_DISABLE_COPY_MOVE(SettingsManager)

    void load();

    QSettings m_settings;

    bool m_muted = false;
    QString m_theme = QStringLiteral("system");
    bool m_autoLoadSubtitle = true;
    QVariantMap m_subtitleStyle;
};

