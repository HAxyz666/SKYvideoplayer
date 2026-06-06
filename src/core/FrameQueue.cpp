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

    std::unique_lock lock(m_mutex);
    m_notFull.wait(lock, [this]() { return m_queue.size() < m_maxSize || m_finished; });

    if (m_finished) {
        av_frame_free(&newFrame);
        return;
    }

    m_queue.enqueue(newFrame);
    m_notEmpty.notify_one();
}

AVFrame *FrameQueue::pop()
{
    std::unique_lock lock(m_mutex);
    m_notEmpty.wait(lock, [this]() { return !m_queue.isEmpty() || m_finished; });

    if (m_queue.isEmpty())
        return nullptr;

    AVFrame *frame = m_queue.dequeue();
    m_notFull.notify_one();
    return frame;
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

int FrameQueue::size() const
{
    std::lock_guard lock(m_mutex);
    return m_queue.size();
}

bool FrameQueue::isFull() const
{
    std::lock_guard lock(m_mutex);
    return m_queue.size() >= m_maxSize;
}

bool FrameQueue::isEmpty() const
{
    std::lock_guard lock(m_mutex);
    return m_queue.isEmpty();
}

void FrameQueue::clear()
{
    std::lock_guard lock(m_mutex);
    while (!m_queue.isEmpty()) {
        AVFrame *frame = m_queue.dequeue();
        av_frame_free(&frame);
    }
    m_finished = false;
    m_serial = 0;
    m_notFull.notify_all();
}

void FrameQueue::setFinished(bool finished)
{
    std::lock_guard lock(m_mutex);
    m_finished = finished;
    m_notEmpty.notify_all();
    m_notFull.notify_all();
}

bool FrameQueue::isFinished() const
{
    std::lock_guard lock(m_mutex);
    return m_finished;
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

int FrameQueue::serial() const
{
    std::lock_guard lock(m_mutex);
    return m_serial;
}
