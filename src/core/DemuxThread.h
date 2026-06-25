#pragma once

#include <QThread>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
}

class PacketQueue;

class DemuxThread : public QThread
{
    Q_OBJECT

public:
    explicit DemuxThread(QObject *parent = nullptr);
    ~DemuxThread() override;

    void setFormatContext(AVFormatContext *ctx);
    void setStreamIndices(int videoIdx, int audioIdx, int subtitleIdx);
    void setPacketQueues(PacketQueue *videoQueue, PacketQueue *audioQueue, PacketQueue *subtitleQueue);
    void setSubtitleStreamIndex(int idx) { m_subtitleStreamIdx = idx; }
    void stopRead();
    void setPausedRef(const std::atomic<bool> &paused);

protected:
    void run() override;

signals:
    void eofReached();
    void errorOccurred(const QString &message);

private:
    AVFormatContext *m_fmtCtx;
    int m_videoStreamIdx;
    int m_audioStreamIdx;
    int m_subtitleStreamIdx;
    PacketQueue *m_videoQueue;
    PacketQueue *m_audioQueue;
    PacketQueue *m_subtitleQueue;

    std::atomic<bool> m_quit;
    const std::atomic<bool> *m_paused;
};
