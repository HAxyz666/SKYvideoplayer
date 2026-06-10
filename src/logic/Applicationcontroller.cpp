#include "Applicationcontroller.h"
#include "MediaEngine.h"

#include <QDebug>

ApplicationController::ApplicationController(QObject *parent)
    : QObject(parent)
    , m_mediaEngine(new MediaEngine(this))
{
    connect(m_mediaEngine, &MediaEngine::pausedChanged, this, [this](bool paused) {
        emit playbackStateChanged(!paused);
    });
}

bool ApplicationController::openFile()
{
    emit requestOpenFile();
    return true;
}

bool ApplicationController::loadFile(const QString &path)
{
    QString filePath = path;
    if (filePath.startsWith("file://"))
        filePath = filePath.mid(7);

    m_mediaEngine->open(filePath);
    emit playbackStateChanged(true);
    return true;
}

void ApplicationController::togglePlayback()
{
    m_mediaEngine->togglePause();
}

MediaEngine *ApplicationController::mediaEngine() const
{
    return m_mediaEngine;
}
