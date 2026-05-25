#ifndef __IB_LOGGER_WRITER_H__
#define __IB_LOGGER_WRITER_H__

#include "backend/backend.h"

#include <atomic>
#include <memory>
#include <thread>

class ibLoggerQueue;
class ibLoggerSink;

// Background drain thread. Owns nothing — Queue + Sink are passed in by
// shared_ptr (caller holds them too). Loops Drain → WriteBatch until
// Stop is called and the queue empties. Never throws; sink failures are
// logged once via stderr.
class BACKEND_API ibLoggerWriter {
public:
    ibLoggerWriter(std::shared_ptr<ibLoggerQueue> queue,
                   std::shared_ptr<ibLoggerSink>  sink,
                   std::size_t maxBatch = 256,
                   int         maxWaitMs = 100);
    ~ibLoggerWriter();

    ibLoggerWriter(const ibLoggerWriter&)            = delete;
    ibLoggerWriter& operator=(const ibLoggerWriter&) = delete;

    void Start();
    // Signals the queue to stop, joins the thread. Idempotent.
    void Stop();

    bool IsRunning() const { return m_running.load(); }

private:
    void Run();

    std::shared_ptr<ibLoggerQueue> m_queue;
    std::shared_ptr<ibLoggerSink>  m_sink;
    std::size_t                    m_maxBatch;
    int                            m_maxWaitMs;
    std::thread                    m_thread;
    std::atomic<bool>              m_running{false};
    std::atomic<bool>              m_stop{false};
};

#endif
