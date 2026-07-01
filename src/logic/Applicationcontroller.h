#pragma once

#include <QObject>
#include <QString>

#include "PlaylistModel.h"
#include "RecentFilesModel.h"

class MediaEngine;

class ApplicationController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(PlaylistModel *playlistModel READ playlistModel CONSTANT)
    Q_PROPERTY(RecentFilesModel *recentFilesModel READ recentFilesModel CONSTANT)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool isAudioOnly READ isAudioOnly NOTIFY isAudioOnlyChanged)
    Q_PROPERTY(QString currentSubtitle READ currentSubtitle NOTIFY currentSubtitleChanged)
    Q_PROPERTY(QVariantList subtitleStreams READ subtitleStreams NOTIFY subtitleStreamsChanged)
    Q_PROPERTY(int currentSubtitleStream READ currentSubtitleStream NOTIFY currentSubtitleStreamChanged)
    Q_PROPERTY(int rotation READ rotation NOTIFY rotationChanged)
    Q_PROPERTY(bool flipVertical READ flipVertical NOTIFY flipVerticalChanged)

public:
    explicit ApplicationController(QObject *parent = nullptr);

    Q_INVOKABLE bool openFile();
    Q_INVOKABLE bool loadFile(const QString &path);
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

private:
    MediaEngine *m_mediaEngine;
    PlaylistModel *m_playlistModel;
    RecentFilesModel *m_recentFiles;
    QString m_theme;
};
