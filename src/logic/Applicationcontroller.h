#pragma once

#include <QObject>
#include <QString>
#include <QTranslator>

#include "PlaylistModel.h"
#include "RecentFilesModel.h"

class MediaEngine;
class VideoRenderItem;
class QTimer;
class QQmlApplicationEngine;

class ApplicationController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(PlaylistModel *playlistModel READ playlistModel NOTIFY playlistModelChanged)
    Q_PROPERTY(QStringList playlistNames READ playlistNames NOTIFY playlistsChanged)
    Q_PROPERTY(int currentPlaylistIndex READ currentPlaylistIndex NOTIFY playlistsChanged)
    Q_PROPERTY(RecentFilesModel *recentFilesModel READ recentFilesModel CONSTANT)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool isAudioOnly READ isAudioOnly NOTIFY isAudioOnlyChanged)
    Q_PROPERTY(QString currentSubtitle READ currentSubtitle NOTIFY currentSubtitleChanged)
    Q_PROPERTY(QVariantList subtitleStreams READ subtitleStreams NOTIFY subtitleStreamsChanged)
    Q_PROPERTY(int currentSubtitleStream READ currentSubtitleStream NOTIFY currentSubtitleStreamChanged)
    Q_PROPERTY(QVariantList audioStreams READ audioStreams NOTIFY audioStreamsChanged)
    Q_PROPERTY(int currentAudioStream READ currentAudioStream NOTIFY currentAudioStreamChanged)
    Q_PROPERTY(int rotation READ rotation NOTIFY rotationChanged)
    Q_PROPERTY(bool flipVertical READ flipVertical NOTIFY flipVerticalChanged)
    Q_PROPERTY(int abLoopState READ abLoopState NOTIFY abLoopStateChanged)
    Q_PROPERTY(double abLoopA READ abLoopA NOTIFY abLoopChanged)
    Q_PROPERTY(double abLoopB READ abLoopB NOTIFY abLoopChanged)
    Q_PROPERTY(bool abLoopToastVisible READ abLoopToastVisible NOTIFY abLoopToastVisibleChanged)
    Q_PROPERTY(QString abLoopToastText READ abLoopToastText NOTIFY abLoopToastTextChanged)
    Q_PROPERTY(QString coverArtUrl READ coverArtUrl NOTIFY coverArtChanged)
    Q_PROPERTY(QString currentLyric READ currentLyric NOTIFY currentLyricChanged)
    Q_PROPERTY(QString currentFilePath READ currentFilePath NOTIFY currentFilePathChanged)
    Q_PROPERTY(bool isNetworkStream READ isNetworkStream NOTIFY isNetworkStreamChanged)
    Q_PROPERTY(bool isLiveStream READ isLiveStream NOTIFY isLiveStreamChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(QString loadingText READ loadingText NOTIFY loadingTextChanged)
    Q_PROPERTY(int bufferState READ bufferState NOTIFY bufferStateChanged)
    Q_PROPERTY(QString screenshotPath READ screenshotPath WRITE setScreenshotPath NOTIFY screenshotPathChanged)
    Q_PROPERTY(int modalCount READ modalCount WRITE setModalCount NOTIFY modalCountChanged)
    Q_PROPERTY(qint64 subtitleDelayMs READ subtitleDelayMs WRITE setSubtitleDelayMs NOTIFY subtitleDelayMsChanged)
    Q_PROPERTY(bool subtitleDelayToastVisible READ subtitleDelayToastVisible NOTIFY subtitleDelayToastVisibleChanged)
    Q_PROPERTY(QString subtitleDelayToastText READ subtitleDelayToastText NOTIFY subtitleDelayToastTextChanged)

public:
    explicit ApplicationController(QObject *parent = nullptr);
    ~ApplicationController();

    Q_INVOKABLE void openFile();
    Q_INVOKABLE bool loadFile(const QString &path);
    Q_INVOKABLE void addFiles(const QStringList &paths);
    Q_INVOKABLE void addUrl(const QString &url);
    Q_INVOKABLE void createPlaylist(const QString &name);
    Q_INVOKABLE void switchPlaylist(int index);
    Q_INVOKABLE void persistPlaylists();  // 关闭播放列表面板时：保存默认列表与用户列表
    QStringList playlistNames() const;
    int currentPlaylistIndex() const;
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void seekTo(double seconds);
    Q_INVOKABLE void scrubStart();          // 拖动进度条开始：暂停播放
    Q_INVOKABLE void scrubTo(double seconds);  // 拖动中：节流预览目标帧
    Q_INVOKABLE void scrubEnd(double seconds); // 松开：落在最终位置并恢复播放
    Q_INVOKABLE void stop();
    Q_INVOKABLE void playItem(int index);
    Q_INVOKABLE void playNext();
    Q_INVOKABLE void playPrev();
    Q_INVOKABLE void toggleMute();           // 切换静音
    Q_INVOKABLE void setSpeed(double speed); // 设置播放速度
    Q_INVOKABLE void stepForward();
    Q_INVOKABLE void stepBackward();
    Q_INVOKABLE void stepForwardLarge();
    Q_INVOKABLE void stepBackwardLarge();
    Q_INVOKABLE void rotateLeft();           // 画面左旋 90° (UC-07)
    Q_INVOKABLE void rotateRight();          // 画面右旋 90° (UC-07)
    Q_INVOKABLE void toggleFlipVertical();   // 切换垂直翻转 (UC-07)
    Q_INVOKABLE void resetRotation();        // 重置画面旋转 (UC-07)
    Q_INVOKABLE void toggleABLoop();         // A-B 循环三态切换（A → B → 清除）
    Q_INVOKABLE void stepFrameForward();     // 逐帧步进（暂停态下推进一帧）
    Q_INVOKABLE void resumeFromBeginning();  // 从头播放（跳过恢复位置）
    Q_INVOKABLE QString takeScreenshot();    // 截图保存

    // 字幕延迟微调：deltaMs 为调整量（毫秒），正值 = 字幕推迟出现。
    // 用于快捷键 [ / ] 与字幕弹窗 −/+ 按钮（每次 0.1s，长按连续调节）。
    Q_INVOKABLE void nudgeSubtitleDelay(qint64 deltaMs);

    // 当前文件字幕延迟（毫秒，正值 = 推迟）。设置后按文件记忆（PlaybackHistory）。
    qint64 subtitleDelayMs() const;
    void setSubtitleDelayMs(qint64 delayMs);

    bool subtitleDelayToastVisible() const;
    QString subtitleDelayToastText() const;

    QString screenshotPath() const;
    void setScreenshotPath(const QString &path);

    int modalCount() const;
    void setModalCount(int count);

    void setEngine(QQmlApplicationEngine *engine);
    void connectVideoDisplay();

    MediaEngine *mediaEngine() const;
    PlaylistModel *playlistModel() const;
    RecentFilesModel *recentFilesModel() const;

    double position() const;
    double duration() const;
    double volume() const;                   // 获取音量
    void setVolume(double vol);              // 设置音量 0~100
    bool muted() const;                      // 是否静音
    double speed() const;                    // 获取播放速度
    bool isAudioOnly() const;                // 是否为纯音频文件
    QString currentSubtitle() const;
    QVariantList subtitleStreams() const;
    int currentSubtitleStream() const;
    Q_INVOKABLE void setCurrentSubtitleStream(int index);
    QVariantList audioStreams() const;
    int currentAudioStream() const;
    Q_INVOKABLE void setCurrentAudioStream(int index);
    Q_INVOKABLE bool isNetworkUrl(const QString &url) const;
    int rotation() const;                   // 当前画面旋转角度
    bool flipVertical() const;              // 当前垂直翻转状态
    int abLoopState() const;                // A-B 循环状态：0=未激活 1=已设A 2=循环中
    double abLoopA() const;
    double abLoopB() const;
    bool abLoopToastVisible() const;
    QString abLoopToastText() const;
    QString coverArtUrl() const;
    QString currentLyric() const;
    QString currentFilePath() const;
    bool isNetworkStream() const;
    bool isLiveStream() const;
    bool isLoading() const;
    QString loadingText() const;
    int bufferState() const;
    QString theme() const;
    void setTheme(const QString &theme);

signals:
    void requestOpenFile();
    void playbackStateChanged(bool isPlaying);
    void positionChanged(double pos);
    void durationChanged(double dur);
    void volumeChanged(double vol);          // 音量变化信号
    void mutedChanged(bool muted);           // 静音状态变化信号
    void speedChanged(double speed);         // 速度变化信号
    void isAudioOnlyChanged();
    void currentSubtitleChanged(QString text);
    void subtitleStreamsChanged();
    void currentSubtitleStreamChanged(int index);
    void audioStreamsChanged();
    void currentAudioStreamChanged(int index);
    void themeChanged();
    void rotationChanged(int angle);        // 画面旋转角度变化信号
    void flipVerticalChanged(bool flip);    // 垂直翻转状态变化信号
    void abLoopStateChanged(int state);     // A-B 循环状态变化信号
    void abLoopChanged();                   // A/B 点坐标变化信号
    void abLoopToastVisibleChanged();
    void abLoopToastTextChanged();
    void coverArtChanged();
    void currentLyricChanged(QString lyric);
    void resumePositionFound(const QString &filePath, double position);
    void currentFilePathChanged();
    void isNetworkStreamChanged(bool isNetwork);
    void isLiveStreamChanged(bool isLive);
    void isLoadingChanged(bool loading);
    void loadingTextChanged(QString text);
    void errorOccurred(QString message, bool isNetworkRelated);
    void bufferStateChanged(int state);
    void playlistModelChanged();
    void playlistsChanged();
    void screenshotPathChanged(const QString &path);
    void modalCountChanged(int count);
    void subtitleDelayMsChanged(qint64 delayMs);
    void subtitleDelayToastVisibleChanged();
    void subtitleDelayToastTextChanged();

private:
    MediaEngine *m_mediaEngine;
    VideoRenderItem *m_videoRenderItem{nullptr};
    QQmlApplicationEngine *m_qmlEngine{nullptr};
    QTranslator m_translator;
    PlaylistModel *m_playlistModel;
    RecentFilesModel *m_recentFiles;
    QString m_theme;
    int m_modalCount = 0;
    QString m_currentFilePath;
    double m_lastPosition{0.0};
    QTimer *m_saveTimer{nullptr};
    qint64 m_subtitleDelayMs{0};        // 当前文件字幕延迟（ms）
    bool m_subtitleDelayToastVisible{false};
    QString m_subtitleDelayToastText;
    QTimer *m_subtitleDelayToastTimer{nullptr};  // 延迟提示自动隐藏

    int m_abLoopState{0};               // A-B 循环状态（转发自 MediaEngine）
    double m_abLoopA{0.0};
    double m_abLoopB{0.0};
    bool m_abLoopToastVisible{false};
    QString m_abLoopToastText;
    QTimer *m_abLoopToastTimer{nullptr};        // A-B 循环提示自动隐藏

    // 进度条拖动预览状态（scrubStart/scrubTo/scrubEnd）
    bool m_scrubWasPlaying{false};          // 拖动前是否在播放
    double m_scrubPendingPos{-1.0};         // 节流期间暂存的最新目标位置
    double m_scrubLastPos{-1.0};            // 上次实际预览 seek 的位置
    qint64 m_scrubLastSeekMs{0};            // 上次预览 seek 的时刻（ms）

    QList<PlaylistModel *> m_allPlaylists;  // 所有播放列表
    QStringList m_playlistNames;            // 各列表名称
    int m_currentPlaylistIndex = 0;         // 当前列表索引

    bool openAndResume(const QString &filePath);
    // 播放指定路径：高亮列表项（index >= 0 时）+ 记录最近播放 + 打开并恢复
    void playByPath(const QString &filePath, int index);
    // 快进/快退（delta 为负时后退），钳制到合法区间
    void stepBy(double delta);
    void saveCurrentProgress();
    // 把当前文件的音轨/字幕轨/倍速/旋转/翻转偏好写入 PlaybackHistory
    void saveCurrentPrefs();
    // A-B 循环状态变化后刷新 OSD 提示（2.5s 自动隐藏）
    void updateAbLoopToast();
    // 显示 OSD 提示并启动自动隐藏计时（字幕延迟/A-B 循环共用）
    void showToast(bool &visible, void (ApplicationController::*visibleChanged)(), QTimer *timer);
    // 加载/卸载翻译器并刷新静态文本（setEngine 初始化与语言切换共用）
    void applyLanguage(const QString &lang);
    // 切换当前列表并通知界面（createPlaylist/switchPlaylist 共用）
    void activatePlaylist(PlaylistModel *pl);

    void savePlaylists();   // 持久化所有列表（含默认列表）
    void loadPlaylists();   // 启动时载入已保存的列表
};
