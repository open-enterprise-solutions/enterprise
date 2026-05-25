#ifndef __IB_LOGGER_ENTRY_H__
#define __IB_LOGGER_ENTRY_H__

#include "backend/backend.h"

#include <wx/string.h>
#include <wx/buffer.h>

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
// optimisation keeps short strings inline, wxMemoryBuffer is ref-counted.
struct ibLogEntry {
    wxLongLong_t   ts_ms = 0;      // ms since unix epoch, UTC
    ibLogLevel     level = ibLogLevel::Info;
    wxString       session_id;
    wxString       user_name;
    wxString       host;
    wxString       source;         // 'auth' / 'document' / 'system' / ...
    wxString       event_type;     // 'login' / 'saved' / 'error' / ...
    wxString       message;

    // Navigable reference — present when the audit row is about a
    // specific business object. Viewer uses (ref_meta_id, ref_guid) to
    // resolve back to the live record (метаданные → объект → форма) so
    // the admin can drill from a journal row into the object that
    // triggered it. Empty for system events (login, session.opened).
    wxString       ref_guid;       // string form of ibGuid; empty when no ref
    int            ref_meta_id = 0;// ibMetaID; 0 when no ref

    wxMemoryBuffer details;        // serialised ibValue (empty when unused)
};

#endif
