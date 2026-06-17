#pragma once

#include <QObject>
#include <QTimer>
#include <QImage>
#include <QElapsedTimer>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

class DemuxThread;
class VideoDecodeThread;
class AudioDecodeThread;
class PacketQueue;
class FrameQueue;
class AVSyncController;
class AudioOutput;

class MediaEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)

public:
    explicit MediaEngine(QObject *parent = nullptr);
    ~MediaEngine();

    void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool open(const QString &url);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seek(double seconds);
    bool isPaused() const;

    double position() const;
    double duration() const;

    // --- 音量控制 ---
    void setVolume(double vol);     // 设置音量 0~100
    void setMuted(bool muted);      // 设置静音
    double volume() const;          // 获取当前音量
    bool muted() const;             // 是否静音

    // --- 速度控制 ---
    void setSpeed(double speed);
    double speed() const;

signals:
    void frameReady(const QImage &image);
    void playbackFinished();
    void pausedChanged(bool paused);
    void positionChanged(double pos);
    void durationChanged(double dur);
    void volumeChanged(double vol);     // 音量变化信号
    void mutedChanged(bool muted);      // 静音状态变化信号
    void speedChanged(double speed);

private:
    bool initFFmpeg(const QString &filename);
    void cleanup();
    void startThreads();
    void stopThreads();
    void updatePosition();

    QString m_filename;

    AVFormatContext *m_fmtCtx;
    int m_videoStreamIndex;
    int m_audioStreamIndex;

    AVCodecContext *m_videoCodecCtx;
    AVCodecContext *m_audioCodecCtx;

    DemuxThread *m_demuxThread;
    VideoDecodeThread *m_videoThread;
    AudioDecodeThread *m_audioThread;

    PacketQueue *m_videoPacketQueue;
    PacketQueue *m_audioPacketQueue;
    FrameQueue *m_videoFrameQueue;
    FrameQueue *m_audioFrameQueue{nullptr};

    AVSyncController *m_syncController;
    AudioOutput *m_audioOutput;
    QTimer *m_frameQueueDrainTimer;

    std::atomic<bool> m_paused;

    double m_position;
    double m_duration;
    qint64 m_startTimeUs;
    qint64 m_pausedDurationUs;
    qint64 m_pauseStartUs;
    QTimer *m_positionTimer;

    double m_volume;        // 音量 0~100，通过 AudioOutput 应用
    bool m_muted;           // 静音标志
    bool m_audioOutputReady; // SDL 音频设备是否已打开
    double m_speed;         // 播放速度
};
