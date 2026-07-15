#include "PacketQueue.h"
#include <QDebug>

PacketQueue::PacketQueue(int maxSize)
    : m_maxSize(maxSize)
    , m_finished(false)
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
    // 使用 wait_for 每 100ms 检查一次退出标志
    m_notFull.wait_for(lock, std::chrono::milliseconds(100), [this]() {
        return m_queue.size() < m_maxSize || m_finished || m_quitRequested.load();
    });

    if (m_finished || m_quitRequested.load()) {
        av_packet_free(&newPkt);
        return;
    }

    m_queue.enqueue(newPkt);
    m_notEmpty.notify_one();
}

void PacketQueue::requestQuit()
{
    m_quitRequested.store(true);
    // 唤醒所有等待的线程
    std::lock_guard lock(m_mutex);
    m_notEmpty.notify_all();
    m_notFull.notify_all();
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
    m_notEmpty.notify_all();
    m_notFull.notify_all();
}

int PacketQueue::size() const
{
    std::lock_guard lock(m_mutex);
    return m_queue.size();
}
