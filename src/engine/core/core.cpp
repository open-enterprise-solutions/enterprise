#include "core.h"
#include "core/utils/fs/types.h"

#include <wx/wx.h>
#include <process.h> 

static int start_day = 31;	    // 1
static int start_month = 1;	    // January
static int start_year = 2018;	// 2018

static std::string build_date = __DATE__;

static std::string month_id[12] = {
	"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};

static int days_in_month[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static unsigned int s_buildID = 0;

void CalculateBuildId()
{
	int					days;
	int					years;
	char			    month[256];

	sscanf(build_date.c_str(), "%s %d %d", month, &days, &years);

	int					months = 0;

	for (int i = 0; i < 12; i++) {
		if (month_id[i] != month)
			continue;

		months = i;
		break;
	}

	u32 build_id = (years - start_year) * 365 + days - start_day;

	for (int i = 0; i < months; ++i)
		build_id += days_in_month[i];

	for (int i = 0; i < start_month - 1; ++i)
		build_id -= days_in_month[i];

	s_buildID = build_id;
}

unsigned int GetBuildId()
{
	return s_buildID;
}

#ifdef __WXMSW__
BOOL APIENTRY DllMain(HANDLE hModule,
	DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		CalculateBuildId();
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}
#endif