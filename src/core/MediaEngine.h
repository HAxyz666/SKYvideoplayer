#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QList>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
#ifdef ENABLE_HWACCEL
#include <libavutil/hwcontext.h>
#endif
}

#include "SubtitleDecodeThread.h"   // SubtitleEntry（m_externalSubtitles 成员需要完整类型）

struct YUVFrame;

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
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
    Q_PROPERTY(QString currentSubtitle READ currentSubtitle NOTIFY currentSubtitleChanged)
    Q_PROPERTY(QVariantList subtitleStreams READ subtitleStreams NOTIFY subtitleStreamsChanged)
    Q_PROPERTY(int currentSubtitleStream READ currentSubtitleStream WRITE setCurrentSubtitleStream NOTIFY currentSubtitleStreamChanged)
    Q_PROPERTY(int rotation READ rotation NOTIFY rotationChanged)
    Q_PROPERTY(bool flipVertical READ flipVertical NOTIFY flipVerticalChanged)

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
    bool hasVideo() const { return m_videoStreamIndex >= 0; }

    double position() const;
    double duration() const;

    // --- 音量控制 ---
    void setVolume(double vol);     // 设置音量 0~100
    void setMuted(bool muted);      // 设置静音
    double volume() const;          // 获取当前音量
    bool muted() const;             // 是否静音

    // --- 字幕 ---
    QString currentSubtitle() const { return m_currentSubtitle; }
    QVariantList subtitleStreams() const;
    int currentSubtitleStream() const { return m_currentSubtitleStreamIndex; }
    Q_INVOKABLE void setCurrentSubtitleStream(int index);

    // --- 速度控制 ---
    void setSpeed(double speed);
    double speed() const;

    // --- 画面旋转 (UC-07) ---
    int rotation() const { return m_rotation; }
    bool flipVertical() const { return m_flipVertical; }
    Q_INVOKABLE void setRotation(int angle);          // 设置旋转角度 (0/90/180/270)
    Q_INVOKABLE void rotateLeft();                    // 左旋 90° (逆时针)
    Q_INVOKABLE void rotateRight();                   // 右旋 90° (顺时针)
    Q_INVOKABLE void setFlipVertical(bool flip);      // 设置垂直翻转
    Q_INVOKABLE void resetRotation();                 // 重置画面

signals:
    void frameReady(const YUVFrame &frame);
    void playbackFinished();
    void pausedChanged(bool paused);
    void positionChanged(double pos);
    void durationChanged(double dur);
    void volumeChanged(double vol);     // 音量变化信号
    void mutedChanged(bool muted);      // 静音状态变化信号
    void speedChanged(double speed);
    void hasVideoChanged();
    void currentSubtitleChanged(QString text);
    void subtitleStreamsChanged();
    void currentSubtitleStreamChanged(int index);
    void rotationChanged(int angle);          // 画面旋转角度变化信号
    void flipVerticalChanged(bool flip);      // 垂直翻转状态变化信号

private:
    static constexpr int kAudioFrameQueueSize = 32;

    bool initFFmpeg(const QString &filename);
    void cleanup();
    void startThreads();
    void stopThreads();
    void updatePosition();
    void onVideoRefresh();

    void updateSubtitle(double clockSeconds);

    void detectExternalSubtitles(const QString &videoPath);

    void activateExternalSubtitle(int infoIndex);

    QString m_filename;

    AVFormatContext *m_fmtCtx;
    int m_videoStreamIndex;
    int m_audioStreamIndex;
    int m_subtitleStreamIndex;

    AVCodecContext *m_videoCodecCtx;
    AVCodecContext *m_audioCodecCtx;
    AVCodecContext *m_subtitleCodecCtx;

    DemuxThread *m_demuxThread;
    VideoDecodeThread *m_videoThread;
    AudioDecodeThread *m_audioThread;
    SubtitleDecodeThread *m_subtitleThread;

    PacketQueue *m_videoPacketQueue;
    PacketQueue *m_audioPacketQueue;
    PacketQueue *m_subtitlePacketQueue;
    FrameQueue *m_videoFrameQueue;
    FrameQueue *m_audioFrameQueue{nullptr};

    AVSyncController *m_syncController;
    AudioOutput *m_audioOutput;
    QTimer *m_videoRefreshTimer;

    AVRational m_videoTimeBase{1, 90000};

    std::atomic<bool> m_paused;

    double m_position;
    double m_duration;
    qint64 m_startTimeUs;
    qint64 m_pausedDurationUs;
    qint64 m_pauseStartUs;
    QTimer *m_positionTimer;

    qint64 m_lastFrameDisplayTimeUs{0};

    double m_volume;
    bool m_muted;
    bool m_audioOutputReady;
    double m_speed;

    // 画面旋转 / 翻转状态 (UC-07)
    int m_rotation{0};
    bool m_flipVertical{false};

    struct SubtitleStreamInfo {
        int streamIndex = -1;
        QString language;
        QString title;
        bool isExternal = false;
        QString filePath;
        QString label;
    };
    QVector<SubtitleStreamInfo> m_subtitleStreamsInfo;
    int m_currentSubtitleStreamIndex{-1};
    QString m_currentSubtitle;


    bool m_externalMode{false};
    QList<SubtitleEntry> m_externalSubtitles;

#ifdef ENABLE_HWACCEL
    AVBufferRef *m_hwDeviceCtx{nullptr};
    AVPixelFormat m_hwPixFmt{AV_PIX_FMT_NONE};
    AVHWDeviceType m_hwDeviceType{AV_HWDEVICE_TYPE_NONE};
    bool m_useHardwareDecode{false};

    bool createHwDeviceContext(const AVCodec *codec);
#endif
};
