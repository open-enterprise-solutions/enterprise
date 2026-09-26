////////////////////////////////////////////////////////////////////////////
//	Description : The calendar without a clock (fdate.h)
////////////////////////////////////////////////////////////////////////////

#include "fdate.h"

namespace {

// The inverse of ibDaysFromCivil: the calendar day a day count is, proleptic Gregorian.
void CivilFromDays(long long days, int& year, unsigned& month, unsigned& day) noexcept
{
	days += 719468;
	const long long era = (days >= 0 ? days : days - 146096) / 146097;
	const unsigned long long doe = static_cast<unsigned long long>(days - era * 146097);            // [0, 146096]
	const unsigned long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;          // [0, 399]
	const long long y = static_cast<long long>(yoe) + era * 400;
	const unsigned long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);                          // [0, 365]
	const unsigned long long mp = (5 * doy + 2) / 153;                                               // [0, 11]
	day = static_cast<unsigned>(doy - (153 * mp + 2) / 5 + 1);                                       // [1, 31]
	month = static_cast<unsigned>(mp < 10 ? mp + 3 : mp - 9);                                        // [1, 12]
	year = static_cast<int>(y + (month <= 2 ? 1 : 0));
}

} // namespace

void ibWallToParts(wxLongLong_t wall, ibDateParts& parts) noexcept
{
	// Floor division: a reading before the epoch belongs to the day that STARTED before it, and its
	// time of day counts up from that day's midnight - the way a calendar reads, not the way `/`
	// truncates toward zero.
	wxLongLong_t days = wall / ibWallMsPerDay;
	wxLongLong_t ofDay = wall % ibWallMsPerDay;
	if (ofDay < 0) { ofDay += ibWallMsPerDay; --days; }

	CivilFromDays(days, parts.m_year, parts.m_month, parts.m_day);
	parts.m_millisecond = static_cast<unsigned>(ofDay % 1000); ofDay /= 1000;
	parts.m_second      = static_cast<unsigned>(ofDay % 60);   ofDay /= 60;
	parts.m_minute      = static_cast<unsigned>(ofDay % 60);   ofDay /= 60;
	parts.m_hour        = static_cast<unsigned>(ofDay);
}
