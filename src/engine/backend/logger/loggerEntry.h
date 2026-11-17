#ifndef __IB_LOGGER_ENTRY_H__
#define __IB_LOGGER_ENTRY_H__

#include "backend/backend.h"

#include "core/memory/memoryBuffer.h"

// Log severity. Audit = business events (login / document save / DDL apply
// / session start-stop). Info/Warn/Error = technical traces.
enum class ibLogLevel : int {
    Info  = 0,
    Warn  = 1,
    Error = 2,
    Audit = 3,
};

// Levels are stored as numbers and asked for as words. The word is the unit a
// caller thinks in - "show me the errors" - and the number is the file's. One
// vocabulary for every reader: journal_read, a query's Journal.Events.Level.
inline ibString ibLevelName(int level)
{
    switch (static_cast<ibLogLevel>(level)) {
        case ibLogLevel::Info:  return ibT("info");
        case ibLogLevel::Warn:  return ibT("warn");
        case ibLogLevel::Error: return ibT("error");
        case ibLogLevel::Audit: return ibT("audit");
    }
    return ibT("info");
}

// The number a word names; -1 for a word that is no level (asked for: any).
inline int ibLevelFromWord(const ibString& word)
{
    const ibString lowered = word.Lower();
    if (lowered == ibT("info"))  return static_cast<int>(ibLogLevel::Info);
    if (lowered == ibT("warn"))  return static_cast<int>(ibLogLevel::Warn);
    if (lowered == ibT("error")) return static_cast<int>(ibLogLevel::Error);
    if (lowered == ibT("audit")) return static_cast<int>(ibLogLevel::Audit);
    return -1;
}

// Single record passed from producers to the writer thread. Held by value
// in the queue and moved into the sink's batch — wxString small-buffer
// optimisation keeps short strings inline, ibMemoryBuffer is shared by a count.
struct ibLogEntry {
    long long   ts_ms = 0;      // ms since unix epoch, UTC
    ibLogLevel     level = ibLogLevel::Info;
    ibString       session_id;
    ibString       user_name;
    ibString       host;
    ibString       source;         // 'auth' / 'document' / 'system' / ...
    ibString       event_type;     // 'login' / 'saved' / 'error' / ...
    ibString       message;

    // Navigable reference — present when the audit row is about a
    // specific business object. Viewer uses (ref_meta_id, ref_guid) to
    // resolve back to the live record (metadata → object → form) so
    // the admin can drill from a journal row into the object that
    // triggered it. Empty for system events (login, session.opened).
    ibString       ref_guid;       // string form of ibGuid; empty when no ref
    int            ref_meta_id = 0;// ibMetaID; 0 when no ref

    ibMemoryBuffer details;        // serialised ibValue (empty when unused)
};

#endif
