#ifndef __IB_CRASH_GUARD_H__
#define __IB_CRASH_GUARD_H__

// ibCrashGuard — headless crash diagnostics. Pure functional, no wx UI.
//
// Layering:
//   - backend  (this module): SEH filter / signal handlers / minidump
//                             writer / std::set_terminate / log files /
//                             platform-native message-box. Linkable
//                             from any binary regardless of frontend.
//   - frontend/diagnostics/oesApp.h: header-only ibWxApp base. wxApp +
//                             wxDebugReport + wxMessageBox pre-wired
//                             pipeline on top of this guard. Inherited
//                             by enterprise, designer, codeRunner,
//                             launcher.
//   - wfrontend (web): cpp-httplib JSON-500 exception handler bolted on
//                      top of this guard. Used by wenterprise-server.
//   - console (daemon-class): this guard alone — no wx UI, just dumps
//                             and log files.
//
// What this module DOES:
//   - SetUnhandledExceptionFilter (Windows) for persistent minidumps.
//   - sigaction for SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS (POSIX) with
//     async-signal-safe backtrace log.
//   - std::set_terminate for background-thread C++ throws.
//   - Process-native message-box (MessageBoxW on Win, stderr on POSIX) —
//     usable from any thread, no main-loop dependency.
//   - Headless WrapStartup: try/catch + log + native message.
//
// What this module DOES NOT do (delegated to wx-aware layer):
//   - wxDebugReportPreviewStd dialogs.
//   - wxMessageBox.
//   - wxApp::OnExceptionInMainLoop / OnFatalException / OnUnhandled.
//   - wxTheApp->CallAfter marshalling.

#include "backend/backend_core.h"
#include <wx/string.h>
#include <functional>
#include <utility>      // std::forward — the journal's parameter pack

namespace ibCrashGuard {

// One-shot install. Call FIRST from main / wxApp::OnInit before anything
// that can throw or fault. exeName seeds the dump-file prefix:
// `crashdumps/<exeName>_<pid>_<stamp>_<seq>.dmp`.
//
// Idempotent — second Install with a different exeName updates the
// label but does not re-register handlers.
BACKEND_API void Install(const wxString& exeName);


// Append `message` to `<exeName>_startup.log` next to the binary. No UI.
// Frontend / web / console wrappers add their own user-visible surface.
BACKEND_API void LogStartupError(const wxString& exeName,
                                  const wxString& message);

// Append `diag` to `<exeName>_unhandled.log`. No UI.
BACKEND_API void LogUnhandledException(const wxString& exeName,
                                        const wxString& diag);

// Platform-native message box that does NOT depend on wx state. Safe
// from any thread; safe before wxApp construction and after wxApp
// teardown. On Windows: MessageBoxW (MB_TASKMODAL). On POSIX: writes
// "[caption] message" to stderr.
//
// Frontend / web layers should still prefer wxMessageBox or HTTP-500
// responses while the higher-level state is alive; this is the bottom
// fallback for startup failure / background-thread faults / post-exit.
BACKEND_API void PlatformNativeMessage(const wxString& caption,
                                       const wxString& message);

// Force-terminate the current process without running destructors or
// atexit handlers. Use only after a fault has already torn diagnostic
// state — never as a normal exit path. Windows: TerminateProcess;
// POSIX: _Exit.
BACKEND_API void TerminateProcessFast(int exitCode);

// Headless WrapStartup — runs `body` inside a try/catch, logs any
// escape to `<exeName>_startup.log` AND surfaces a PlatformNativeMessage.
// Returns body's int on success, 1 on caught error. ibWxApp::OnRun
// wraps DoOnRun with a wx-aware variant that prefers wxMessageBox
// over PlatformNativeMessage; for console / daemon / wes, this is the
// direct path.
BACKEND_API int WrapStartup(const wxString& exeName,
                             std::function<int()> body);

// ⭐ A DUMP RIGHT NOW, with nothing wrong yet — the process keeps running. Written when something
// reports a failure it can still walk away from: by the time a person reads the journal line, the
// state that produced it is long gone, and this is the only way to keep it.
//
// `kindSuffix` goes into the file name beside the pid and the stamp, so a deliberate dump is
// distinguishable from a crash one at a glance. No-op off Windows for now — the POSIX side writes a
// backtrace from its signal handler and has no equivalent "snapshot me" call.
BACKEND_API void WriteDumpNow(const wxString& kindSuffix);

// Where this process writes its dump files. Resolved on first Install,
// stable for the rest of the process. ibWxApp::OnFatalException quotes
// this path in its background-thread "we crashed, look here" message.
BACKEND_API wxString GetCrashDir();

} // namespace ibCrashGuard

#endif // __IB_CRASH_GUARD_H__
