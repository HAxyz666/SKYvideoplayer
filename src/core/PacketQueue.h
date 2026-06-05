#pragma once


#include <QQueue>
#include <QMutex>
#include <QWaitCondition>

extern "C" {
#include <libavcodec/avcodec.h>
}
//**职责**: 线程安全的 AVPacket 缓冲队列，结构与 FrameQueue 类似，用于解封装线程到解码线程的数据传递。
class PacketQueue
{
public:
    explicit PacketQueue(int maxSize = 64);
    ~PacketQueue();

    void push(AVPacket *pkt); //入队
    AVPacket *pop();            //出队
    AVPacket *tryPop(int timeoutMs); //超时入队
    int size() const;
    bool isFull() const;
    bool isEmpty() const;
    void clear();               //清空队列并释放
    void setFinished(bool finished);
    bool isFinished() const;
    void flush();
    int serial() const;
    void incrementSerial(); //序列号递增

private:
    QQueue<AVPacket *> m_queue;//Packet队列
    mutable QMutex m_mutex;     //互斥锁
    QWaitCondition m_notEmpty;
    QWaitCondition m_notFull;
    int m_maxSize;
    bool m_finished;
    int m_serial;
};
