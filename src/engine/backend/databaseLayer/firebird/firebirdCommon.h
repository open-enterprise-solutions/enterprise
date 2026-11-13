#ifndef __FIREBIRD_COMMON_H__
#define __FIREBIRD_COMMON_H__

// Shared internal helpers for the FB driver sub-system. Inline but for
// LogThreadSafe (firebirdCommon.cpp) — used by firebirdLease / firebirdLeaderMode /
// firebirdMaintenanceScheduler / firebirdHlc which all need the
// same wall-clock millisecond reading. Centralising avoids the
// three identical copies that drifted independently before.


#include "core/fstring.h"   // ibString

#include <chrono>
#include <cstdint>

namespace ibFb {

// Wall-clock UNIX ms since epoch. Used for lease heartbeat, HLC
// stamping, scheduler bookkeeping. std::chrono::system_clock is
// the right pick (not steady_clock) because we want the actual
// wall time observable across processes — followers compare a
// leader's heartbeat timestamp against their own clock to detect
// staleness, so monotonic-but-not-comparable steady_clock would
// be wrong.
inline uint64_t NowUnixMs() {
	return static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()
		).count());
}

// Thread-safe log line for background threads. wxLog{Message,
// Warning,Error} in wx 3.3.2 race with main-thread wxEvtHandler
// dispatch via the dynamic-event-table recursion guard:
// concurrent wxRecursionGuard ctor/dtor on the same flag
// corrupts the counter, fires wxASSERT("recursion guard flag
// corrupted") and aborts. wxAuiMDIParentFrame's idle handler
// is a typical victim. Background threads (lease heartbeat,
// maintenance scheduler) must use OS-level logging directly.
//
// Win32: OutputDebugStringW — async, lock-free, captured by
// debugger / DebugView.
// POSIX: fputs to stderr — atomic per-line on most libc's.
// Defined in firebirdCommon.cpp: windows.h stays out of the headers, where its macros (GetUserName,
// GetClassName) and its old winsock.h would reach every file that includes this one.
void LogThreadSafe(const ibString& msg);

} // namespace ibFb

#endif // __FIREBIRD_COMMON_H__
