#include "core/core.h"

#include <wx/wx.h>   // DllMain's types (windows.h, by wx's door)

#include "core/fstring.h"

// The string pool is drained in THREE places, and each one earned its number:
//   DestroyAppDataEnv (backend/appData.cpp) — the main thread of a process running the engine, which never gets
//                                     DLL_THREAD_DETACH because the thread calling exit() detaches the process,
//                                     not itself.                             291 -> 30 blocks
//   DLL_THREAD_DETACH (below)       — every other thread, including those we neither own nor
//                                     can join (Firebird's, wx's).             30 -> 14 blocks
//                                     MSW only; off Windows ~ThreadPool does the same job on
//                                     thread exit (fstring.h explains why not both everywhere).
//   the atexit drain (fstring.cpp)  — last of all, after the statics that hold strings as long as the process.
// The pool is the core's, so its thread hook is the core's: every process that holds an ibString loads this library,
// the thin client's with no engine in it among them.

//*******************************************************************************************
//*                                 DllMain													*
//*******************************************************************************************

#ifdef __WXMSW__
BOOL APIENTRY DllMain(HANDLE hModule,
	DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
		break;

	case DLL_THREAD_DETACH:
		// The string pool is thread_local and deliberately trivially destructible, so a thread
		// that ends takes its cache with it: the blocks stay allocated and unreachable, and show
		// up in the exit dump as leaks belonging to whoever first allocated them. Measured: every
		// surviving string block traced back to the session-registry thread, which builds the
		// INSERT for a session row and then exits.
		//
		// This is the one hook that covers *every* thread, including those we do not own and
		// cannot join (Firebird's, wx's). Safe under the loader lock: it only returns blocks to
		// the CRT heap — no library calls, no waiting, no allocation.
		ibFStringPool::Drain();
		break;
	}
	return TRUE;
}
#endif
