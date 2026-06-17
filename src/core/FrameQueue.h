#pragma once

#include <mutex>
#include <condition_variable>
#include <QQueue>

extern "C" {
#include <libavcodec/avcodec.h>
}

class FrameQueue
{
public:
    explicit FrameQueue(int maxSize = 24);
    ~FrameQueue();

    void push(AVFrame *frame);
    AVFrame *pop();
    AVFrame *tryPop(int timeoutMs = 100);
    AVFrame *peek();
    int size() const;
    bool isFull() const;
    bool isEmpty() const;
    void clear();
    void setFinished(bool finished);
    bool isFinished() const;
    void flush();
    int serial() const;

private:
    QQueue<AVFrame *> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
    int m_maxSize;
    bool m_finished;
    int m_serial;
};
