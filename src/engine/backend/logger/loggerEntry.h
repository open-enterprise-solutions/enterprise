#ifndef __IB_LOGGER_ENTRY_H__
#define __IB_LOGGER_ENTRY_H__

#include "backend/backend.h"

#include <wx/string.h>
#include "core/memory/memoryBuffer.h"

// Log severity. Audit = business events (login / document save / DDL apply
// / session start-stop). Info/Warn/Error = technical traces.
enum class ibLogLevel : int {
    Info  = 0,
    Warn  = 1,
    Error = 2,
    Audit = 3,
};

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
