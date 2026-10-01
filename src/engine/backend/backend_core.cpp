#include "backend_core.h"
#include "backend/fileSystem/types.h"

#include <wx/wx.h>
#ifdef _MSC_VER
#include <process.h>
#endif

static int start_day   = 01;   // 1
static int start_month = 01;   // January
static int start_year  = 2018; // 2018

static std::string build_date = __DATE__;

// ⭐ AND THE TIME OF IT, for whoever needs to tell two builds of ONE DAY apart. The build number
// below is a day count — right for "which release is this", useless for development, where an
// engine is rebuilt forty times between breakfast and dinner and every one of them answers 3164.
//
// 🛑 WHAT MADE IT WORTH HAVING: the bytecode cache is keyed by the platform plus the configuration,
// and with a day-granular platform half, cached bytecode from this morning's engine looked valid to
// this evening's (2026-09-02 — a global function added at noon was invisible for an hour, and Max
// recognised it as the same thing we had been chasing the day before).
//
// ⚠ IT IS THIS TRANSLATION UNIT'S COMPILE TIME. That is what __TIME__ means, so an incremental
// build that does not recompile backend_core.cpp keeps the previous stamp. It moves for a clean
// build, for any change reaching this file's headers, and for a release — which is what it is for.
static std::string build_time = __TIME__;

static std::string month_id[] = {
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

	// Width-capped: an unbounded "%s" writes as far as the input goes, and the buffer's size
	// is the only thing standing between it and the stack. The input is __DATE__, but the
	// cap belongs in the format string, not in an assumption about the caller.
	::sscanf(build_date.c_str(), "%255s %d %d", month, &days, &years);

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
	// Computed from __DATE__, so every caller would compute the same number — the
	// old `if (s_buildID == 0) CalculateBuildId();` raced only to the same answer.
	// Still a check-then-fill on shared state; a function-local static gets the
	// one-time, thread-safe initialisation from the language instead.
	static const unsigned int s_id = [] { CalculateBuildId(); return s_buildID; }();
	return s_id;
}

// ⭐ THE BUILD, SPELLED OUT. The number above is the VERSION and is right to be stable — it says
// which engine this is, and a day's builds are one engine as far as anybody outside is concerned.
// The stamp is the same fact unfolded: the number, and the date and time it was actually compiled.
//
// Whoever needs to tell two builds of one day apart takes this one (the bytecode cache does — see
// byteCodeCache.cpp, and the hour it cost on 2026-09-02 to find out why cached bytecode outlived
// the engine that made it). Whoever is showing a version takes the number.
const char* GetBuildStamp()
{
	// "3164 (Sep  2 2026 16:55:03)" — assembled once; nothing here is meant to be parsed back.
	static const std::string s_stamp =
		std::to_string(GetBuildId()) + " (" + build_date + " " + build_time + ")";
	return s_stamp.c_str();
}
