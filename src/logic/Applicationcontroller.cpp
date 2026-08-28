#include "Applicationcontroller.h"
#include "MediaEngine.h"
#include "NetworkStreamManager.h"
#include "PlaybackHistory.h"
#include "PlaylistModel.h"
#include "RecentFilesModel.h"
#include "SettingsManager.h"
#include "StreamResolverManager.h"
#include "VideoRenderItem.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QSettings>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <QTranslator>
#include <QVariantMap>

ApplicationController::ApplicationController(QObject *parent)
    : QObject(parent)
    , m_mediaEngine(new MediaEngine(this))
    , m_streamResolver(new StreamResolverManager(this))
    , m_recentFiles(new RecentFilesModel(this))
{
    // 默认播放列表（关闭程序时持久化，不再清空）
    m_playlistModel = new PlaylistModel(this);
    m_allPlaylists.append(m_playlistModel);
    m_playlistNames.append(tr("Default List"));
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
    // 播放偏好按文件记忆：音轨/字幕轨/倍速/旋转/翻转变化即记录
    connect(m_mediaEngine, &MediaEngine::currentAudioStreamChanged, this, &ApplicationController::saveCurrentPrefs);
    connect(m_mediaEngine, &MediaEngine::currentSubtitleStreamChanged, this, &ApplicationController::saveCurrentPrefs);
    connect(m_mediaEngine, &MediaEngine::speedChanged, this, &ApplicationController::saveCurrentPrefs);
    connect(m_mediaEngine, &MediaEngine::rotationChanged, this, &ApplicationController::saveCurrentPrefs);
    connect(m_mediaEngine, &MediaEngine::flipVerticalChanged, this, &ApplicationController::saveCurrentPrefs);
    // 转发 MediaEngine 画面旋转 / 翻转信号到 QML 层 (UC-07)
    connect(m_mediaEngine, &MediaEngine::rotationChanged, this, &ApplicationController::rotationChanged);
    connect(m_mediaEngine, &MediaEngine::flipVerticalChanged, this, &ApplicationController::flipVerticalChanged);
    // A-B 循环：转发状态与 A/B 点，状态变化时刷新 OSD 提示。
    // 状态与坐标同批取回，避免 stateChanged 先于 abLoopChanged 到达
    // 导致提示文案使用旧坐标。
    connect(m_mediaEngine, &MediaEngine::abLoopStateChanged, this, [this](int state) {
        m_abLoopState = state;
        m_abLoopA = m_mediaEngine->abLoopA();
        m_abLoopB = m_mediaEngine->abLoopB();
        emit abLoopChanged();
        emit abLoopStateChanged(state);
        updateAbLoopToast();
    });
    connect(m_mediaEngine, &MediaEngine::abLoopChanged, this, [this]() {
        m_abLoopA = m_mediaEngine->abLoopA();
        m_abLoopB = m_mediaEngine->abLoopB();
        emit abLoopChanged();
        // 循环中重设 B 点（状态不变）也要刷新提示文案
        if (m_abLoopState == 2)
            updateAbLoopToast();
    });
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
    // 致命错误（引擎已自行停管线）：清理当前文件/标题状态，再转发给 UI 回主菜单
    connect(m_mediaEngine, &MediaEngine::fatalErrorOccurred, this, [this](const QString &msg, bool isNet) {
        m_currentFilePath.clear();
        m_currentTitle.clear();
        m_resolverRecentAdded = false;
        emit currentFilePathChanged();
        emit currentTitleChanged();
        emit errorOccurred(msg, isNet);
    });
    connect(m_mediaEngine, &MediaEngine::bufferStateChanged, this, &ApplicationController::bufferStateChanged);
    connect(m_mediaEngine, &MediaEngine::networkStreamReady, this, [this](const QString &url) {
        m_currentFilePath = url;
        emit currentFilePathChanged();
        // 解析模式：最近记录（含真实标题）已在解析成功后写入，此处不重复添加；
        // 原生模式：标题从 URL 末尾派生。已有真实标题（解析模式/reopen 后）
        // 时不再覆盖，避免直播 reopen 把标题重新污染成直链。
        if (m_resolverRecentAdded) {
            m_resolverRecentAdded = false;
            return;
        }
        if (m_currentTitle.isEmpty()) {
            m_currentTitle = QFileInfo(url).fileName();
            emit currentTitleChanged();
        }
        m_recentFiles->addFile(url);
    });

    // 网络流解析完成：按结果形态交给引擎（单 URL / DASH 分离流）
    connect(m_streamResolver, &StreamResolverManager::resolveFinished,
            this, [this](const ResolveResult &r, const QString &sourceUrl, int mode) {
        m_mediaEngine->setLoadingState(false);
        if (!r.ok) {
            emit errorOccurred(tr("解析失败：%1").arg(r.error), true);
            return;
        }
        bool ok = false;
        if (!r.videoUrl.isEmpty() && !r.audioUrl.isEmpty()) {
            ok = m_mediaEngine->openSplit(r.videoUrl, r.audioUrl,
                                          r.videoHeaders, r.audioHeaders);
        } else if (!r.directUrl.isEmpty()) {
            ok = m_mediaEngine->openWithHeaders(r.directUrl, r.headers);
        } else {
            emit errorOccurred(tr("解析结果为空"), true);
            return;
        }
        emit playbackStateChanged(ok);

        // 用 yt-dlp 返回的真实标题作为显示标题与最近记录标题（避免直链污染）。
        // 最近记录存原始输入 URL + 用户选择的模式，点击时按模式重新解析。
        m_currentTitle = r.title.isEmpty() ? QFileInfo(sourceUrl).fileName() : r.title;
        emit currentTitleChanged();
        m_resolverRecentAdded = true;
        m_recentFiles->addFile(sourceUrl, m_currentTitle, mode);

        // 播放列表展示正在播放的网络流（真实标题 + 当前项高亮）
        m_playlistModel->addUrl(sourceUrl, m_currentTitle, mode);
        m_playlistModel->setCurrentIndex(m_playlistModel->indexOf(sourceUrl));
        Q_UNUSED(ok);
    });
    // 解析进行中：借用引擎加载态展示遮罩
    connect(m_streamResolver, &StreamResolverManager::resolvingChanged,
            this, [this](bool resolving) {
        if (resolving)
            m_mediaEngine->setLoadingState(true, tr("解析中..."));
        // 结束时由 resolveFinished 统一清除（含失败路径）
    });

    // 隐藏类 OSD 提示：2.5 秒后自动隐藏（字幕延迟 / A-B 循环共用）
    auto makeToastTimer = [this](bool *visible, void (ApplicationController::*changed)()) {
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(2500);
        connect(timer, &QTimer::timeout, this, [this, visible, changed]() {
            *visible = false;
            (this->*changed)();
        });
        return timer;
    };
    m_subtitleDelayToastTimer =
        makeToastTimer(&m_subtitleDelayToastVisible, &ApplicationController::subtitleDelayToastVisibleChanged);
    m_abLoopToastTimer =
        makeToastTimer(&m_abLoopToastVisible, &ApplicationController::abLoopToastVisibleChanged);
}

void ApplicationController::openFile()
{
    emit requestOpenFile();
}

static QStringList mediaFileFilters()
{
    return { "*.mp4", "*.mkv", "*.avi", "*.mov", "*.flv", "*.wmv",
             "*.mp3", "*.flac", "*.wav", "*.aac", "*.ogg", "*.opus",
             "*.m4a", "*.wma" };
}

// 去掉 file:// 前缀，兼容拖拽/文件对话框传入的 URL
static QString stripFileScheme(const QString &path)
{
    return path.startsWith("file://") ? path.mid(7) : path;
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
    for (const QString &raw : paths)
        m_playlistModel->addFile(stripFileScheme(raw));
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
    QString filePath = stripFileScheme(path);
    m_resolverRecentAdded = false;

    // 网络流：异步初始化，避免阻塞主线程
    if (NetworkStreamManager::isNetworkUrl(filePath)) {
        m_playlistModel->clear();
        m_playlistModel->addUrl(filePath);
        m_playlistModel->setCurrentIndex(m_playlistModel->indexOf(filePath));
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

// 打开网络流：mode 见 StreamResolverManager::Mode
// （原生=直接交给引擎；直播/点播=经 yt-dlp 解析出直链后交给引擎）
void ApplicationController::openNetworkStream(const QString &url, int mode)
{
    QString u = url.trimmed();
    if (u.isEmpty())
        return;

    m_resolverRecentAdded = false;

    // 原生模式：沿用现有网络流路径
    if (mode == StreamResolverManager::Native) {
        loadFile(u);
        return;
    }

    // 直播/点播模式：先检查解析工具可用性
    if (!StreamResolverManager::toolAvailable(mode)) {
        emit errorOccurred(
            mode == StreamResolverManager::Live
                ? tr("未检测到 streamlink，请先安装（pip install streamlink），或改用原生模式")
                : tr("未检测到 yt-dlp，请先安装（pip install yt-dlp），或改用原生模式"),
            true);
        return;
    }

    m_playlistModel->clear();
    resolveNetworkUrl(u, mode);
}

// 网络流解析条目按模式走解析管线（不清空播放列表，供播放列表点击/切换复用）
void ApplicationController::resolveNetworkUrl(const QString &url, int mode)
{
    m_streamResolver->resolve(url, mode);
}

// 取消在途解析：用户取消/关闭网络对话框、或退出播放（返回列表）时调用。
// 成功打开（accept）不触发，避免误杀刚启动的解析；reject/返回路径生效，
// 可取消此前仍在进行中的解析。解析结果在 m_cancelRequested 置位后被丢弃，
// 不会进入播放；此处同步清掉加载遮罩（被丢弃的结果不会再触发 setLoadingState(false)）。
void ApplicationController::cancelNetworkResolve()
{
    if (m_streamResolver)
        m_streamResolver->cancel();
    m_mediaEngine->setLoadingState(false);
}

// 从最近记录打开：网络流解析条目按保存的模式重新解析（拿新鲜直链与真实标题）
void ApplicationController::openRecentFile(const QString &filePath, int mode)
{
    if (mode == StreamResolverManager::Native) {
        loadFile(filePath);
        return;
    }
    openNetworkStream(filePath, mode);
}

void ApplicationController::togglePlayback()
{
    m_mediaEngine->togglePause();
}

void ApplicationController::seekTo(double seconds)
{
    m_mediaEngine->seek(seconds);
}

// --- 进度条拖动实时预览（scrub） ---
// 拖动时先暂停并逐帧跟进目标画面；松开后落在最终位置并恢复播放。
// 网络流/直播流保持原行为：拖动不暂停，松开时一次性 seek。

void ApplicationController::scrubStart()
{
    if (m_mediaEngine->isNetworkStream() || m_mediaEngine->isLiveStream())
        return;
    m_scrubWasPlaying = !m_mediaEngine->isPaused();
    m_scrubPendingPos = -1.0;
    m_scrubLastPos = -1.0;
    m_scrubLastSeekMs = 0;
    if (!m_mediaEngine->isPaused())
        m_mediaEngine->pause();
}

void ApplicationController::scrubTo(double seconds)
{
    if (m_mediaEngine->isNetworkStream() || m_mediaEngine->isLiveStream())
        return;

    const double target = qBound(0.0, seconds, m_mediaEngine->duration());
    // 节流：距上次预览 seek 不足 100ms 时只暂存最新位置（拖动事件远密于
    // seek 代价），由后续事件或 scrubEnd 兜底消费。
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_scrubLastSeekMs < 100) {
        m_scrubPendingPos = target;
        return;
    }
    m_scrubPendingPos = -1.0;
    m_scrubLastSeekMs = now;
    m_scrubLastPos = target;
    m_mediaEngine->seekAndShowFrame(target);
}

void ApplicationController::scrubEnd(double seconds)
{
    if (m_mediaEngine->isNetworkStream() || m_mediaEngine->isLiveStream()) {
        m_mediaEngine->seek(qBound(0.0, seconds, m_mediaEngine->duration()));
        return;
    }

    // 取拖动期间暂存的最新位置（节流事件不会丢目标）
    double finalPos = (m_scrubPendingPos >= 0.0) ? m_scrubPendingPos
                                                 : qBound(0.0, seconds, m_mediaEngine->duration());
    m_scrubPendingPos = -1.0;

    // 与最后预览位置不一致才补一次 seek，避免松开瞬间重复重建管线。
    // m_scrubLastPos == -1 表示拖动期间从未产生预览 seek（按下即松开），
    // 位置未变，无需 seek，直接恢复播放状态即可。
    if (m_scrubLastPos >= 0.0 && qAbs(finalPos - m_scrubLastPos) > 0.05) {
        m_scrubLastPos = finalPos;
        m_mediaEngine->seekAndShowFrame(finalPos);
    }

    // 恢复拖动前的播放状态
    if (m_scrubWasPlaying)
        m_mediaEngine->resume();
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
    m_currentTitle.clear();
    m_resolverRecentAdded = false;
    emit currentFilePathChanged();
    emit currentTitleChanged();
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
    activatePlaylist(pl);
    savePlaylists();
}

// 切换到指定索引的列表
void ApplicationController::switchPlaylist(int index)
{
    if (index < 0 || index >= m_allPlaylists.size() || index == m_currentPlaylistIndex)
        return;
    m_currentPlaylistIndex = index;
    activatePlaylist(m_allPlaylists.at(index));
}

// 切换当前列表并通知界面（createPlaylist/switchPlaylist 共用）
void ApplicationController::activatePlaylist(PlaylistModel *pl)
{
    m_playlistModel = pl;
    emit playlistModelChanged();
    emit playlistsChanged();
}

// 关闭播放列表面板时调用：保存默认列表与用户创建的列表（不再清空）
void ApplicationController::persistPlaylists()
{
    savePlaylists();
}

// 持久化所有列表（含索引 0 的默认列表）
// 网络流条目存 {path,title,mode} 映射以保留真实标题与播放模式；本地文件存路径字符串。
void ApplicationController::savePlaylists()
{
    QSettings settings;
    auto serialize = [](const PlaylistModel *pl) {
        QVariantList out;
        for (int r = 0; r < pl->count(); ++r) {
            const PlaylistItem &it = pl->itemAt(r);
            if (NetworkStreamManager::isNetworkUrl(it.filePath)) {
                QVariantMap m;
                m["path"] = it.filePath;
                m["title"] = it.title;
                m["mode"] = it.mode;
                out.append(m);
            } else {
                out.append(it.filePath);
            }
        }
        return out;
    };

    // 默认列表（索引 0）单独保存
    QVariantList defaultFiles = serialize(m_allPlaylists.first());
    settings.setValue("defaultPlaylist", defaultFiles);

    // 用户创建的列表（索引 1 起）
    QVariantList saved;
    for (int i = 1; i < m_allPlaylists.size(); ++i) {
        QVariantMap entry;
        entry["name"] = m_playlistNames.at(i);
        entry["files"] = serialize(m_allPlaylists.at(i));
        saved.append(entry);
    }
    settings.setValue("playlists", saved);
}

// 启动时载入已保存的列表（含默认列表）
// 网络 URL 用 addUrl（不探测时长，避免启动时阻塞在 avformat_open_input）；
// 兼容旧格式（纯路径字符串）与新格式（{path,title,mode} 映射）。
void ApplicationController::loadPlaylists()
{
    QSettings settings;
    auto addEntry = [this](PlaylistModel *pl, const QVariant &v) {
        if (v.canConvert<QVariantMap>()) {
            const QVariantMap m = v.toMap();
            const QString path = m.value("path").toString();
            if (!path.isEmpty())
                pl->addUrl(path, m.value("title").toString(), m.value("mode", 0).toInt());
        } else {
            const QString fp = v.toString();
            if (NetworkStreamManager::isNetworkUrl(fp))
                pl->addUrl(fp);
            else
                pl->addFile(fp);
        }
    };

    // 载入默认列表
    QVariantList defaultFiles = settings.value("defaultPlaylist").toList();
    for (const QVariant &f : defaultFiles)
        addEntry(m_playlistModel, f);

    // 载入用户列表
    QVariantList saved = settings.value("playlists").toList();
    for (const QVariant &v : saved) {
        QVariantMap entry = v.toMap();
        QString name = entry.value("name").toString();
        QVariantList files = entry.value("files").toList();

        auto *pl = new PlaylistModel(this);
        for (const QVariant &f : files)
            addEntry(pl, f);
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
    playByPath(m_playlistModel->itemAt(index).filePath, index);
}

void ApplicationController::playNext()
{
    QString nextPath = m_playlistModel->nextFilePath();
    if (nextPath.isEmpty())
        return;
    playByPath(nextPath, m_playlistModel->indexOf(nextPath));
}

void ApplicationController::playPrev()
{
    QString prevPath = m_playlistModel->prevFilePath();
    if (prevPath.isEmpty())
        return;
    playByPath(prevPath, m_playlistModel->indexOf(prevPath));
}

// 播放指定路径：高亮列表项（index >= 0 时）+ 记录最近播放 + 打开并恢复进度
void ApplicationController::playByPath(const QString &filePath, int index)
{
    if (index >= 0)
        m_playlistModel->setCurrentIndex(index);
    m_recentFiles->addFile(filePath);

    // 网络流解析条目（直播/点播）：按条目保存的模式重新解析，
    // 避免原生打开页面 URL 失败；解析完成后由 resolveFinished 更新标题。
    if (index >= 0 && NetworkStreamManager::isNetworkUrl(filePath)
        && m_playlistModel->itemAt(index).mode != StreamResolverManager::Native) {
        resolveNetworkUrl(filePath, m_playlistModel->itemAt(index).mode);
        emit playbackStateChanged(true);   // 提前切播放视图，失败经 errorOccurred 回退
        return;
    }

    bool ok = openAndResume(filePath);
    emit playbackStateChanged(ok);
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
    stepBy(5.0);
}

void ApplicationController::stepBackward()
{
    stepBy(-5.0);
}

void ApplicationController::stepForwardLarge()
{
    stepBy(30.0);
}

void ApplicationController::stepBackwardLarge()
{
    stepBy(-30.0);
}

// 快进/快退（delta 为负时后退），钳制到合法区间
void ApplicationController::stepBy(double delta)
{
    double pos = m_mediaEngine->position();
    m_mediaEngine->seek(qBound(0.0, pos + delta, m_mediaEngine->duration()));
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
        m_subtitleDelayToastText = tr("Subtitle delay reset");
    else if (m_subtitleDelayMs > 0)
        m_subtitleDelayToastText = tr("Subtitle delay +%1s").arg(sec, 0, 'f', 1);
    else
        m_subtitleDelayToastText = tr("Subtitle delay %1s").arg(sec, 0, 'f', 1);
    emit subtitleDelayToastTextChanged();
    showToast(m_subtitleDelayToastVisible, &ApplicationController::subtitleDelayToastVisibleChanged,
              m_subtitleDelayToastTimer);
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

// --- A-B 区间循环 / 逐帧步进 ---

void ApplicationController::toggleABLoop()
{
    m_mediaEngine->toggleABLoop();
}

void ApplicationController::stepFrameForward()
{
    m_mediaEngine->stepFrameForward();
}

int ApplicationController::abLoopState() const
{
    return m_abLoopState;
}

double ApplicationController::abLoopA() const
{
    return m_abLoopA;
}

double ApplicationController::abLoopB() const
{
    return m_abLoopB;
}

bool ApplicationController::abLoopToastVisible() const
{
    return m_abLoopToastVisible;
}

QString ApplicationController::abLoopToastText() const
{
    return m_abLoopToastText;
}

void ApplicationController::updateAbLoopToast()
{
    auto fmt = [](double sec) {
        int total = static_cast<int>(sec);
        return QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
    };
    switch (m_abLoopState) {
    case 1:
        m_abLoopToastText = tr("Loop start A: %1, press again to set B").arg(fmt(m_abLoopA));
        break;
    case 2:
        m_abLoopToastText = tr("A-B loop: %1 → %2").arg(fmt(m_abLoopA), fmt(m_abLoopB));
        break;
    default:
        m_abLoopToastText = tr("A-B loop cleared");
        break;
    }
    emit abLoopToastTextChanged();
    showToast(m_abLoopToastVisible, &ApplicationController::abLoopToastVisibleChanged,
              m_abLoopToastTimer);
}

// 显示 OSD 提示并启动 2.5s 自动隐藏（字幕延迟/A-B 循环共用）
void ApplicationController::showToast(bool &visible,
                                      void (ApplicationController::*visibleChanged)(),
                                      QTimer *timer)
{
    visible = true;
    (this->*visibleChanged)();
    timer->start();
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
        m_currentTitle = QFileInfo(filePath).fileName();
        m_lastPosition = 0.0;
        emit currentFilePathChanged();
        emit currentTitleChanged();

        // 恢复该文件记忆的字幕延迟（按文件记忆）
        setSubtitleDelayMs(PlaybackHistory::instance().getSubtitleDelay(filePath));

        // 恢复该文件记忆的播放偏好：音轨/字幕轨/倍速/旋转/翻转。
        // 音轨先于字幕恢复：switchAudioToStream 会重建全部线程，
        // 字幕索引在其后设置可避免重建被打断。
        const QVariantMap prefs = PlaybackHistory::instance().prefs(filePath);
        if (prefs.contains("audioStream")) {
            const int idx = prefs.value("audioStream").toInt();
            if (idx >= 0 && idx < m_mediaEngine->audioStreams().size())
                m_mediaEngine->setCurrentAudioStream(idx);
        }
        if (prefs.contains("subtitleStream")) {
            const int idx = prefs.value("subtitleStream").toInt();
            if (idx < m_mediaEngine->subtitleStreams().size()) // -1 = 关闭字幕
                m_mediaEngine->setCurrentSubtitleStream(idx);
        }
        if (prefs.contains("speed"))
            m_mediaEngine->setSpeed(prefs.value("speed").toDouble());
        if (prefs.contains("rotation"))
            m_mediaEngine->setRotation(prefs.value("rotation").toInt());
        if (prefs.contains("flipVertical"))
            m_mediaEngine->setFlipVertical(prefs.value("flipVertical").toBool());

        emit resumePositionFound(filePath, initialSeekPos > 0.0 ? savedPos : 0.0);

        m_saveTimer->start();
    } else {
        m_currentFilePath.clear();
        m_currentTitle.clear();
        emit currentFilePathChanged();
        emit currentTitleChanged();
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

void ApplicationController::saveCurrentPrefs()
{
    if (m_currentFilePath.isEmpty())
        return;

    QVariantMap p;
    p[QStringLiteral("audioStream")] = m_mediaEngine->currentAudioStream();
    p[QStringLiteral("subtitleStream")] = m_mediaEngine->currentSubtitleStream();
    p[QStringLiteral("speed")] = m_mediaEngine->speed();
    p[QStringLiteral("rotation")] = m_mediaEngine->rotation();
    p[QStringLiteral("flipVertical")] = m_mediaEngine->flipVertical();
    PlaybackHistory::instance().savePrefs(m_currentFilePath, p);
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

    // 初始化当前语言翻译（翻译器就绪前构造函数中的 tr() 尚未生效）
    applyLanguage(SettingsManager::instance().language());

    // 语言切换时即时生效
    connect(&SettingsManager::instance(), &SettingsManager::languageChanged, this, [this](const QString &lang) {
        applyLanguage(lang);
        if (m_qmlEngine)
            m_qmlEngine->retranslate();
    });
}

// 加载/卸载翻译器并刷新静态文本（setEngine 初始化与语言切换共用）
void ApplicationController::applyLanguage(const QString &lang)
{
    QCoreApplication::removeTranslator(&m_translator);
    QCoreApplication::removeTranslator(&m_qtBaseTranslator);
    QCoreApplication::removeTranslator(&m_qtDeclarativeTranslator);

    // QML/源码字符串为英文，zh_CN.qm 把 qsTr 解析为中文；仅当选择中文时才安装翻译器，
    // 否则 qsTr 回退到英文源串，从而实现中英文真正切换。Qt 内置翻译（QLineEdit/TextField
    // 右键菜单等标准控件文案）同样按语言门控：英文模式下本就应保持英文。
    if (lang == "zh_CN") {
        if (m_translator.load(":/i18n/zh_CN.qm"))
            QCoreApplication::installTranslator(&m_translator);

        const QString baseDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
        if (m_qtBaseTranslator.load("qtbase_zh_CN", baseDir))
            QCoreApplication::installTranslator(&m_qtBaseTranslator);
        if (m_qtDeclarativeTranslator.load("qtdeclarative_zh_CN", baseDir))
            QCoreApplication::installTranslator(&m_qtDeclarativeTranslator);
    }

    // 刷新默认列表名称以匹配当前语言
    if (!m_playlistNames.isEmpty()) {
        m_playlistNames[0] = tr("Default List");
        emit playlistsChanged();
    }
}

QString ApplicationController::currentFilePath() const
{
    return m_currentFilePath;
}

QString ApplicationController::currentTitle() const
{
    return m_currentTitle;
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
