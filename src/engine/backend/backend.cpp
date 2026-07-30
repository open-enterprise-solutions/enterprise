#include "backend.h"

#include "backend/fstring.h"

// The string pool is drained in TWO places, and each one earned its number:
//   DestroyAppDataEnv (appData.cpp) — the main thread, which never gets DLL_THREAD_DETACH
//                                     because the thread calling exit() detaches the process,
//                                     not itself.                             291 -> 30 blocks
//   DLL_THREAD_DETACH (below)       — every other thread, including those we neither own nor
//                                     can join (Firebird's, wx's).             30 -> 14 blocks
//                                     MSW only; off Windows ~ThreadPool does the same job on
//                                     thread exit (fstring.h explains why not both everywhere).
//
// A THIRD drain lived here: an atexit handler registered under `#pragma init_seg(lib)`, on the
// theory that statics release their buffers into the pool after DestroyAppDataEnv has already
// drained it. It was measured at 30 blocks before and 30 after — it never freed anything — and it
// is gone. The recipe itself is sound and written up in the playbook; it just had no work to do
// here, and a cleanup that cleans nothing is a claim that something needed cleaning.

//*******************************************************************************************
//*                                 DllMain													*
//*******************************************************************************************

#ifdef __WXMSW__
static HANDLE ThreadId = nullptr;

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