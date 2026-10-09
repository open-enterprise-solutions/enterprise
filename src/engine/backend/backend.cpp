#include "backend.h"

// (The string pool's thread hook — DLL_THREAD_DETACH — is the core's now, where the pool is: core/core.cpp.)

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
	case DLL_THREAD_DETACH:
		break;
	}
	return TRUE;
}
#endif
