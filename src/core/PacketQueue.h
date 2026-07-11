#pragma once

#include <mutex>
#include <condition_variable>
#include <QQueue>
#include <atomic>

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
    void clear();
    void setFinished(bool finished);
    void flush();
    int size() const;

    // 设置退出标志，唤醒所有等待的线程
    void requestQuit();

private:
    QQueue<AVPacket *> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
    int m_maxSize;
    bool m_finished;
    int m_serial;
    std::atomic<bool> m_quitRequested{false};
};
