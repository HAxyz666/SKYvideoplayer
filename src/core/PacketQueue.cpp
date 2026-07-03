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
