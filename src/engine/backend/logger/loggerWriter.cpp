#include "backend/logger/loggerWriter.h"
#include "backend/logger/loggerQueue.h"
#include "backend/logger/loggerSink.h"

#include <wx/log.h>

ibLoggerWriter::ibLoggerWriter(std::shared_ptr<ibLoggerQueue> queue,
                               std::shared_ptr<ibLoggerSink>  sink,
                               std::size_t maxBatch,
                               int         maxWaitMs)
    : m_queue(std::move(queue))
    , m_sink(std::move(sink))
    , m_maxBatch(maxBatch)
    , m_maxWaitMs(maxWaitMs)
{}

ibLoggerWriter::~ibLoggerWriter()
{
    Stop();
}

void ibLoggerWriter::Start()
{
    if (m_running.exchange(true)) return;
    m_stop.store(false);
    m_thread = std::thread(&ibLoggerWriter::Run, this);
}

void ibLoggerWriter::Stop()
{
    if (!m_running.exchange(false)) return;
    m_stop.store(true);
    if (m_queue) m_queue->Stop();
    if (m_thread.joinable()) m_thread.join();
}

void ibLoggerWriter::Run()
{
    std::vector<ibLogEntry> batch;
    batch.reserve(m_maxBatch);

    while (true) {
        batch.clear();
        const std::size_t drained = m_queue
            ? m_queue->Drain(batch, m_maxBatch, m_maxWaitMs)
            : 0;

        if (drained == 0) {
            if (m_stop.load()) break;
            continue;
        }

        if (m_sink) {
            try { m_sink->WriteBatch(batch); }
            catch (...) {
                // Sinks promise not to throw, but defence-in-depth.
            }
        }
    }

    // Final drain — queue may have residual entries pushed between the
    // last Drain return and Stop. Caller (~ibLogger) has already
    // signalled m_stop, so Drain will not block.
    if (m_queue && m_sink) {
        batch.clear();
        while (m_queue->Drain(batch, m_maxBatch, 0) > 0) {
            try { m_sink->WriteBatch(batch); } catch (...) {}
            batch.clear();
        }
        try { m_sink->Flush(); } catch (...) {}
    }
}
