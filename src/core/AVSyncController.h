#pragma once

#include <QObject>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

class AVSyncController : public QObject
{
    Q_OBJECT

public:
    explicit AVSyncController(QObject *parent = nullptr);

    double synchronizeVideo(AVFrame *frame, double pts, AVFormatContext *fmtCtx, int streamIndex);
    void reset();

    double videoClock() const;

private:
    double m_videoClock;
};
