#ifndef __CORE_PROCESS_H__
#define __CORE_PROCESS_H__

#include "core/core.h"

#include <wx/string.h>

// Starts a program and does not wait for it: the new process's id, or 0 when it could not start (the reason goes to
// the technology journal). The command is one line, split the way wxExecute splits it - a quoted path stays one
// argument.
//
// ⚠ CALLABLE FROM ANY THREAD, which is why it is not wxExecute. wxExecute asserts off the main thread on Windows and
// waits for its child through the main loop on Unix, and the application server has no main loop at all: a designer's
// tool there runs on a session's fiber, and app_run asserted from one (2026-10-21). The child is not watched; on POSIX
// a thread waits for it, so it does not stay a zombie that wxProcess::Exists reports as running.
CORE_API long ibStartProcess(const wxString& command);

#endif // __CORE_PROCESS_H__
