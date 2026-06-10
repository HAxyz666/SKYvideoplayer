#pragma once

#include <QObject>
#include <QTimer>
#include <QImage>
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

public:
    explicit MediaEngine(QObject *parent = nullptr);
    ~MediaEngine();

    void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void open(const QString &url);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePause();
    bool isPaused() const;

signals:
    void frameReady(const QImage &image);
    void playbackFinished();
    void pausedChanged(bool paused);

private:
    bool initFFmpeg(const QString &filename);
    void cleanup();
    void startThreads();
    void stopThreads();

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

    AVSyncController *m_syncController;
    AudioOutput *m_audioOutput;
    QTimer *m_frameQueueDrainTimer;

    std::atomic<bool> m_paused;
};
