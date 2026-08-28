#include "Applicationcontroller.h"
#include "SettingsManager.h"
#include "VideoRenderItem.h"
#include "PlaybackMode.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // 固定使用 Qt 自带 Basic 样式：否则系统会注入 KDE 的 org.kde.breeze
    // QQC2 风格（Arch 的 qqc2-desktop-style），控件尺寸按 Kirigami gridUnit
    // 计算，比 UI 里写死的像素尺寸大得多，弹窗内容会被裁切错位
    QQuickStyle::setStyle("Basic");

    app.setOrganizationName("SKYsoft");
    app.setApplicationName("SKYvideoplayer");
    app.setWindowIcon(QIcon(":/icons/logos/LogoD.png"));

    qRegisterMetaType<PlaybackMode>("PlaybackMode");
    qRegisterMetaType<YUVFrame>("YUVFrame");

    ApplicationController controller;
    QQmlApplicationEngine engine;
    controller.setEngine(&engine);
    engine.rootContext()->setContextProperty("appController", &controller);
    engine.rootContext()->setContextProperty("settingsManager", &SettingsManager::instance());

    engine.loadFromModule("SKYvideoplayer", "Main");
    controller.connectVideoDisplay();

    return app.exec();
}
