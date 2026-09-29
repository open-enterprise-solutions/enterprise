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

bool ibWallOfText(const wxString& text, wxLongLong_t& wall) noexcept
{
	const size_t length = text.length();
	size_t at = 0;
	const auto isDigit = [&](size_t i) { return i < length && text[i] >= wxT('0') && text[i] <= wxT('9'); };
	// Up to `most` digits, at least `least`; the value read, the cursor past them.
	const auto digits = [&](size_t least, size_t most, long long& value) -> bool {
		size_t n = 0; value = 0;
		while (n < most && isDigit(at)) {
			value = value * 10 + static_cast<long long>(text[at].GetValue() - static_cast<wxUint32>(wxT('0')));
			++at; ++n;
		}
		return n >= least;
	};
	const auto take = [&](wxChar c) -> bool {
		if (at < length && text[at] == c) { ++at; return true; }
		return false;
	};
	const auto settle = [&](long long y, long long mo, long long d, long long h, long long mi, long long s, long long ms) -> bool {
		if (mo < 0 || d < 0 || h < 0 || mi < 0 || s < 0 || ms < 0
			|| !ibPartsAreADate(y, static_cast<unsigned>(mo), static_cast<unsigned>(d), static_cast<unsigned>(h),
			                    static_cast<unsigned>(mi), static_cast<unsigned>(s), static_cast<unsigned>(ms)))
			return false;
		wall = ibWallFromParts(static_cast<int>(y), static_cast<unsigned>(mo), static_cast<unsigned>(d), static_cast<unsigned>(h),
			static_cast<unsigned>(mi), static_cast<unsigned>(s), static_cast<unsigned>(ms));
		return true;
	};
	long long year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, millisecond = 0;

	// A run of digits alone: yyyymmdd or yyyymmddhhmmss.
	size_t run = 0;
	while (isDigit(run)) ++run;
	if (run == length && (run == 8 || run == 14)) {
		const auto piece = [&](size_t from, size_t n) {
			long long v = 0;
			for (size_t i = from; i < from + n; ++i) v = v * 10 + static_cast<long long>(text[i].GetValue() - static_cast<wxUint32>(wxT('0')));
			return v;
		};
		year = piece(0, 4); month = piece(4, 2); day = piece(6, 2);
		if (run == 14) { hour = piece(8, 2); minute = piece(10, 2); second = piece(12, 2); }
		return settle(year, month, day, hour, minute, second, 0);
	}

	// dd.mm.yyyy[ hh:mm:ss]
	at = 0;
	if (digits(1, 2, day) && take(wxT('.')) && digits(1, 2, month) && take(wxT('.')) && digits(4, 4, year)) {
		if (at == length)
			return settle(year, month, day, 0, 0, 0, 0);
		while (take(wxT(' '))) {}
		if (digits(1, 2, hour) && take(wxT(':')) && digits(1, 2, minute) && take(wxT(':')) && digits(1, 2, second) && at == length)
			return settle(year, month, day, hour, minute, second, 0);
		return false;
	}

	// yyyy-mm-dd[ hh:mm[:ss[.fraction]]]  (ISO 8601, `T` or a space between the halves)
	at = 0;
	if (digits(4, 4, year) && take(wxT('-')) && digits(2, 2, month) && take(wxT('-')) && digits(2, 2, day)) {
		if (at == length)
			return settle(year, month, day, 0, 0, 0, 0);
		if (!(take(wxT(' ')) || take(wxT('T'))))
			return false;
		if (!(digits(2, 2, hour) && take(wxT(':')) && digits(2, 2, minute)))
			return false;
		if (take(wxT(':'))) {
			if (!digits(2, 2, second))
				return false;
			if (take(wxT('.'))) {
				// The first three digits of the fraction are the milliseconds; the rest is finer than the reading.
				size_t n = 0; long long fraction = 0;
				while (isDigit(at)) { if (n < 3) fraction = fraction * 10 + static_cast<long long>(text[at].GetValue() - static_cast<wxUint32>(wxT('0'))); ++at; ++n; }
				if (n == 0) return false;
				while (n < 3) { fraction *= 10; ++n; }
				millisecond = fraction;
			}
		}
		return at == length && settle(year, month, day, hour, minute, second, millisecond);
	}
	return false;
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
