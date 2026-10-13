#ifndef _FRMCLIENT_BACKEND_LOGGER_READER_H__
#define _FRMCLIENT_BACKEND_LOGGER_READER_H__

#include <wx/string.h>

#include <cstddef>
#include <vector>

// One row as returned by ibLoggerReader — the engine's (backend/logger/loggerReader.h), as the server's journal sends
// it: the raw ms-since-epoch timestamp — viewer formats with wxDateTime on display.
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

// Filter passed to ibLoggerReader::Query. Zero / empty fields mean "no constraint on this dimension". Combined with
// AND on the server.
struct ibLogFilter {
    wxLongLong_t from_ms   = 0;     // inclusive; 0 = no lower bound
    wxLongLong_t to_ms     = 0;     // exclusive; 0 = no upper bound
    int          min_level = -1;    // -1 = any; otherwise rows with level >= this
    wxString     user_name;         // empty = any
    wxString     session_id;        // empty = any
    wxString     source;            // empty = any
    wxString     event_type;        // empty = any
    wxString     ref_guid;          // drill-down by object identity
    wxString     search;            // substring in message
    std::size_t  limit  = 1000;     // 0 = no cap
    std::size_t  offset = 0;
};

// THE ENGINE'S READER (backend/logger/loggerReader.h) as the client's journal asks it: the journal is the server's,
// read through the tab that shows it — fetch {Tab, Request: the filter}. Order: newest first.
class ibLoggerReader {
public:
    explicit ibLoggerReader(long long tabId) : m_tabId(tabId) {}

    std::vector<ibLogRow> Query(const ibLogFilter& filter);

private:
    const long long m_tabId;
};

#endif
