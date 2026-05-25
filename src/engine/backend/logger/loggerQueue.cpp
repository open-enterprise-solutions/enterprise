#include "backend/logger/loggerQueue.h"

#include <algorithm>
#include <chrono>

ibLoggerQueue::ibLoggerQueue(std::size_t maxEntries)
    : m_maxEntries(maxEntries) {}

void ibLoggerQueue::Push(ibLogEntry&& entry)
{
    bool wasEmpty;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_stop) return;
        if (m_queue.size() >= m_maxEntries) {
            ++m_dropped;
            return;
        }
        wasEmpty = m_queue.empty();
        m_queue.push_back(std::move(entry));
    }
    // Wake the writer only on the empty → non-empty transition. When
    // the queue already has entries, the writer is either mid-drain
    // (will pick this up on its next Drain call) or about to wake on
    // the previous notify. Avoids one syscall per Push under load.
    if (wasEmpty) m_cv.notify_one();
}

std::size_t ibLoggerQueue::Drain(std::vector<ibLogEntry>& out,
                                 std::size_t maxBatch,
                                 int maxWaitMs)
{
    std::unique_lock<std::mutex> lk(m_mutex);
    if (m_queue.empty() && !m_stop) {
        m_cv.wait_for(lk, std::chrono::milliseconds(maxWaitMs),
                      [this] { return !m_queue.empty() || m_stop; });
    }
    const std::size_t take = std::min(maxBatch, m_queue.size());
    out.reserve(out.size() + take);
    for (std::size_t i = 0; i < take; ++i) {
        out.push_back(std::move(m_queue.front()));
        m_queue.pop_front();
    }
    return take;
}

std::size_t ibLoggerQueue::TakeDropped()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    const std::size_t n = m_dropped;
    m_dropped = 0;
    return n;
}

void ibLoggerQueue::Stop()
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
}

std::size_t ibLoggerQueue::Size() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_queue.size();
}
