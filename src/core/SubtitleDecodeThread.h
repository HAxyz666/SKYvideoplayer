#pragma once

#include <QThread>
#include <QMutex>
#include <QList>
#include <QString>
#include <atomic>

extern "C" {
#include <libavutil/rational.h>
}

struct AVCodecContext;
struct AVPacket;

class PacketQueue;

// 单条字幕条目：开始时间(微秒)、结束时间(微秒)、文本
struct SubtitleEntry {
    qint64 startUs = 0;
    qint64 endUs = 0;
    QString text;
};

class SubtitleDecodeThread : public QThread
{
    Q_OBJECT

public:
    explicit SubtitleDecodeThread(QObject *parent = nullptr);
    ~SubtitleDecodeThread() override;

    void setCodecContext(AVCodecContext *ctx);
    void setPacketQueue(PacketQueue *queue);
    void setTimeBase(AVRational tb);
    void stopDecode();
    void setPausedRef(const std::atomic<bool> &paused);
    void clearSubtitles();

    QString getSubtitleAt(qint64 positionUs) const;


    void setExternalSubtitles(const QList<SubtitleEntry> &subs); 
    static QList<SubtitleEntry> loadFromFile(const QString &path);
    static QList<SubtitleEntry> loadLrc(const QString &path);

protected:
    void run() override;

private:
    AVCodecContext *m_codecCtx = nullptr;
    PacketQueue *m_packetQueue = nullptr;
    AVRational m_timeBase;
    std::atomic<bool> m_quit{false};
    const std::atomic<bool> *m_paused = nullptr;

    mutable QMutex m_subMutex;
    QList<SubtitleEntry> m_subtitles;

    // 外挂字幕解析实现
    static QList<SubtitleEntry> loadSrt(const QString &path);
    static QList<SubtitleEntry> loadVtt(const QString &path);
    static QList<SubtitleEntry> loadAss(const QString &path);
};
