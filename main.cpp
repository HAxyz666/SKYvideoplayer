#include "VideoRenderItem.h"
#include "MediaEngine.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    engine.loadFromModule("SKYvideoplayer", "Main");

    auto rootObjects = engine.rootObjects();
    if (!rootObjects.isEmpty()) {
        VideoRenderItem *display = rootObjects.first()->findChild<VideoRenderItem *>("videoRenderItem");
        if (display) {
            QString videoFile = "/root/baogao.mkv";
            MediaEngine *player = new MediaEngine(videoFile, &app);
            QObject::connect(player, &MediaEngine::frameReady, display, &VideoRenderItem::setImage);
            QObject::connect(player, &MediaEngine::playbackFinished, &app, []() {
                qDebug() << "Playback finished";
            });
            player->start();
        }
    }

    return app.exec();
}
