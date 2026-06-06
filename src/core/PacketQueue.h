#pragma once

#include <mutex>
#include <condition_variable>
#include <QQueue>

extern "C" {
#include <libavcodec/avcodec.h>
}

class PacketQueue
{
public:
    explicit PacketQueue(int maxSize = 64);
    ~PacketQueue();

    void push(AVPacket *pkt);
    AVPacket *pop();
    AVPacket *tryPop(int timeoutMs = 100);
    int size() const;
    bool isFull() const;
    bool isEmpty() const;
    void clear();
    void setFinished(bool finished);
    bool isFinished() const;
    void flush();
    int serial() const;
    void incrementSerial();

private:
    QQueue<AVPacket *> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
    int m_maxSize;
    bool m_finished;
    int m_serial;
};
