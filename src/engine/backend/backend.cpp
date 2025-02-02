#include "backend.h"

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
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	}
	return TRUE;
}
#endif