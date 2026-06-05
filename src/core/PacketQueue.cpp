#include "PacketQueue.h"
#include <QMutexLocker>

PacketQueue::PacketQueue(int maxSize)
    : m_maxSize(maxSize)
    , m_finished(false)
    , m_serial(0)
{
}

PacketQueue::~PacketQueue()
{
    clear();
}

void PacketQueue::push(AVPacket *pkt)
{
    QMutexLocker locker(&m_mutex);
    while (m_queue.size() >= m_maxSize && !m_finished) {
        m_notFull.wait(&m_mutex);
    }
    if (m_finished) {
        av_packet_free(&pkt);
        return;
    }
    m_queue.enqueue(av_packet_clone(pkt));
    m_notEmpty.wakeAll();
}

AVPacket *PacketQueue::pop()
{
    QMutexLocker locker(&m_mutex);
    while (m_queue.isEmpty() && !m_finished) {
        m_notEmpty.wait(&m_mutex);
    }
    if (m_finished && m_queue.isEmpty()) {
        return nullptr;
    }
    AVPacket *pkt = m_queue.dequeue();
    m_notFull.wakeAll();
    return pkt;
}

AVPacket *PacketQueue::tryPop(int timeoutMs)
{
    QMutexLocker locker(&m_mutex);
    if (m_queue.isEmpty() && !m_finished) {
        m_notEmpty.wait(&m_mutex, timeoutMs);
    }
    if (m_finished || m_queue.isEmpty()) {
        return nullptr;
    }
    AVPacket *pkt = m_queue.dequeue();
    m_notFull.wakeAll();
    return pkt;
}

int PacketQueue::size() const
{
    QMutexLocker locker(&m_mutex);
    return m_queue.size();
}

bool PacketQueue::isFull() const
{
    QMutexLocker locker(&m_mutex);
    return m_queue.size() >= m_maxSize;
}

bool PacketQueue::isEmpty() const
{
    QMutexLocker locker(&m_mutex);
    return m_queue.isEmpty();
}

void PacketQueue::clear()
{
    QMutexLocker locker(&m_mutex);
    while (!m_queue.isEmpty()) {
        AVPacket *pkt = m_queue.dequeue();
        av_packet_free(&pkt);
    }
    m_finished = false;
    m_serial = 0;
}

void PacketQueue::setFinished(bool finished)
{
    QMutexLocker locker(&m_mutex);
    m_finished = finished;
    m_notEmpty.wakeAll();
    m_notFull.wakeAll();
}

bool PacketQueue::isFinished() const
{
    QMutexLocker locker(&m_mutex);
    return m_finished;
}

void PacketQueue::flush()
{
    QMutexLocker locker(&m_mutex);
    while (!m_queue.isEmpty()) {
        AVPacket *pkt = m_queue.dequeue();
        av_packet_free(&pkt);
    }
    m_serial++;
    m_notEmpty.wakeAll();
    m_notFull.wakeAll();
}

int PacketQueue::serial() const
{
    QMutexLocker locker(&m_mutex);
    return m_serial;
}

void PacketQueue::incrementSerial()
{
    QMutexLocker locker(&m_mutex);
    m_serial++;
}