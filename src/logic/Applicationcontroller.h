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
    void themeChanged();

private:
    MediaEngine *m_mediaEngine;
    PlaylistModel *m_playlistModel;
    RecentFilesModel *m_recentFiles;
    QString m_theme;
};
