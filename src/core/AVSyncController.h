#pragma once

#include <QObject>

struct AVFrame;
struct AVFormatContext;

enum class SyncMode {
    AudioMaster,
    VideoMaster,
    ExternalClock
};

class AVSyncController : public QObject
{
    Q_OBJECT

public:
    explicit AVSyncController(QObject *parent = nullptr);

    double computeFrameDelay(double videoPts);
    void updateAudioClock(double pts);
    void updateVideoClock(double pts);

    double getAudioClock() const;
    double getVideoClock() const;

    void setSpeed(double speed);
    void reset();
    void setSyncMode(SyncMode mode);

private:
    int synchronizeAudio();
    double synchronizeVideo();

    SyncMode m_syncMode;

    double m_audioClock;
    double m_videoClock;
    double m_frameTimer;
    double m_frameLastPts;
    double m_frameLastDelay;

    double m_speed;

    double m_audioDiffThreshold;
    double m_audioDiffAvgCoef;
    int m_audioDiffAvgCount;
};

