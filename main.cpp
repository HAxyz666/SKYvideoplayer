#include "Applicationcontroller.h"
#include "MediaEngine.h"
#include "SettingsManager.h"
#include "VideoRenderItem.h"
#include "PlaybackMode.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("SKYsoft");
    app.setApplicationName("SKYvideoplayer");
    app.setWindowIcon(QIcon(":/icons/logos/LogoD.png"));

    qRegisterMetaType<PlaybackMode>("PlaybackMode");
    qRegisterMetaType<YUVFrame>("YUVFrame");

    ApplicationController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("appController", &controller);
    engine.rootContext()->setContextProperty("settingsManager", &SettingsManager::instance());

    engine.loadFromModule("SKYvideoplayer", "Main");

    auto rootObjects = engine.rootObjects();
    if (!rootObjects.isEmpty()) {
        VideoRenderItem *display = rootObjects.first()->findChild<VideoRenderItem *>("videoRenderItem");
        if (display) {
            QObject::connect(controller.mediaEngine(), &MediaEngine::frameReady,
                             display, &VideoRenderItem::setYUVFrame);
            controller.setVideoRenderItem(display);
        }
    }

    return app.exec();
}
