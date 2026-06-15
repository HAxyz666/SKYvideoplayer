#include "Applicationcontroller.h"
#include "MediaEngine.h"
#include "PlaylistModel.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

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

static QStringList videoFileFilters()
{
    return { "*.mp4", "*.mkv", "*.avi", "*.mov", "*.flv", "*.wmv" };
}

static void scanDirectoryFiles(PlaylistModel *model, const QString &dirPath)
{
    QDir dir(dirPath);
    QFileInfoList entries = dir.entryInfoList(videoFileFilters(), QDir::Files);
    for (const auto &entry : entries) {
        model->addFile(entry.absoluteFilePath());
    }
}

bool ApplicationController::loadFile(const QString &path)
{
    QString filePath = path;
    if (filePath.startsWith("file://"))
        filePath = filePath.mid(7);

    QFileInfo fi(filePath);
    m_playlistModel->clear();
    scanDirectoryFiles(m_playlistModel, fi.absolutePath());
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
    m_playlistModel->setCurrentIndex(index);
    QString filePath = m_playlistModel->itemAt(index).filePath;
    m_mediaEngine->open(filePath);
    emit playbackStateChanged(true);
}

void ApplicationController::playNext()
{
    QString nextPath = m_playlistModel->nextFilePath();
    if (nextPath.isEmpty())
        return;
    int nextIdx = m_playlistModel->indexOf(nextPath);
    if (nextIdx >= 0) {
        m_playlistModel->setCurrentIndex(nextIdx);
        m_mediaEngine->open(nextPath);
        emit playbackStateChanged(true);
    }
}

void ApplicationController::playPrev()
{
    QString prevPath = m_playlistModel->prevFilePath();
    if (prevPath.isEmpty())
        return;
    int prevIdx = m_playlistModel->indexOf(prevPath);
    if (prevIdx >= 0) {
        m_playlistModel->setCurrentIndex(prevIdx);
        m_mediaEngine->open(prevPath);
        emit playbackStateChanged(true);
    }
}
