#include "Applicationcontroller.h"
#include "MediaEngine.h"
#include "PlaylistModel.h"

#include <QDebug>

ApplicationController::ApplicationController(QObject *parent)
    : QObject(parent)
    , m_mediaEngine(new MediaEngine(this))
    , m_playlistModel(new PlaylistModel(this))
{
    connect(m_mediaEngine, &MediaEngine::pausedChanged, this, [this](bool paused) {
        emit playbackStateChanged(!paused);
    });
    connect(m_mediaEngine, &MediaEngine::positionChanged, this, &ApplicationController::positionChanged);
    connect(m_mediaEngine, &MediaEngine::durationChanged, this, &ApplicationController::durationChanged);
    connect(m_mediaEngine, &MediaEngine::playbackFinished, this, &ApplicationController::playNext);
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

    m_playlistModel->addFile(filePath);
    m_playlistModel->setCurrentIndex(m_playlistModel->indexOf(filePath));
    m_mediaEngine->open(filePath);
    emit playbackStateChanged(true);
    return true;
}

void ApplicationController::togglePlayback()
{
    m_mediaEngine->togglePause();
}

void ApplicationController::seekTo(double seconds)
{
    m_mediaEngine->seek(seconds);
}

double ApplicationController::position() const
{
    return m_mediaEngine->position();
}

double ApplicationController::duration() const
{
    return m_mediaEngine->duration();
}

void ApplicationController::stop()
{
    m_mediaEngine->stop();
}

MediaEngine *ApplicationController::mediaEngine() const
{
    return m_mediaEngine;
}

PlaylistModel *ApplicationController::playlistModel() const
{
    return m_playlistModel;
}

void ApplicationController::playItem(int index)
{
    if (index < 0 || index >= m_playlistModel->count())
        return;
    loadFile(m_playlistModel->itemAt(index).filePath);
}

void ApplicationController::playNext()
{
    int next = m_playlistModel->currentIndex() + 1;
    if (next >= m_playlistModel->count())
        return;
    loadFile(m_playlistModel->itemAt(next).filePath);
}
