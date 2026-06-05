#include "FrameQueue.h"
#include <QMutexLocker>

FrameQueue::FrameQueue(int maxSize)
    : m_maxSize(maxSize)
    , m_finished(false)
{
}

FrameQueue::~FrameQueue()
{
    clear();
}

void FrameQueue::push(AVFrame *frame)
{
    QMutexLocker locker(&m_mutex);
    while (m_queue.size() >= m_maxSize && !m_finished) {
        m_notFull.wait(&m_mutex);
    }
    if (m_finished) {
        av_frame_free(&frame);
        return;
    }
    m_queue.enqueue(av_frame_clone(frame));
    m_notEmpty.wakeAll();
}

AVFrame *FrameQueue::pop()
{
    QMutexLocker locker(&m_mutex);
    while (m_queue.isEmpty() && !m_finished) {
        m_notEmpty.wait(&m_mutex);
    }
    if (m_finished && m_queue.isEmpty()) {
        return nullptr;
    }
    AVFrame *frame = m_queue.dequeue();
    m_notFull.wakeAll();
    return frame;
}

AVFrame *FrameQueue::tryPop(int timeoutMs)
{
    QMutexLocker locker(&m_mutex);
    if (m_queue.isEmpty() && !m_finished) {
        m_notEmpty.wait(&m_mutex, timeoutMs);
    }
    if (m_finished || m_queue.isEmpty()) {
        return nullptr;
    }
    AVFrame *frame = m_queue.dequeue();
    m_notFull.wakeAll();
    return frame;
}

AVFrame *FrameQueue::peek()
{
    QMutexLocker locker(&m_mutex);
    if (m_queue.isEmpty()) {
        return nullptr;
    }
    return m_queue.head();
}

int FrameQueue::size() const
{
    QMutexLocker locker(&m_mutex);
    return m_queue.size();
}

bool FrameQueue::isFull() const
{
    QMutexLocker locker(&m_mutex);
    return m_queue.size() >= m_maxSize;
}

bool FrameQueue::isEmpty() const
{
    QMutexLocker locker(&m_mutex);
    return m_queue.isEmpty();
}

void FrameQueue::clear()
{
    QMutexLocker locker(&m_mutex);
    while (!m_queue.isEmpty()) {
        AVFrame *frame = m_queue.dequeue();
        av_frame_free(&frame);
    }
    m_finished = false;
}

void FrameQueue::setFinished(bool finished)
{
    QMutexLocker locker(&m_mutex);
    m_finished = finished;
    m_notEmpty.wakeAll();
    m_notFull.wakeAll();
}

bool FrameQueue::isFinished() const
{
    QMutexLocker locker(&m_mutex);
    return m_finished;
}

void FrameQueue::flush()
{
    QMutexLocker locker(&m_mutex);
    while (!m_queue.isEmpty()) {
        AVFrame *frame = m_queue.dequeue();
        av_frame_free(&frame);
    }
    m_notEmpty.wakeAll();
    m_notFull.wakeAll();
}
