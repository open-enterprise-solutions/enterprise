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

	// The day's place: in the week (day 0 was a Thursday, Monday is 1), in the year, and in the ISO
	// week numbering, where week 1 is the week holding January 4th and a year has 53 weeks when it
	// begins on a Thursday, or on a Wednesday in a leap year. The last days of December can belong to
	// week 1 of the next year and the first days of January to the last week of the one before -
	// that is the rule, and it is the rule wxDateTime::GetWeekOfYear(Monday_First) applied.
	parts.m_weekDay = static_cast<unsigned>(((days % 7 + 7) % 7 + 3) % 7 + 1);
	parts.m_yearDay = static_cast<unsigned>(days - ibDaysFromCivil(parts.m_year, 1, 1) + 1);
	const auto weeksOf = [](long long year) -> unsigned {
		const auto jan1 = [](long long y) { return static_cast<unsigned>(((ibDaysFromCivil(y, 1, 1) % 7 + 7) % 7 + 3) % 7 + 1); };
		return jan1(year) == 4 || (jan1(year) == 3 && ibDaysInMonth(year, 2) == 29) ? 53u : 52u;
	};
	const long long week = (static_cast<long long>(parts.m_yearDay) - parts.m_weekDay + 10) / 7;
	parts.m_isoWeek = week < 1 ? weeksOf(parts.m_year - 1)
	                : week > weeksOf(parts.m_year) ? 1u : static_cast<unsigned>(week);
}

wxDateTime ibDateTimeOfWall(wxLongLong_t wall)
{
	ibDateParts p;
	ibWallToParts(wall, p);
	return wxDateTime(static_cast<wxDateTime::wxDateTime_t>(p.m_day), static_cast<wxDateTime::Month>(p.m_month - 1), p.m_year,
		static_cast<wxDateTime::wxDateTime_t>(p.m_hour), static_cast<wxDateTime::wxDateTime_t>(p.m_minute),
		static_cast<wxDateTime::wxDateTime_t>(p.m_second), static_cast<wxDateTime::wxDateTime_t>(p.m_millisecond));
}

wxLongLong_t ibWallOfDateTime(const wxDateTime& moment)
{
	if (!moment.IsValid())
		return ibWallFromParts(1, 1, 1);
	const wxDateTime::Tm tm = moment.GetTm();
	return ibWallFromParts(tm.year, static_cast<unsigned>(tm.mon) + 1, tm.mday, tm.hour, tm.min, tm.sec, tm.msec);
}
