#include "MediaEngine.h"
#include "VideoRenderItem.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    MediaEngine player;
    engine.rootContext()->setContextProperty("mediaEngine", &player);

    engine.loadFromModule("SKYvideoplayer", "Main");

    auto rootObjects = engine.rootObjects();
    if (!rootObjects.isEmpty()) {
        VideoRenderItem *display = rootObjects.first()->findChild<VideoRenderItem *>("videoRenderItem");
        if (display)
            QObject::connect(&player, &MediaEngine::frameReady, display, &VideoRenderItem::setImage);
    }

    return app.exec();
}
