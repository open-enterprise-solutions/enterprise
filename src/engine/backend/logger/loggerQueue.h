#ifndef __IB_LOGGER_QUEUE_H__
#define __IB_LOGGER_QUEUE_H__

#include "backend/backend.h"
#include "backend/logger/loggerEntry.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

// Multi-producer / single-consumer bounded queue. Producers (any thread
// calling ibLogger::Info/Audit/...) push non-blocking; on overflow the
// new entry is dropped and a counter ticks up. The writer thread drains
// in batches and reports drops as one self-log row per drain.
class BACKEND_API ibLoggerQueue {
public:
    explicit ibLoggerQueue(std::size_t maxEntries = 100000);
    ~ibLoggerQueue() = default;

    ibLoggerQueue(const ibLoggerQueue&)            = delete;
    ibLoggerQueue& operator=(const ibLoggerQueue&) = delete;

    void Push(ibLogEntry&& entry);

    // Drains at most `maxBatch` entries into `out`. Blocks up to
    // `maxWaitMs` if the queue is empty. Returns the number of entries
    // actually moved. Returns 0 on shutdown with an empty queue.
    std::size_t Drain(std::vector<ibLogEntry>& out,
                      std::size_t maxBatch,
                      int maxWaitMs);

    std::size_t TakeDropped();

    // Wakes the writer; subsequent Push calls silently drop.
    void Stop();

    std::size_t Size() const;

private:
    mutable std::mutex      m_mutex;
    std::condition_variable m_cv;
    std::deque<ibLogEntry>  m_queue;
    std::size_t             m_maxEntries;
    std::size_t             m_dropped = 0;
    bool                    m_stop    = false;
};

#endif
