#include "AVSyncController.h"

AVSyncController::AVSyncController(QObject *parent)
    : QObject(parent)
    , m_videoClock(0)
{
}

void AVSyncController::reset()
{
    m_videoClock = 0;
}

double AVSyncController::synchronizeVideo(AVFrame *frame, double pts, AVFormatContext *fmtCtx, int streamIndex)
{
    double frameDelay;

    if (pts != 0) {
        m_videoClock = pts;
    } else {
        pts = m_videoClock;
    }

    frameDelay = av_q2d(fmtCtx->streams[streamIndex]->time_base);

    if (frameDelay <= 0.0 || frameDelay >= 1.0) {
        frameDelay = 0.04;
    }

    m_videoClock += frameDelay;

    return pts;
}

double AVSyncController::videoClock() const
{
    return m_videoClock;
}
