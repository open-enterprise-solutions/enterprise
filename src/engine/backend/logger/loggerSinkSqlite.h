#ifndef __IB_LOGGER_SINK_SQLITE_H__
#define __IB_LOGGER_SINK_SQLITE_H__

#include "backend/backend.h"
#include "backend/logger/loggerSink.h"

#include <wx/string.h>
#include <wx/datetime.h>

#include <memory>

class ibDatabaseLayerSQLite;

// SQLite-backed sink. Files: <dir>/oes_YYYY_MM.olg, rotated on month
// boundary inside WriteBatch. Holds its own ibDatabaseLayerSQLite — not
// part of the main connection pool. WAL mode for concurrent reads
// (admin viewer running while writer thread appends).
class BACKEND_API ibLoggerSinkSqlite : public ibLoggerSink {
public:
    explicit ibLoggerSinkSqlite(const wxString& dir);
    ~ibLoggerSinkSqlite() override;

    void WriteBatch(const std::vector<ibLogEntry>& batch) override;
    void Flush() override;

private:
    bool     EnsureOpenForMonth(int year, int month);
    void     CloseCurrent();
    bool     EnsureSchema();
    wxString BuildPath(int year, int month) const;

    wxString m_dir;
    int      m_currentYear  = 0;
    int      m_currentMonth = 0;
    std::shared_ptr<ibDatabaseLayerSQLite> m_db;
};

#endif
