#ifndef __IB_LOGGER_READER_H__
#define __IB_LOGGER_READER_H__

#include "backend/backend.h"
#include "backend/logger/loggerEntry.h"

#include <wx/string.h>

#include <cstddef>
#include <vector>

// One row as returned by ibLoggerReader. Mirrors the schema on disk
// (.olg / log_entry table) one-to-one; the wxLongLong_t timestamp is
// raw ms-since-epoch — viewer formats with wxDateTime on display.
struct ibLogRow {
    wxLongLong_t ts_ms       = 0;
    int          level       = 0;
    wxString     session_id;
    wxString     user_name;
    wxString     host;
    wxString     source;
    wxString     event_type;
    wxString     message;
    wxString     ref_guid;
    int          ref_meta_id = 0;
};

// Filter passed to ibLoggerReader::Query / Count. Zero / empty fields
// mean "no constraint on this dimension". Combined with AND in SQL.
struct ibLogFilter {
    wxLongLong_t from_ms   = 0;     // inclusive; 0 = no lower bound
    wxLongLong_t to_ms     = 0;     // exclusive; 0 = no upper bound
    int          min_level = -1;    // -1 = any; otherwise rows with level >= this
    wxString     user_name;         // empty = any
    // ⭐⭐ ONE RUN'S OWN ROWS. Every row already carries `session_id` — it is written on the way in —
    // and a background job returns the session it runs as, so this is the two ends of an existing
    // number meeting. Without it, watching what one run is doing meant reading everybody's rows and
    // guessing which were whose; a background job cannot send its caller a message (its session is
    // tied to nobody), so the journal filtered by session IS the channel.
    wxString     session_id;        // empty = any
    wxString     source;            // empty = any
    wxString     event_type;        // empty = any
    wxString     ref_guid;          // drill-down by object identity
    wxString     search;            // substring in message (LIKE %...%)
    std::size_t  limit  = 1000;     // 0 = no cap
    std::size_t  offset = 0;
};

// Read-side facade. Opens .olg files in `dir` (no writes — sink owns
// writes, reader is purely a query surface). Each Query call walks
// only the files whose YYYY_MM falls within the filter's date range.
// Order: ts_ms DESC, id DESC (newest first).
class BACKEND_API ibLoggerReader {
public:
    explicit ibLoggerReader(const wxString& dir);

    std::vector<ibLogRow> Query(const ibLogFilter& filter);
    std::size_t           Count(const ibLogFilter& filter);

private:
    // Enumerate .olg files in m_dir whose (year, month) overlap with
    // the filter's [from_ms, to_ms) range. Sorted newest month first.
    std::vector<wxString> CollectFiles(const ibLogFilter& filter) const;

    wxString m_dir;
};

#endif
