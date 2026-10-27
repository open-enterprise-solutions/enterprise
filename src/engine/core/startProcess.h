#ifndef __CORE_START_PROCESS_H__
#define __CORE_START_PROCESS_H__

// ⚠ NOT process.h: the core's folder is on the include path of the modules, and a file of that name stood in for the
// CRT's <process.h> that <thread> includes — `_beginthreadex` was not found in the first file to recompile (2026-10-21).

#include "core/core.h"

#include <functional>
#include <vector>


// Starts a program and does not wait for it: the new process's id, or 0 when it could not start (the reason goes to
// the technology journal). The command is one line, split the way wxExecute splits it - a quoted path stays one
// argument.
//
// ⚠ CALLABLE FROM ANY THREAD, which is why it is not wxExecute. wxExecute asserts off the main thread on Windows and
// waits for its child through the main loop on Unix, and the application server has no main loop at all: a designer's
// tool there runs on a session's fiber, and app_run asserted from one (2026-10-21). The child is not watched; on POSIX
// a thread waits for it, so it does not stay a zombie that wxProcess::Exists reports as running.
//
// `input`, when there is any, is written on the child's stdin and the pipe closed after it - the road a launch hands
// its passwords by (launchTicket.h), so they are never on a command line. Without it the child's stdin is left alone.
CORE_API long ibStartProcess(const ibString& command, const std::vector<unsigned char>& input = {});

// Starts the web server (webserver) and waits, up to five seconds, for the address it serves at: `command` is the
// binary and what it publishes; `--port=0 --manifest=<file>` are added here, and the server writes `url=` there once
// its port is open. The process's id, or 0; `url` stays empty when the server never said one. `waiting` is called
// between the looks - a window keeps painting meanwhile (wxYieldIfNeeded is the caller's: the core has no window).
CORE_API long ibStartWebServer(const ibString& command, ibString& url, const std::function<void()>& waiting = {});

// A STARTED PROCESS, ASKED TO GO: Stop asks it to finish (SIGTERM; on Windows its windows are told to close), Kill ends
// it at once (SIGKILL / TerminateProcess). Each is false when the process could not be reached.
CORE_API bool ibStopProcess(long pid);
CORE_API bool ibKillProcess(long pid);
// …and whether it is still running.
CORE_API bool ibProcessExists(long pid);

// This process's environment, as a library it loads reads it (fbclient finds its folder by FIREBIRD).
CORE_API bool ibSetEnv(const ibString& name, const ibString& value);

// This computer's name, as the system calls it.
CORE_API ibString ibHostName();

#endif // __CORE_START_PROCESS_H__
