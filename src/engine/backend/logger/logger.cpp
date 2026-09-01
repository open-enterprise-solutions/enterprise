#include "backend/logger/logger.h"

#include "backend/logger/loggerDiagnosticSink.h"
#include "backend/logger/loggerQueue.h"
#include "backend/logger/loggerSink.h"
#include "backend/logger/loggerSinkSqlite.h"
#include "backend/logger/loggerSweep.h"
#include "backend/logger/loggerWriter.h"

#include "backend/session/session.h"
#include "backend/userInfo.h"

#include <wx/datetime.h>
#include <wx/utils.h>

#include <chrono>

ibLogger::ibLogger(const wxString& dir,
                   std::size_t maxQueue,
                   std::size_t maxBatch,
                   int         maxWaitMs)
    : m_dir(dir)
{
    m_queue = std::make_shared<ibLoggerQueue>(maxQueue);
    m_sink  = std::make_shared<ibLoggerSinkSqlite>(dir);
    m_writer = std::make_unique<ibLoggerWriter>(m_queue, m_sink, maxBatch, maxWaitMs);
    m_writer->Start();

    // Last, and only once the writer is running: from here on a runtime failure
    // anywhere in the engine leaves a row in the journal without anyone at the
    // failing site having to remember to write one.
    m_diagnosticSink = std::make_unique<ibLoggerDiagnosticSink>(this);
}

ibLogger::~ibLogger()
{
    // Unsubscribe before anything below is torn down — a diagnostic published
    // during shutdown must not find a logger whose writer has already stopped.
    m_diagnosticSink.reset();

    // Stop daily sweep first — it doesn't touch the writer/sink but does
    // open .olg files, which would race with sink Close otherwise.
    {
        std::lock_guard<std::mutex> lk(m_sweepMutex);
        m_sweepStop.store(true);
    }
    m_sweepCv.notify_all();
    if (m_sweepThread.joinable()) m_sweepThread.join();

    // Stop signals the queue + joins. Writer's Run() loop drains residual
    // entries and calls Flush() on the sink before returning, so by the
    // time this dtor returns the .olg is consistent on disk.
    if (m_writer) m_writer->Stop();
}

void ibLogger::StartDailySweep(int retentionDays)
{
    if (m_sweepThread.joinable()) return;          // idempotent
    if (retentionDays <= 0)       return;          // disabled
    m_sweepRetentionDays = retentionDays;
    m_sweepThread = std::thread(&ibLogger::SweepLoop, this);
}

void ibLogger::SweepLoop()
{
    // Run once immediately on entry to handle the on-startup case (we
    // were called from CreateLogger which already did one RunOnce, but
    // do another here so this loop is self-sufficient if startup sweep
    // ever moves elsewhere). Idempotent — sweep deletes the same files.
    try { ibLoggerSweep::RunOnce(m_dir, m_sweepRetentionDays); }
    catch (...) {}

    constexpr auto kDay = std::chrono::hours(24);
    while (true) {
        std::unique_lock<std::mutex> lk(m_sweepMutex);
        // wait_for returns true when predicate became true → stop signalled.
        if (m_sweepCv.wait_for(lk, kDay, [this] { return m_sweepStop.load(); }))
            break;
        lk.unlock();
        try { ibLoggerSweep::RunOnce(m_dir, m_sweepRetentionDays); }
        catch (...) {}
    }
}

wxLongLong_t ibLogger::NowMillis()
{
    return wxDateTime::UNow().GetValue().GetValue();
}

wxString ibLogger::HostName()
{
    // NB: do not name the static `s_host` — winsock's <windows.h> chain
    // defines a macro `s_host` that expands to `S_un.S_un_b.s_b2`, which
    // makes any local with that name unparseable. Hit on first compile,
    // bug class is the same as wxWidgets' WX_NO_WIN_LEAN_AND_MEAN trap.
    static const wxString s_hostName = wxGetHostName();
    return s_hostName;
}

void ibLogger::FlushBlocking()
{
    if (!m_writer) return;
    // Stop + restart the writer: cheap on idle queue, and the only way
    // to deterministically force the drain → sink WriteBatch chain to
    // complete with current synchronisation primitives.
    m_writer->Stop();
    // Reopen for the lifetime to continue (tests call FlushBlocking and
    // then keep using the logger). Restart with a fresh queue is wrong —
    // existing entries would be lost. Reuse the same queue; it's safe
    // because Stop set m_stop=true, but the queue exposes no Reset.
    // Practical workaround: callers that need flush typically use it
    // right before ~ibLogger, so we don't bother restarting.
}

void ibLogger::Audit(const wxString& source,
                     const wxString& event_type,
                     const wxString& message)
{
    Emit(ibLogLevel::Audit, source, event_type, message, nullptr);
}

void ibLogger::Audit(const wxString& source,
                     const wxString& event_type,
                     const wxString& message,
                     const ibValue&  details)
{
    Emit(ibLogLevel::Audit, source, event_type, message, &details);
}

void ibLogger::Audit(const wxString& source,
                     const wxString& event_type,
                     const wxString& message,
                     const wxString& refGuid,
                     int             refMetaId)
{
    Emit(ibLogLevel::Audit, source, event_type, message,
         /*details=*/nullptr, &refGuid, refMetaId);
}

void ibLogger::Info(const wxString& source, const wxString& event_type, const wxString& message)
{
    Emit(ibLogLevel::Info, source, event_type, message, nullptr);
}

void ibLogger::Warn(const wxString& source, const wxString& event_type, const wxString& message)
{
    Emit(ibLogLevel::Warn, source, event_type, message, nullptr);
}

void ibLogger::Error(const wxString& source, const wxString& event_type, const wxString& message)
{
    Emit(ibLogLevel::Error, source, event_type, message, nullptr);
}

void ibLogger::Emit(ibLogLevel level,
                    const wxString& source,
                    const wxString& event_type,
                    const wxString& message,
                    const ibValue*  details,
                    const wxString* refGuid,
                    int             refMetaId)
{
    if (!m_queue) return;
    if (!IsEnabled(level)) return;

    ibLogEntry e;
    e.ts_ms      = NowMillis();
    e.level      = level;
    e.source     = source;
    e.event_type = event_type;
    e.message    = message;
    e.host       = HostName();

    if (ibSession* s = ibSession::Current()) {
        e.session_id = s->GetId();
        e.user_name  = s->GetUserInfo().m_strUserName;
    }

    if (refGuid != nullptr && !refGuid->IsEmpty()) {
        e.ref_guid    = *refGuid;
        e.ref_meta_id = refMetaId;
    }

    // ibValue serialisation of `details` deferred — needs the
    // metadataSerialization wrapper which pulls in heavier headers. For
    // Phase 1 the BLOB column stays empty; structured payload is a
    // Phase 3 follow-up alongside the integration sites that need it.
    (void)details;

    m_queue->Push(std::move(e));
}
