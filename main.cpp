#include "Applicationcontroller.h"
#include "MediaEngine.h"
#include "VideoRenderItem.h"
#include "PlaybackMode.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("SKYsoft");
    app.setApplicationName("SKYvideoplayer");

    qRegisterMetaType<PlaybackMode>("PlaybackMode");

    ApplicationController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("appController", &controller);

    engine.loadFromModule("SKYvideoplayer", "Main");

    auto rootObjects = engine.rootObjects();
    if (!rootObjects.isEmpty()) {
        VideoRenderItem *display = rootObjects.first()->findChild<VideoRenderItem *>("videoRenderItem");
        if (display)
            QObject::connect(controller.mediaEngine(), &MediaEngine::frameReady,
                             display, &VideoRenderItem::setImage);
    }

    return app.exec();
}
