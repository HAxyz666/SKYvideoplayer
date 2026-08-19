#include "PacketQueue.h"

// 静态哨兵：零初始化，data == nullptr，仅作为冲刷标记，不承载数据。
AVPacket PacketQueue::s_flushMarker;

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
        return m_queue.size() < m_maxSize || m_finished
            || m_quitRequested.load() || m_seekNotify.load();
    });

    // seek 请求已到达：丢弃在途包（内容在 seek 之前），让出队列供冲刷。
    if (m_finished || m_quitRequested.load() || m_seekNotify.exchange(false)) {
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
    drain(false);
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
    drain(true);
}

// 清空队列并释放非哨兵包。notifyEmpty 用于唤醒等待 pop 的线程
// （flush 后等待者应返回；clear 仅用于析构/预清空，不唤醒）。
void PacketQueue::drain(bool notifyEmpty)
{
    std::lock_guard lock(m_mutex);
    while (!m_queue.isEmpty()) {
        AVPacket *pkt = m_queue.dequeue();
        if (pkt != &s_flushMarker)
            av_packet_free(&pkt);
    }
    m_notFull.notify_all();
    if (notifyEmpty)
        m_notEmpty.notify_all();
}

void PacketQueue::pushFlushMarker()
{
    std::lock_guard lock(m_mutex);
    if (m_finished || m_quitRequested.load())
        return;
    m_queue.enqueue(&s_flushMarker);
    m_notEmpty.notify_one();
}

void PacketQueue::notifySeekWaiters()
{
    m_seekNotify.store(true, std::memory_order_release);
    std::lock_guard lock(m_mutex);
    m_notFull.notify_all();
}

void PacketQueue::clearSeekNotify()
{
    m_seekNotify.store(false, std::memory_order_release);
}

bool PacketQueue::isFlushMarker(const AVPacket *pkt)
{
    return pkt == &s_flushMarker;
}

int PacketQueue::size() const
{
    std::lock_guard lock(m_mutex);
    return m_queue.size();
}
