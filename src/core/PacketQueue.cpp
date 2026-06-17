#include "PacketQueue.h"
#include <QDebug>

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
    AVPacket *newPkt = av_packet_clone(pkt);
    if (!newPkt)
        return;

    std::unique_lock lock(m_mutex);
    m_notFull.wait(lock, [this]() { return m_queue.size() < m_maxSize || m_finished; });

    if (m_finished) {
        av_packet_free(&newPkt);
        return;
    }

    m_queue.enqueue(newPkt);
    m_notEmpty.notify_one();
}

AVPacket *PacketQueue::pop()
{
    std::unique_lock lock(m_mutex);
    m_notEmpty.wait(lock, [this]() { return !m_queue.isEmpty() || m_finished; });

    if (m_queue.isEmpty())
        return nullptr;

    AVPacket *pkt = m_queue.dequeue();
    m_notFull.notify_one();
    return pkt;
}

AVPacket *PacketQueue::tryPop(int timeoutMs)
{
    std::unique_lock lock(m_mutex);
    if (!m_notEmpty.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                             [this]() { return !m_queue.isEmpty() || m_finished; }))
        return nullptr;

    if (m_queue.isEmpty())
        return nullptr;

    AVPacket *pkt = m_queue.dequeue();
    m_notFull.notify_one();
    return pkt;
}

int PacketQueue::size() const
{
    std::lock_guard lock(m_mutex);
    return m_queue.size();
}

bool PacketQueue::isFull() const
{
    std::lock_guard lock(m_mutex);
    return m_queue.size() >= m_maxSize;
}

bool PacketQueue::isEmpty() const
{
    std::lock_guard lock(m_mutex);
    return m_queue.isEmpty();
}

void PacketQueue::clear()
{
    std::lock_guard lock(m_mutex);
    while (!m_queue.isEmpty()) {
        AVPacket *pkt = m_queue.dequeue();
        av_packet_free(&pkt);
    }
    m_notFull.notify_all();
}

void PacketQueue::setFinished(bool finished)
{
    std::lock_guard lock(m_mutex);
    m_finished = finished;
    m_notEmpty.notify_all();
    m_notFull.notify_all();
}

bool PacketQueue::isFinished() const
{
    std::lock_guard lock(m_mutex);
    return m_finished;
}

void PacketQueue::flush()
{
    std::lock_guard lock(m_mutex);
    while (!m_queue.isEmpty()) {
        AVPacket *pkt = m_queue.dequeue();
        av_packet_free(&pkt);
    }
    m_serial++;
    m_notEmpty.notify_all();
    m_notFull.notify_all();
}

int PacketQueue::serial() const
{
    std::lock_guard lock(m_mutex);
    return m_serial;
}

void PacketQueue::incrementSerial()
{
    std::lock_guard lock(m_mutex);
    ++m_serial;
}
