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
    Q_PROPERTY(int rotation READ rotation NOTIFY rotationChanged)
    Q_PROPERTY(bool flipVertical READ flipVertical NOTIFY flipVerticalChanged)
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

public:
    explicit ApplicationController(QObject *parent = nullptr);
    ~ApplicationController();

    Q_INVOKABLE bool openFile();
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
    Q_INVOKABLE void resumeFromBeginning();  // 从头播放（跳过恢复位置）
    Q_INVOKABLE QString takeScreenshot();    // 截图保存

    QString screenshotPath() const;
    void setScreenshotPath(const QString &path);

    int modalCount() const;
    void setModalCount(int count);

    void setVideoRenderItem(VideoRenderItem *item);
    void setEngine(QQmlApplicationEngine *engine);

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
    int rotation() const;                   // 当前画面旋转角度
    bool flipVertical() const;              // 当前垂直翻转状态
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
    void themeChanged();
    void rotationChanged(int angle);        // 画面旋转角度变化信号
    void flipVerticalChanged(bool flip);    // 垂直翻转状态变化信号
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

    QList<PlaylistModel *> m_allPlaylists;  // 所有播放列表
    QStringList m_playlistNames;            // 各列表名称
    int m_currentPlaylistIndex = 0;         // 当前列表索引

    bool openAndResume(const QString &filePath);
    void saveCurrentProgress();

    void savePlaylists();   // 持久化所有列表（含默认列表）
    void loadPlaylists();   // 启动时载入已保存的列表
};
