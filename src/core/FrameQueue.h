#pragma once

#include <QQueue>
#include <QMutex>
#include <QWaitCondition>

extern "C" {
#include <libavcodec/avcodec.h>
}
//**职责**: 线程安全的帧缓冲队列，基于 QMutex + QWaitCondition 实现。解封装线程写入 PacketQueue，解码线程读取 PacketQueue 并写入 FrameQueue，渲染/音频线程读取 FrameQueue。支持队列满时阻塞写入、队列空时阻塞读取。
class FrameQueue
{
public:
    explicit FrameQueue(int maxSize = 24);
    ~FrameQueue();

    void push(AVFrame *frame);
    AVFrame *pop();
    AVFrame *tryPop(int timeoutMs);
    AVFrame *peek();        //查看队首帧
    int size() const;
    bool isFull() const;
    bool isEmpty() const;
    void clear();
    void setFinished(bool finished);
    bool isFinished() const;
    void flush();   //唤醒所有等待线程

private:
    QQueue<AVFrame *> m_queue;
    mutable QMutex m_mutex;
    QWaitCondition m_notEmpty;
    QWaitCondition m_notFull;
    int m_maxSize;
    bool m_finished;
};
