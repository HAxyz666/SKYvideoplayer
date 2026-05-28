#pragma once

#include <QThread>
#include <QImage>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

class AVSyncController;
class AudioOutput;

class MediaEngine : public QThread
{
    Q_OBJECT

public:
    explicit MediaEngine(const QString &filename, QObject *parent = nullptr);
    ~MediaEngine();

    void stop();

protected:
    void run() override;

private:
    bool initFFmpeg();
    void cleanup();

    QString m_filename;
    bool m_stopRequested;

    AVFormatContext *m_formatCtx;
    int m_videoStreamIndex;
    int m_audioStreamIndex;

    AVCodecContext *m_videoCodecCtx;
    AVCodecContext *m_audioCodecCtx;

    SwsContext *m_swsCtx;
    SwrContext *m_swrCtx;

    int64_t m_startTime;

    AVSyncController *m_syncController;
    AudioOutput *m_audioOutput;

signals:
    void frameReady(const QImage &image);
    void playbackFinished();
};
