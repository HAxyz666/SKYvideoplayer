#pragma once

#include <QSettings>
#include <QVariantMap>

// 集中管理用户偏好设置，基于 QSettings 实现持久化。
// 单例模式 — 通过 instance() 全局访问。
class SettingsManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap subtitleStyle READ subtitleStyle WRITE setSubtitleStyle NOTIFY subtitleStyleChanged)

public:
    static SettingsManager &instance();

    QVariantMap subtitleStyle() const;
    void setSubtitleStyle(const QVariantMap &style);

signals:
    void subtitleStyleChanged(const QVariantMap &style);
    void settingsChanged();

private:
    SettingsManager(QObject *parent = nullptr);
    ~SettingsManager() override = default;
    Q_DISABLE_COPY_MOVE(SettingsManager)

    void load();

    QSettings m_settings;

    QVariantMap m_subtitleStyle;
};

