#include "FrameQueue.h"
#include <QDebug>

FrameQueue::FrameQueue(int maxSize)
    : m_maxSize(maxSize)
    , m_finished(false)
    , m_serial(0)
{
}

FrameQueue::~FrameQueue()
{
    clear();
}

void FrameQueue::push(AVFrame *frame)
{
    AVFrame *newFrame = av_frame_clone(frame);
    if (!newFrame)
        return;

    std::unique_lock lock(m_mutex);
    // 使用 wait_for 每 100ms 检查一次退出标志
    m_notFull.wait_for(lock, std::chrono::milliseconds(100), [this]() {
        return m_queue.size() < m_maxSize || m_finished || m_quitRequested.load();
    });

    if (m_finished || m_quitRequested.load()) {
        av_frame_free(&newFrame);
        return;
    }

    m_queue.enqueue(newFrame);
    m_notEmpty.notify_one();
}

void FrameQueue::requestQuit()
{
    m_quitRequested.store(true);
    // 唤醒所有等待的线程
    std::lock_guard lock(m_mutex);
    m_notEmpty.notify_all();
    m_notFull.notify_all();
}

AVFrame *FrameQueue::tryPop(int timeoutMs)
{
    std::unique_lock lock(m_mutex);
    if (!m_notEmpty.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                             [this]() { return !m_queue.isEmpty() || m_finished; }))
        return nullptr;

    if (m_queue.isEmpty())
        return nullptr;

    AVFrame *frame = m_queue.dequeue();
    m_notFull.notify_one();
    return frame;
}

AVFrame *FrameQueue::peek()
{
    std::lock_guard lock(m_mutex);
    if (m_queue.isEmpty())
        return nullptr;
    return m_queue.head();
}

void FrameQueue::clear()
{
    std::lock_guard lock(m_mutex);
    while (!m_queue.isEmpty()) {
        AVFrame *frame = m_queue.dequeue();
        av_frame_free(&frame);
    }
    m_notFull.notify_all();
}

void FrameQueue::setFinished(bool finished)
{
    std::lock_guard lock(m_mutex);
    m_finished = finished;
    m_notEmpty.notify_all();
    m_notFull.notify_all();
}

void FrameQueue::flush()
{
    std::lock_guard lock(m_mutex);
    while (!m_queue.isEmpty()) {
        AVFrame *frame = m_queue.dequeue();
        av_frame_free(&frame);
    }
    m_serial++;
    m_notEmpty.notify_all();
    m_notFull.notify_all();
}
