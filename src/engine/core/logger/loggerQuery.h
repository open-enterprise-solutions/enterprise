#ifndef __LOGGER_QUERY_H__
#define __LOGGER_QUERY_H__

////////////////////////////////////////////////////////////////////////////
//	Description : A JOURNAL QUERY - what the reader is asked, and one row it answers with
////////////////////////////////////////////////////////////////////////////
//
// The reader is the server's (backend/logger/loggerReader.h: it opens the .olg files); the journal window
// asks it — the desktop's beside it, the client's through the tab that shows it. The question and the row
// are one shape for both, so they live here.

#include "core/memory/memoryBuffer.h"

#include <cstddef>

// One row as returned by ibLoggerReader. Mirrors the schema on disk
// (.olg / log_entry table) one-to-one; the wxLongLong_t timestamp is
// raw ms-since-epoch — viewer formats with wxDateTime on display.
struct ibLogRow {
    long long ts_ms       = 0;
    int          level       = 0;
    ibString     session_id;
    ibString     user_name;
    ibString     host;
    ibString     source;
    ibString     event_type;
    ibString     message;
    ibString     ref_guid;
    int          ref_meta_id = 0;
    // …AND THE STRUCTURED PAYLOAD THE ENTRY CARRIED, as it was stored: a node written by
    // ibBinaryProvider. Empty for the rows that carried none, which is most of them.
    //
    // 🛑 IT WAS WRITTEN AND NEVER SELECTED. The column has existed since 2026-06-28 and the reader's
    // SELECT stopped one field short of it, so even after the writer was fixed the value would have
    // gone into a column nobody reads — the same defect twice, at the two ends of one row.
    ibMemoryBuffer details;
};

// Filter passed to ibLoggerReader::Query / Count. Zero / empty fields
// mean "no constraint on this dimension". Combined with AND by the reader.
struct ibLogFilter {
    long long from_ms   = 0;     // inclusive; 0 = no lower bound
    long long to_ms     = 0;     // exclusive; 0 = no upper bound
    int          min_level = -1;    // -1 = any; otherwise rows with level >= this
    ibString     user_name;         // empty = any
    // ⭐⭐ ONE RUN'S OWN ROWS. Every row already carries `session_id` — it is written on the way in —
    // and a background job returns the session it runs as, so this is the two ends of an existing
    // number meeting. Without it, watching what one run is doing meant reading everybody's rows and
    // guessing which were whose; a background job cannot send its caller a message (its session is
    // tied to nobody), so the journal filtered by session IS the channel.
    ibString     session_id;        // empty = any
    ibString     source;            // empty = any
    ibString     event_type;        // empty = any
    ibString     ref_guid;          // drill-down by object identity
    ibString     search;            // substring in message (LIKE %...%)
    std::size_t  limit  = 1000;     // 0 = no cap
    std::size_t  offset = 0;
};

#endif // __LOGGER_QUERY_H__
