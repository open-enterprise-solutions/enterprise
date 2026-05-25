#ifndef __IB_LOGGER_H__
#define __IB_LOGGER_H__

#include "backend/backend.h"
#include "backend/logger/loggerEntry.h"

#include <wx/string.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

class ibLoggerQueue;
class ibLoggerSink;
class ibLoggerWriter;
class ibValue;

// ibLogger — central audit + trace facade.
//
// One per process, owned by ibApplicationData. Callers go through the
// `ibLog` macro: `ibLog->Audit("auth", "login", _("ok"))`.
//
// Routing:
//   - All entries go to the local SQLite sink (.olg files). In file-mode
//     the directory is next to the database file; in server-mode it's
//     under %LOCALAPPDATA%\OES\<config>\logs.
//   - PG / network sinks are deferred until compute-server arrives —
//     until then admins view a client's journal locally on that client.
//
// Methods never throw. Backend-internal errors are swallowed and a
// dropped-count surfaces as a self-log row from the writer.
class BACKEND_API ibLogger {
public:
    // dir = absolute path to the .olg directory. Created if missing.
    explicit ibLogger(const wxString& dir,
                      std::size_t maxQueue = 100000,
                      std::size_t maxBatch = 256,
                      int         maxWaitMs = 100);
    ~ibLogger();

    // Below-threshold entries are dropped before any formatting work
    // runs in the caller — callers should branch on IsEnabled(level)
    // before building the message string at hot sites. Audit is always
    // enabled regardless of m_minLevel (business events never silenced).
    void SetMinLevel(ibLogLevel level) { m_minLevel = level; }
    bool IsEnabled(ibLogLevel level) const {
        return level == ibLogLevel::Audit
            || static_cast<int>(level) >= static_cast<int>(m_minLevel);
    }

    ibLogger(const ibLogger&)            = delete;
    ibLogger& operator=(const ibLogger&) = delete;

    // Audit — business events (login / document write / DDL apply / ...).
    // This is the "журнал регистрации" surface.
    void Audit(const wxString& source,
               const wxString& event_type,
               const wxString& message);
    void Audit(const wxString& source,
               const wxString& event_type,
               const wxString& message,
               const ibValue&  details);

    // Object-bound audit: viewer uses (refMetaId, refGuid) to drill
    // from the journal row back to the live record. refGuid is the
    // object's guid in string form (ibGuid::str()); refMetaId is the
    // ibMetaID of the catalog/document type. Empty refGuid degrades
    // to the non-ref overload — admins still see the row, just
    // without drill-down.
    void Audit(const wxString& source,
               const wxString& event_type,
               const wxString& message,
               const wxString& refGuid,
               int             refMetaId);

    // Trace — technical events for the developer.
    void Info (const wxString& source, const wxString& event_type, const wxString& message);
    void Warn (const wxString& source, const wxString& event_type, const wxString& message);
    void Error(const wxString& source, const wxString& event_type, const wxString& message);

    const wxString& GetLogDir() const { return m_dir; }

    // Start a background thread that runs ibLoggerSweep::RunOnce every
    // ~24h. retentionDays = how old .olg files must be before deletion.
    // Idempotent — second call no-ops. Stopped automatically in dtor.
    void StartDailySweep(int retentionDays);

    // Block until the writer has drained the current queue. Used by tests
    // and by ~ibApplicationData to flush before pool shutdown.
    void FlushBlocking();

private:
    void Emit(ibLogLevel level,
              const wxString& source,
              const wxString& event_type,
              const wxString& message,
              const ibValue*  details,
              const wxString* refGuid   = nullptr,
              int             refMetaId = 0);

    static wxLongLong_t NowMillis();
    static wxString     HostName();

    void SweepLoop();

    wxString                          m_dir;
    std::shared_ptr<ibLoggerQueue>    m_queue;
    std::shared_ptr<ibLoggerSink>     m_sink;
    std::unique_ptr<ibLoggerWriter>   m_writer;
    ibLogLevel                        m_minLevel = ibLogLevel::Info;

    // Daily sweep background — owned here so the thread joins inside
    // ~ibLogger before the sink dies.
    std::thread                       m_sweepThread;
    std::mutex                        m_sweepMutex;
    std::condition_variable           m_sweepCv;
    std::atomic<bool>                 m_sweepStop{false};
    int                               m_sweepRetentionDays = 0;
};

#endif
