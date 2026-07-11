#include "Applicationcontroller.h"
#include "MediaEngine.h"
#include "PlaybackHistory.h"
#include "PlaylistModel.h"
#include "RecentFilesModel.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTimer>

ApplicationController::ApplicationController(QObject *parent)
    : QObject(parent)
    , m_mediaEngine(new MediaEngine(this))
    , m_playlistModel(new PlaylistModel(this))
    , m_recentFiles(new RecentFilesModel(this))
    , m_theme("dark")
{
    QSettings settings;
    m_theme = settings.value("theme", "dark").toString();

    connect(m_mediaEngine, &MediaEngine::pausedChanged, this, [this](bool paused) {
        emit playbackStateChanged(!paused);
    });
    connect(m_mediaEngine, &MediaEngine::positionChanged, this, &ApplicationController::positionChanged);
    connect(m_mediaEngine, &MediaEngine::durationChanged, this, &ApplicationController::durationChanged);
    connect(m_mediaEngine, &MediaEngine::playbackFinished, this, &ApplicationController::playNext);
    // 转发 MediaEngine 音量/静音/速度信号到 QML 层
    connect(m_mediaEngine, &MediaEngine::volumeChanged, this, &ApplicationController::volumeChanged);
    connect(m_mediaEngine, &MediaEngine::mutedChanged, this, &ApplicationController::mutedChanged);
    connect(m_mediaEngine, &MediaEngine::speedChanged, this, &ApplicationController::speedChanged);
    connect(m_mediaEngine, &MediaEngine::hasVideoChanged, this, [this]() {
        emit isAudioOnlyChanged();
    });
    connect(m_mediaEngine, &MediaEngine::currentSubtitleChanged, this, &ApplicationController::currentSubtitleChanged);
    connect(m_mediaEngine, &MediaEngine::subtitleStreamsChanged, this, &ApplicationController::subtitleStreamsChanged);
    connect(m_mediaEngine, &MediaEngine::currentSubtitleStreamChanged, this, &ApplicationController::currentSubtitleStreamChanged);
    // 转发 MediaEngine 画面旋转 / 翻转信号到 QML 层 (UC-07)
    connect(m_mediaEngine, &MediaEngine::rotationChanged, this, &ApplicationController::rotationChanged);
    connect(m_mediaEngine, &MediaEngine::flipVerticalChanged, this, &ApplicationController::flipVerticalChanged);
    connect(m_mediaEngine, &MediaEngine::coverArtChanged, this, &ApplicationController::coverArtChanged);
    connect(m_mediaEngine, &MediaEngine::currentLyricChanged, this, &ApplicationController::currentLyricChanged);

    // 播放进度自动保存
    m_saveTimer = new QTimer(this);
    m_saveTimer->setInterval(5000);
    connect(m_saveTimer, &QTimer::timeout, this, &ApplicationController::saveCurrentProgress);
    connect(m_mediaEngine, &MediaEngine::positionChanged, this, [this](double pos) {
        m_lastPosition = pos;
    });

    connect(m_mediaEngine, &MediaEngine::isNetworkStreamChanged, this, &ApplicationController::isNetworkStreamChanged);
    connect(m_mediaEngine, &MediaEngine::isLiveStreamChanged, this, &ApplicationController::isLiveStreamChanged);
    connect(m_mediaEngine, &MediaEngine::isLoadingChanged, this, &ApplicationController::isLoadingChanged);
    connect(m_mediaEngine, &MediaEngine::loadingTextChanged, this, &ApplicationController::loadingTextChanged);
    connect(m_mediaEngine, &MediaEngine::errorOccurred, this, &ApplicationController::errorOccurred);
}

bool ApplicationController::openFile()
{
    emit requestOpenFile();
    return true;
}

static QStringList mediaFileFilters()
{
    return { "*.mp4", "*.mkv", "*.avi", "*.mov", "*.flv", "*.wmv",
             "*.mp3", "*.flac", "*.wav", "*.aac", "*.ogg", "*.opus",
             "*.m4a", "*.wma" };
}

static void scanDirectoryFiles(PlaylistModel *model, const QString &dirPath)
{
    QDir dir(dirPath);
    QFileInfoList entries = dir.entryInfoList(mediaFileFilters(), QDir::Files);
    for (const auto &entry : entries) {
        model->addFile(entry.absoluteFilePath());
    }
}

bool ApplicationController::loadFile(const QString &path)
{
    QString filePath = path;
    if (filePath.startsWith("file://"))
        filePath = filePath.mid(7);

    // 网络流：跳过目录扫描和播放列表操作
    if (filePath.startsWith("http://") || filePath.startsWith("https://") ||
        filePath.startsWith("rtmp://") || filePath.startsWith("rtmps://") ||
        filePath.startsWith("rtsp://") || filePath.startsWith("rtsps://") ||
        filePath.startsWith("mms://") || filePath.startsWith("mmsh://") ||
        filePath.startsWith("udp://") || filePath.startsWith("tcp://")) {
        m_playlistModel->clear();
        bool ok = m_mediaEngine->open(filePath);
        emit playbackStateChanged(ok);
        return ok;
    }

    QFileInfo fi(filePath);
    m_recentFiles->addFile(filePath);
    m_playlistModel->clear();
    scanDirectoryFiles(m_playlistModel, fi.absolutePath());
    m_playlistModel->setCurrentIndex(m_playlistModel->indexOf(filePath));
    bool ok = openAndResume(filePath);
    emit playbackStateChanged(ok);
    return ok;
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
    saveCurrentProgress();
    m_saveTimer->stop();
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

RecentFilesModel *ApplicationController::recentFilesModel() const
{
    return m_recentFiles;
}

void ApplicationController::playItem(int index)
{
    if (index < 0 || index >= m_playlistModel->count())
        return;
    m_playlistModel->setCurrentIndex(index);
    QString filePath = m_playlistModel->itemAt(index).filePath;
    m_recentFiles->addFile(filePath);
    bool ok = openAndResume(filePath);
    emit playbackStateChanged(ok);
}

void ApplicationController::playNext()
{
    QString nextPath = m_playlistModel->nextFilePath();
    if (nextPath.isEmpty())
        return;
    int nextIdx = m_playlistModel->indexOf(nextPath);
    if (nextIdx >= 0) {
        m_playlistModel->setCurrentIndex(nextIdx);
        m_recentFiles->addFile(nextPath);
        bool ok = openAndResume(nextPath);
        emit playbackStateChanged(ok);
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
        m_recentFiles->addFile(prevPath);
        bool ok = openAndResume(prevPath);
        emit playbackStateChanged(ok);
    }
}

// --- 音量控制，代理到 MediaEngine ---

void ApplicationController::toggleMute()
{
    m_mediaEngine->setMuted(!m_mediaEngine->muted());
}

void ApplicationController::setVolume(double vol)
{
    m_mediaEngine->setVolume(vol);
}

double ApplicationController::volume() const
{
    return m_mediaEngine->volume();
}

bool ApplicationController::muted() const
{
    return m_mediaEngine->muted();
}

void ApplicationController::setSpeed(double speed)
{
    m_mediaEngine->setSpeed(speed);
}

double ApplicationController::speed() const
{
    return m_mediaEngine->speed();
}

void ApplicationController::stepForward()
{
    double pos = m_mediaEngine->position();
    m_mediaEngine->seek(qMin(pos + 5, m_mediaEngine->duration()));
}

void ApplicationController::stepBackward()
{
    double pos = m_mediaEngine->position();
    m_mediaEngine->seek(qMax(pos - 5, 0.0));
}

void ApplicationController::stepForwardLarge()
{
    double pos = m_mediaEngine->position();
    m_mediaEngine->seek(qMin(pos + 30, m_mediaEngine->duration()));
}

void ApplicationController::stepBackwardLarge()
{
    double pos = m_mediaEngine->position();
    m_mediaEngine->seek(qMax(pos - 30, 0.0));
}

QString ApplicationController::theme() const
{
    return m_theme;
}

void ApplicationController::setTheme(const QString &theme)
{
    if (m_theme == theme) return;
    m_theme = theme;
    QSettings().setValue("theme", theme);
    emit themeChanged();
}

QString ApplicationController::currentSubtitle() const
{
    return m_mediaEngine->currentSubtitle();
}

QVariantList ApplicationController::subtitleStreams() const
{
    return m_mediaEngine->subtitleStreams();
}

int ApplicationController::currentSubtitleStream() const
{
    return m_mediaEngine->currentSubtitleStream();
}

void ApplicationController::setCurrentSubtitleStream(int index)
{
    m_mediaEngine->setCurrentSubtitleStream(index);
}

bool ApplicationController::isAudioOnly() const
{
    return !m_mediaEngine->hasVideo();
}

QString ApplicationController::coverArtUrl() const
{
    return m_mediaEngine->coverArtUrl();
}

QString ApplicationController::currentLyric() const
{
    return m_mediaEngine->currentLyric();
}

// --- 画面旋转 (UC-07) ---

int ApplicationController::rotation() const
{
    return m_mediaEngine->rotation();
}

bool ApplicationController::flipVertical() const
{
    return m_mediaEngine->flipVertical();
}

void ApplicationController::rotateLeft()
{
    m_mediaEngine->rotateLeft();
}

void ApplicationController::rotateRight()
{
    m_mediaEngine->rotateRight();
}

void ApplicationController::toggleFlipVertical()
{
    m_mediaEngine->setFlipVertical(!m_mediaEngine->flipVertical());
}

void ApplicationController::resetRotation()
{
    m_mediaEngine->resetRotation();
}

// --- 播放进度记忆 (UC-AF-05) ---

bool ApplicationController::openAndResume(const QString &filePath)
{
    // 保存上一个文件的进度
    if (!m_currentFilePath.isEmpty())
        saveCurrentProgress();

    bool ok = m_mediaEngine->open(filePath);
    if (ok) {
        m_currentFilePath = filePath;
        m_lastPosition = 0.0;
        emit currentFilePathChanged();

        // 检查是否有保存的播放位置
        double savedPos = PlaybackHistory::instance().getPosition(filePath);
        double dur = m_mediaEngine->duration();
        if (savedPos > 5.0 && dur > 0 && savedPos < dur * 0.95) {
            m_mediaEngine->seek(savedPos);
            emit resumePositionFound(filePath, savedPos);
        } else {
            emit resumePositionFound(filePath, 0.0);
        }

        m_saveTimer->start();
    } else {
        m_currentFilePath.clear();
        m_saveTimer->stop();
    }
    return ok;
}

void ApplicationController::saveCurrentProgress()
{
    if (m_currentFilePath.isEmpty())
        return;
    double dur = m_mediaEngine->duration();
    if (dur <= 0)
        return;
    PlaybackHistory::instance().savePosition(m_currentFilePath, m_lastPosition, dur);
}

void ApplicationController::resumeFromBeginning()
{
    if (m_mediaEngine->duration() > 0) {
        m_mediaEngine->seek(0.0);
        // 清除该文件的保存进度，避免下次又恢复
        PlaybackHistory::instance().removeEntry(m_currentFilePath);
    }
}

QString ApplicationController::currentFilePath() const
{
    return m_currentFilePath;
}

bool ApplicationController::openNetworkStream(const QString &url)
{
    if (url.isEmpty())
        return false;
    return loadFile(url);
}

bool ApplicationController::isNetworkStream() const
{
    return m_mediaEngine->isNetworkStream();
}

bool ApplicationController::isLiveStream() const
{
    return m_mediaEngine->isLiveStream();
}

bool ApplicationController::isLoading() const
{
    return m_mediaEngine->isLoading();
}

QString ApplicationController::loadingText() const
{
    return m_mediaEngine->loadingText();
}
