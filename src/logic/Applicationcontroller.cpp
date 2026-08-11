#include "Applicationcontroller.h"
#include "MediaEngine.h"
#include "PlaybackHistory.h"
#include "PlaylistModel.h"
#include "RecentFilesModel.h"
#include "SettingsManager.h"
#include "VideoRenderItem.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QQmlApplicationEngine>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

ApplicationController::ApplicationController(QObject *parent)
    : QObject(parent)
    , m_mediaEngine(new MediaEngine(this))
    , m_recentFiles(new RecentFilesModel(this))
    , m_theme("dark")
{
    // 默认播放列表（关闭程序时持久化，不再清空）
    m_playlistModel = new PlaylistModel(this);
    m_allPlaylists.append(m_playlistModel);
    m_playlistNames.append(tr("Default List"));
    m_currentPlaylistIndex = 0;
    QSettings settings;
    m_theme = settings.value("theme", "dark").toString();

    // 载入已保存的用户列表
    loadPlaylists();

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
    connect(m_mediaEngine, &MediaEngine::subtitleDelayMsChanged, this, &ApplicationController::subtitleDelayMsChanged);
    connect(m_mediaEngine, &MediaEngine::audioStreamsChanged, this, &ApplicationController::audioStreamsChanged);
    connect(m_mediaEngine, &MediaEngine::currentAudioStreamChanged, this, &ApplicationController::currentAudioStreamChanged);
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
    connect(m_mediaEngine, &MediaEngine::bufferStateChanged, this, &ApplicationController::bufferStateChanged);
    connect(m_mediaEngine, &MediaEngine::networkStreamReady, this, [this](const QString &url) {
        m_currentFilePath = url;
        emit currentFilePathChanged();
        m_recentFiles->addFile(url);
    });

    // 字幕延迟调整提示（OSD），2.5 秒后自动隐藏
    m_subtitleDelayToastTimer = new QTimer(this);
    m_subtitleDelayToastTimer->setSingleShot(true);
    m_subtitleDelayToastTimer->setInterval(2500);
    connect(m_subtitleDelayToastTimer, &QTimer::timeout, this, [this]() {
        m_subtitleDelayToastVisible = false;
        emit subtitleDelayToastVisibleChanged();
    });
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

// 将所选文件追加到当前播放列表（不清空原有内容）
void ApplicationController::addFiles(const QStringList &paths)
{
    for (const QString &raw : paths) {
        QString filePath = raw;
        if (filePath.startsWith("file://"))
            filePath = filePath.mid(7);
        m_playlistModel->addFile(filePath);
    }
    savePlaylists();
}

// 添加单个网络 URL 到当前播放列表
void ApplicationController::addUrl(const QString &url)
{
    QString u = url.trimmed();
    if (u.isEmpty())
        return;
    m_playlistModel->addUrl(u);
    savePlaylists();
}

bool ApplicationController::loadFile(const QString &path)
{
    QString filePath = path;
    if (filePath.startsWith("file://"))
        filePath = filePath.mid(7);

    // 网络流：异步初始化，避免阻塞主线程
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
    if (!fi.exists()) {
        emit errorOccurred(tr("无法打开文件：%1").arg(filePath), false);
        emit playbackStateChanged(false);
        return false;
    }
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
    m_currentFilePath.clear();
    emit currentFilePathChanged();
}

MediaEngine *ApplicationController::mediaEngine() const
{
    return m_mediaEngine;
}

PlaylistModel *ApplicationController::playlistModel() const
{
    return m_playlistModel;
}

QStringList ApplicationController::playlistNames() const
{
    return m_playlistNames;
}

int ApplicationController::currentPlaylistIndex() const
{
    return m_currentPlaylistIndex;
}

// 创建新列表并切换到它
void ApplicationController::createPlaylist(const QString &name)
{
    auto *pl = new PlaylistModel(this);
    m_allPlaylists.append(pl);
    QString finalName = name.trimmed();
    if (finalName.isEmpty())
        finalName = tr("List %1").arg(m_allPlaylists.size());
    m_playlistNames.append(finalName);
    m_currentPlaylistIndex = m_allPlaylists.size() - 1;
    m_playlistModel = pl;
    emit playlistModelChanged();
    emit playlistsChanged();
    savePlaylists();
}

// 切换到指定索引的列表
void ApplicationController::switchPlaylist(int index)
{
    if (index < 0 || index >= m_allPlaylists.size() || index == m_currentPlaylistIndex)
        return;
    m_currentPlaylistIndex = index;
    m_playlistModel = m_allPlaylists.at(index);
    emit playlistModelChanged();
    emit playlistsChanged();
}

// 关闭播放列表面板时调用：保存默认列表与用户创建的列表（不再清空）
void ApplicationController::persistPlaylists()
{
    savePlaylists();
}

// 持久化所有列表（含索引 0 的默认列表）
void ApplicationController::savePlaylists()
{
    QSettings settings;
    // 默认列表（索引 0）单独保存
    QVariantList defaultFiles;
    PlaylistModel *def = m_allPlaylists.first();
    for (int r = 0; r < def->count(); ++r)
        defaultFiles.append(def->itemAt(r).filePath);
    settings.setValue("defaultPlaylist", defaultFiles);

    // 用户创建的列表（索引 1 起）
    QVariantList saved;
    for (int i = 1; i < m_allPlaylists.size(); ++i) {
        QVariantMap entry;
        entry["name"] = m_playlistNames.at(i);
        QVariantList files;
        PlaylistModel *pl = m_allPlaylists.at(i);
        for (int r = 0; r < pl->count(); ++r)
            files.append(pl->itemAt(r).filePath);
        entry["files"] = files;
        saved.append(entry);
    }
    settings.setValue("playlists", saved);
}

// 启动时载入已保存的列表（含默认列表）
void ApplicationController::loadPlaylists()
{
    QSettings settings;
    // 载入默认列表
    QVariantList defaultFiles = settings.value("defaultPlaylist").toList();
    for (const QVariant &f : defaultFiles)
        m_playlistModel->addFile(f.toString());

    // 载入用户列表
    QVariantList saved = settings.value("playlists").toList();
    for (const QVariant &v : saved) {
        QVariantMap entry = v.toMap();
        QString name = entry.value("name").toString();
        QStringList files;
        for (const QVariant &f : entry.value("files").toList())
            files.append(f.toString());

        auto *pl = new PlaylistModel(this);
        for (const QString &fp : files)
            pl->addFile(fp);
        m_allPlaylists.append(pl);
        m_playlistNames.append(name.isEmpty() ? tr("List %1").arg(m_allPlaylists.size()) : name);
    }
    m_currentPlaylistIndex = 0;
    emit playlistsChanged();
}

RecentFilesModel *ApplicationController::recentFilesModel() const
{
    return m_recentFiles;
}

ApplicationController::~ApplicationController()
{
    // 程序退出：保存默认列表与用户创建的列表
    savePlaylists();
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

qint64 ApplicationController::subtitleDelayMs() const
{
    return m_subtitleDelayMs;
}

void ApplicationController::setSubtitleDelayMs(qint64 delayMs)
{
    delayMs = qBound<qint64>(-60000LL, delayMs, 60000LL);
    if (m_subtitleDelayMs == delayMs)
        return;
    m_subtitleDelayMs = delayMs;
    m_mediaEngine->setSubtitleDelayMs(delayMs);
    // 按文件记忆（0 = 清除记录）
    if (!m_currentFilePath.isEmpty())
        PlaybackHistory::instance().saveSubtitleDelay(m_currentFilePath, delayMs);
    emit subtitleDelayMsChanged(m_subtitleDelayMs);
}

void ApplicationController::nudgeSubtitleDelay(qint64 deltaMs)
{
    setSubtitleDelayMs(m_subtitleDelayMs + deltaMs);

    // OSD 提示当前延迟；归零时提示"已重置"
    double sec = m_subtitleDelayMs / 1000.0;
    if (m_subtitleDelayMs == 0)
        m_subtitleDelayToastText = tr("字幕延迟已重置");
    else if (m_subtitleDelayMs > 0)
        m_subtitleDelayToastText = tr("字幕延迟 +%1s").arg(sec, 0, 'f', 1);
    else
        m_subtitleDelayToastText = tr("字幕延迟 %1s").arg(sec, 0, 'f', 1);
    emit subtitleDelayToastTextChanged();

    m_subtitleDelayToastVisible = true;
    emit subtitleDelayToastVisibleChanged();
    m_subtitleDelayToastTimer->start();
}

bool ApplicationController::subtitleDelayToastVisible() const
{
    return m_subtitleDelayToastVisible;
}

QString ApplicationController::subtitleDelayToastText() const
{
    return m_subtitleDelayToastText;
}

QVariantList ApplicationController::audioStreams() const
{
    return m_mediaEngine->audioStreams();
}

int ApplicationController::currentAudioStream() const
{
    return m_mediaEngine->currentAudioStream();
}

void ApplicationController::setCurrentAudioStream(int index)
{
    m_mediaEngine->setCurrentAudioStream(index);
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

    // 预取已保存的播放位置，传给 open() 在启动播放线程之前完成 seek，
    // 避免先从 position 0 播放再 seek 导致的音画不同步。
    // 有效范围检查（savedPos > 5 且 < 95% duration）在 open() 内完成。
    double savedPos = PlaybackHistory::instance().getPosition(filePath);
    double initialSeekPos = (savedPos > 5.0) ? savedPos : -1.0;

    bool ok = m_mediaEngine->open(filePath, initialSeekPos);
    if (ok) {
        m_currentFilePath = filePath;
        m_lastPosition = 0.0;
        emit currentFilePathChanged();

        // 恢复该文件记忆的字幕延迟（按文件记忆）
        setSubtitleDelayMs(PlaybackHistory::instance().getSubtitleDelay(filePath));

        if (initialSeekPos > 0.0) {
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

    // 播放到尾部时清除记录，避免恢复到临近结束位置导致循环跳转
    if (m_lastPosition >= dur * 0.90) {
        PlaybackHistory::instance().removeEntry(m_currentFilePath);
        return;
    }

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

QString ApplicationController::takeScreenshot()
{
    if (m_videoRenderItem) {
        QString path = SettingsManager::instance().screenshotPath();
        QDir().mkpath(path);
        QString baseName = QFileInfo(m_currentFilePath).completeBaseName();
        return m_videoRenderItem->captureAndSave(path, baseName);
    }
    return QString();
}

QString ApplicationController::screenshotPath() const
{
    return SettingsManager::instance().screenshotPath();
}

void ApplicationController::setScreenshotPath(const QString &path)
{
    SettingsManager::instance().setScreenshotPath(path);
    emit screenshotPathChanged(path);
}

int ApplicationController::modalCount() const
{
    return m_modalCount;
}

void ApplicationController::setModalCount(int count)
{
    count = qMax(0, count);
    if (m_modalCount != count) {
        m_modalCount = count;
        emit modalCountChanged(count);
    }
}

void ApplicationController::connectVideoDisplay()
{
    if (!m_qmlEngine)
        return;
    auto rootObjects = m_qmlEngine->rootObjects();
    if (rootObjects.isEmpty())
        return;
    VideoRenderItem *display = rootObjects.first()->findChild<VideoRenderItem *>("videoRenderItem");
    if (display) {
        QObject::connect(m_mediaEngine, &MediaEngine::frameReady,
                         display, &VideoRenderItem::setYUVFrame);
        m_videoRenderItem = display;
    }
}

void ApplicationController::setEngine(QQmlApplicationEngine *engine)
{
    m_qmlEngine = engine;

    // 初始化当前语言翻译
    QString lang = SettingsManager::instance().language();
    if (lang != "en" && m_translator.load(":/i18n/" + lang + ".qm"))
        QCoreApplication::installTranslator(&m_translator);

    // 翻译器已就绪，刷新默认列表名称（构造函数中 tr() 此时尚未生效）
    if (!m_playlistNames.isEmpty()) {
        m_playlistNames[0] = tr("Default List");
        emit playlistsChanged();
    }

    // 语言切换时即时生效
    connect(&SettingsManager::instance(), &SettingsManager::languageChanged, this, [this](const QString &lang) {
        QCoreApplication::removeTranslator(&m_translator);
        if (lang != "en" && m_translator.load(":/i18n/" + lang + ".qm"))
            QCoreApplication::installTranslator(&m_translator);
        // 刷新默认列表名称以匹配新语言
        if (!m_playlistNames.isEmpty()) {
            m_playlistNames[0] = tr("Default List");
            emit playlistsChanged();
        }
        if (m_qmlEngine)
            m_qmlEngine->retranslate();
    });
}

QString ApplicationController::currentFilePath() const
{
    return m_currentFilePath;
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

int ApplicationController::bufferState() const
{
    return m_mediaEngine->bufferState();
}

bool ApplicationController::isNetworkUrl(const QString &url) const
{
    return url.startsWith("http://") || url.startsWith("https://") ||
           url.startsWith("rtmp://") || url.startsWith("rtmps://") ||
           url.startsWith("rtsp://") || url.startsWith("rtsps://") ||
           url.startsWith("mms://") || url.startsWith("mmsh://") ||
           url.startsWith("udp://") || url.startsWith("tcp://");
}
