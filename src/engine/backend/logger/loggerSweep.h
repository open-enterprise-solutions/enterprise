#ifndef __IB_LOGGER_SWEEP_H__
#define __IB_LOGGER_SWEEP_H__

#include "backend/backend.h"

#include <wx/string.h>

// Retention sweep — deletes .olg files in `dir` whose month bucket
// ends before (now - retentionDays). Idempotent; safe to call
// concurrently with the live writer (the writer holds the current
// month's file open, sweep skips it). Returns number of files
// removed.
//
// Phase 1 invocation: once at logger startup. A periodic daily-job
// variant via workerPool is a follow-up (Phase 5b).
class BACKEND_API ibLoggerSweep {
public:
    static int RunOnce(const wxString& dir, int retentionDays);
};

#endif
