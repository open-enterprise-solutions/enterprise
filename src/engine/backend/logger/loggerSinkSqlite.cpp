#include "backend/logger/loggerSinkSqlite.h"

#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/preparedStatement.h"

#include <wx/filename.h>
#include <wx/log.h>

ibLoggerSinkSqlite::ibLoggerSinkSqlite(const wxString& dir)
    : m_dir(dir)
{
    if (!m_dir.IsEmpty() && !wxFileName::DirExists(m_dir))
        wxFileName::Mkdir(m_dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
}

ibLoggerSinkSqlite::~ibLoggerSinkSqlite()
{
    CloseCurrent();
}

wxString ibLoggerSinkSqlite::BuildPath(int year, int month) const
{
    const wxString name = wxString::Format(wxT("oes_%04d_%02d.olg"), year, month);
    return m_dir + wxFileName::GetPathSeparator() + name;
}

bool ibLoggerSinkSqlite::EnsureSchema()
{
    if (!m_db || !m_db->IsOpen()) return false;
    try {
        m_db->RunQuery(wxT(
            "CREATE TABLE IF NOT EXISTS log_entry ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " ts_ms INTEGER NOT NULL,"
            " level INTEGER NOT NULL,"
            " session_id TEXT,"
            " user_name TEXT,"
            " host TEXT,"
            " source TEXT NOT NULL,"
            " event_type TEXT NOT NULL,"
            " message TEXT,"
            " ref_guid TEXT,"
            " ref_meta_id INTEGER,"
            " details BLOB)"));
        m_db->RunQuery(wxT("CREATE INDEX IF NOT EXISTS ix_log_ts ON log_entry(ts_ms)"));
        m_db->RunQuery(wxT("CREATE INDEX IF NOT EXISTS ix_log_level_ts ON log_entry(level, ts_ms)"));
        // Drill-down: "every audit row touching this object" — viewer
        // joins on ref_guid when admin clicks on a record-source row.
        m_db->RunQuery(wxT("CREATE INDEX IF NOT EXISTS ix_log_ref ON log_entry(ref_guid)"));
        m_db->RunQuery(wxT("PRAGMA journal_mode=WAL"));
        m_db->RunQuery(wxT("PRAGMA synchronous=NORMAL"));
        return true;
    } catch (...) {
        return false;
    }
}

void ibLoggerSinkSqlite::CloseCurrent()
{
    if (m_db) {
        try { m_db->Close(); } catch (...) {}
        m_db.reset();
    }
    m_currentYear  = 0;
    m_currentMonth = 0;
}

bool ibLoggerSinkSqlite::EnsureOpenForMonth(int year, int month)
{
    if (m_db && m_db->IsOpen()
        && m_currentYear == year && m_currentMonth == month)
        return true;

    CloseCurrent();
    m_db = std::make_shared<ibDatabaseLayerSQLite>();
    const wxString path = BuildPath(year, month);
    if (!m_db->Open(path)) {
        m_db.reset();
        return false;
    }
    if (!EnsureSchema()) {
        CloseCurrent();
        return false;
    }
    m_currentYear  = year;
    m_currentMonth = month;
    return true;
}

void ibLoggerSinkSqlite::WriteBatch(const std::vector<ibLogEntry>& batch)
{
    if (batch.empty()) return;

    // Pick rotation bucket from the first entry's timestamp. Within one
    // drain batches are tiny enough that month rollover mid-batch is
    // not worth handling — those few entries land in the prior month.
    wxDateTime ts((time_t)(batch.front().ts_ms / 1000));
    const int year  = ts.GetYear();
    const int month = static_cast<int>(ts.GetMonth()) + 1;   // 0-based enum
    if (!EnsureOpenForMonth(year, month)) return;

    // Use raw BEGIN/COMMIT/ROLLBACK instead of the ibDatabaseLayer
    // BeginTransaction() wrapper — the wrapper calls SetActiveTxConnection
    // which pushes this layer into the main ibConnectionPool's m_entries.
    // The pool then hands our SQLite conn back to ses_query callers,
    // who run Firebird-named queries (Document<metaID>) against it —
    // "no such table: Document1058". Our SQLite is a private connection
    // that must stay outside the main pool's bookkeeping.
    bool inTx = false;
    try {
        m_db->RunQuery(wxT("BEGIN"));
        inTx = true;
        ibPreparedStatement* raw = m_db->PrepareStatement(wxT(
            "INSERT INTO log_entry (ts_ms, level, session_id, user_name, host,"
            " source, event_type, message, ref_guid, ref_meta_id, details)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        if (raw == nullptr) {
            m_db->RunQuery(wxT("ROLLBACK"));
            return;
        }
        ibStatementGuard stmt(m_db.get(), raw);
        for (const ibLogEntry& e : batch) {
            // SQLite has no SetParamInt64 here; ms-resolution timestamps
            // fit a double's 53-bit mantissa until year 285k.
            raw->SetParamDouble(1, static_cast<double>(e.ts_ms));
            raw->SetParamInt   (2, static_cast<int>(e.level));
            raw->SetParamString(3, e.session_id);
            raw->SetParamString(4, e.user_name);
            raw->SetParamString(5, e.host);
            raw->SetParamString(6, e.source);
            raw->SetParamString(7, e.event_type);
            raw->SetParamString(8, e.message);
            if (e.ref_guid.IsEmpty()) raw->SetParamNull(9);
            else                      raw->SetParamString(9, e.ref_guid);
            if (e.ref_meta_id == 0)   raw->SetParamNull(10);
            else                      raw->SetParamInt(10, e.ref_meta_id);
            if (e.details.GetDataLen() > 0)
                raw->SetParamBlob(11, e.details);
            else
                raw->SetParamNull(11);
            raw->RunQuery();
        }
        m_db->RunQuery(wxT("COMMIT"));
        inTx = false;
    } catch (...) {
        if (inTx) {
            try { m_db->RunQuery(wxT("ROLLBACK")); } catch (...) {}
        }
    }
}

void ibLoggerSinkSqlite::Flush()
{
    if (!m_db || !m_db->IsOpen()) return;
    try { m_db->RunQuery(wxT("PRAGMA wal_checkpoint(PASSIVE)")); } catch (...) {}
}
