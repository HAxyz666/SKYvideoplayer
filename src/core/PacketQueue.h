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

    // 冲刷标记：seek 时 demux 在清空队列后投入的哨兵包（data == nullptr），
    // 解码线程按 FIFO 顺序消费到它时冲刷编解码器与帧队列，保证旧包已处理、
    // 新内容在标记之后才被解码。
    static bool isFlushMarker(const AVPacket *pkt);
    void pushFlushMarker();

    // 通知阻塞在 push 的等待线程（demux 可能在队列满且暂停时阻塞于 push，
    // seek 请求需要让其立即醒来处理，否则 seek 会被拖到下一次 pop 才执行）。
    void notifySeekWaiters();
    void clearSeekNotify();

private:
    static AVPacket s_flushMarker;
    void drain(bool notifyEmpty);
    QQueue<AVPacket *> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
    int m_maxSize;
    bool m_finished;
    std::atomic<bool> m_quitRequested{false};
    // 置位后 push 等待条件立即放行并丢弃在途包（内容在 seek 请求之前，
    // 丢弃无害）；由 demux 处理 seek 时清除，避免误丢后续正常包。
    std::atomic<bool> m_seekNotify{false};
};
