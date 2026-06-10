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
    void setStreamIndices(int videoIdx, int audioIdx);
    void setPacketQueues(PacketQueue *videoQueue, PacketQueue *audioQueue);
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
    PacketQueue *m_videoQueue;
    PacketQueue *m_audioQueue;

    std::atomic<bool> m_quit;
    const std::atomic<bool> *m_paused;
};
