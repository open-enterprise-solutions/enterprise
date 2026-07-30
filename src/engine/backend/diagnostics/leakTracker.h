////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : leak attribution for the debug CRT dump
////////////////////////////////////////////////////////////////////////////

#ifndef __IB_LEAK_TRACKER_H__
#define __IB_LEAK_TRACKER_H__

#include "backend/backend.h"

// The debug CRT prints "Detected memory leaks!" on exit and names every surviving block by size
// and by an allocation ordinal — never by WHO allocated it, which is the only thing worth knowing.
// This module answers that, and it lives in backend so every binary that links backend gets it:
// enterprise, designer, codeRunner, launcher, wenterprise-server. Same layering rule as
// ibCrashGuard next door — no wx UI, linkable from anything.
//
//     set OES_TRACK_SIZE=116,32,64   record a call stack for every allocation of these sizes
//     set OES_TRACK_ALL=1            same for every size — complete, much slower
//     set OES_TRACK_ORDINAL=423758   print the stacks of these exact blocks and nothing else
//     set OES_TRACK_TAIL=1           print every allocation made AFTER the report (teardown)
//     set OES_BREAK_ALLOC=4429052    break in the debugger on that allocation
//
// Nothing runs unless one of those is set, so an ordinary debug run pays nothing at all.
//
// Platform: this is the MSVC debug heap (_CrtSetAllocHook) and dbghelp, so it is Windows-Debug
// only and compiles to nothing everywhere else. The equivalent on Linux / macOS is not ours to
// write — LeakSanitizer already reports leaks with stacks, and the CMake build exposes it as
// -DOES_SANITIZE=address. The procedure in docs/engineering-playbook/25-memory-leaks.md is the
// same either way; only the instrument differs.

#if defined(DEBUG) && defined(__WXMSW__)

#include <cstdlib>

// Reads the environment and installs the hooks. Returns true when something was asked for and the
// tracker is live — only then is a report worth registering.
BACKEND_API bool ibLeakTrackArm();

// Prints the surviving blocks, grouped by call site. Must run from atexit: every atexit handler
// runs after main returns, i.e. after wxEntry has torn down the windows and cleaned up wx's own
// caches, which is the same heap the CRT dump is about to read. Reporting from OnExit instead put
// the print three orders of magnitude too early (4618 "alive" against the dump's 2273).
BACKEND_API void ibLeakTrackReport();

// One line per binary, next to wxIMPLEMENT_APP. Static init on purpose — the hooks have to be
// armed before wxWidgets allocates anything, which rules out OnInit.
//
// The atexit registration is deliberately HERE, in the macro, rather than inside ibLeakTrackArm:
// MSVC gives every DLL its own onexit table, so an atexit called from backend.dll would run at
// backend's DLL_PROCESS_DETACH instead of at the executable's exit. Expanding in the executable
// keeps the report where it was measured to belong.
#define IB_LEAK_TRACKER_ARM()                                          \
	namespace {                                                        \
	struct ibLeakTrackArmOnce {                                        \
		ibLeakTrackArmOnce() {                                         \
			if (ibLeakTrackArm())                                      \
				std::atexit(&ibLeakTrackReport);                       \
		}                                                              \
	} s_ibLeakTrackArmOnce;                                            \
	}

#else

#define IB_LEAK_TRACKER_ARM()

#endif // DEBUG && __WXMSW__

#endif // __IB_LEAK_TRACKER_H__
