////////////////////////////////////////////////////////////////////////////
//	Description : A date and a time of day - a wall-clock reading (fdatetime.h)
////////////////////////////////////////////////////////////////////////////

#include "fdatetime.h"
#include "fstring.h"

#include <algorithm>   // std::min - the day a shorter month clamps to
#include <chrono>      // Now - the machine's clock
#include <cwchar>      // std::swprintf - the text of a date outside years 1..9999
#include <ctime>       // Now - the clock's reading in the machine's zone
#include <wx/datetime.h>

// ⭐ ASKED FOR ONE THING, A DATE WORKS OUT ONE THING. Most questions a date is asked - the month a report
// folds by, the day a year's period begins on, its hour - need either the calendar day or the time of day,
// never both and never the week: the time of day is the remainder of the day, the weekday is the day count
// modulo seven, and only the ISO week needs its neighbours. So the calendar day (CivilOf) and the time of
// day (TimeOf) are worked out on their own, and ToParts - which answers everything - is left to whoever
// wants everything.
namespace {

constexpr long long s_msPerSecond = 1000ll;
constexpr long long s_msPerMinute = 60000ll;
constexpr long long s_msPerHour = 3600000ll;
constexpr long long s_emptyDay = ibDaysFromCivil(1, 1, 1);   // the count's first day, in days-from-civil numbering

struct CivilDay {
	int      m_year;
	unsigned m_month;   // 1..12
	unsigned m_day;     // 1..31
};

// The inverse of days-from-civil: the calendar day a count of days since 1970-01-01 is, proleptic Gregorian.
CivilDay CivilFromDays(long long days) noexcept
{
	days += 719468;
	const long long era = (days >= 0 ? days : days - 146096) / 146097;
	const unsigned long long doe = static_cast<unsigned long long>(days - era * 146097);            // [0, 146096]
	const unsigned long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;          // [0, 399]
	const long long y = static_cast<long long>(yoe) + era * 400;
	const unsigned long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);                          // [0, 365]
	const unsigned long long mp = (5 * doy + 2) / 153;                                               // [0, 11]
	const unsigned day = static_cast<unsigned>(doy - (153 * mp + 2) / 5 + 1);                        // [1, 31]
	const unsigned month = static_cast<unsigned>(mp < 10 ? mp + 3 : mp - 9);                         // [1, 12]
	return { static_cast<int>(y + (month <= 2 ? 1 : 0)), month, day };
}

// The calendar day a reading falls on. Floor division (GetDays): a reading before the empty date belongs
// to the day that STARTED before it - the way a calendar reads, not the way `/` truncates toward zero.
CivilDay CivilOf(const ibDateTime& date) noexcept
{
	return CivilFromDays(date.GetDays() + s_emptyDay);
}

// The time of day, counted up from the day's midnight.
void TimeOf(const ibDateTime& date, unsigned& hour, unsigned& minute, unsigned& second, unsigned& millisecond) noexcept
{
	long long ofDay = date - date.GetDayStart();
	millisecond = static_cast<unsigned>(ofDay % 1000); ofDay /= 1000;
	second      = static_cast<unsigned>(ofDay % 60);   ofDay /= 60;
	minute      = static_cast<unsigned>(ofDay % 60);   ofDay /= 60;
	hour        = static_cast<unsigned>(ofDay);
}

// The weekday of a count of days from the empty date: 0001-01-01 was a Monday, and Monday is 1.
unsigned WeekDayOf(long long days) noexcept
{
	return static_cast<unsigned>((days % 7 + 7) % 7 + 1);
}

// The day of the year of a count of days from the empty date that falls in `year`.
unsigned YearDayOf(long long days, int year) noexcept
{
	return static_cast<unsigned>(days + s_emptyDay - ibDaysFromCivil(year, 1, 1) + 1);
}

// The first day of the period a calendar day falls in, for the calendar units.
void FirstOfPeriod(CivilDay& day, ibTotalsPeriod period) noexcept
{
	switch (period) {
		case ibTotalsPeriod::TenDays:  day.m_day = 1 + 10 * std::min((day.m_day - 1) / 10, 2u); break;
		case ibTotalsPeriod::Month:    day.m_day = 1; break;
		case ibTotalsPeriod::Quarter:  day.m_day = 1; day.m_month = ((day.m_month - 1) / 3) * 3 + 1; break;
		case ibTotalsPeriod::HalfYear: day.m_day = 1; day.m_month = day.m_month < 7 ? 1 : 7; break;
		case ibTotalsPeriod::Year:     day.m_day = 1; day.m_month = 1; break;
		default: break;
	}
}

// Months added to a reading, the day clamped to the month it lands in - Jan 31 + 1 is Feb 28 (29), as
// every engine's DATEADD has it - and the time of day kept.
ibDateTime AddMonths(const ibDateTime& date, long months) noexcept
{
	const CivilDay day = CivilOf(date);
	const long long total = static_cast<long long>(day.m_year) * 12 + static_cast<long long>(day.m_month) - 1 + months;
	const long long year = (total >= 0 ? total : total - 11) / 12;
	const unsigned month = static_cast<unsigned>(total - year * 12) + 1;
	return ibDateTime(static_cast<int>(year), month, std::min(day.m_day, ibDateTime::DaysInMonth(year, month)))
		.AddMilliseconds(date - date.GetDayStart());
}

// Two digits, and four, into a text being written.
wchar_t* PutTwo(wchar_t* at, unsigned value) noexcept
{
	*at++ = static_cast<wchar_t>(L'0' + value / 10 % 10);
	*at++ = static_cast<wchar_t>(L'0' + value % 10);
	return at;
}

wchar_t* PutFour(wchar_t* at, unsigned value) noexcept
{
	return PutTwo(PutTwo(at, value / 100), value % 100);
}

} // namespace

ibDateTime ibDateTime::Now()
{
	// The clock's reading in the machine's zone, by its parts: what the clock on the wall shows here.
	const auto now = std::chrono::system_clock::now();
	const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
	const long long milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
	std::tm local{};
#if defined(_WIN32)
	localtime_s(&local, &seconds);
#else
	localtime_r(&seconds, &local);
#endif
	return ibDateTime(local.tm_year + 1900, static_cast<unsigned>(local.tm_mon) + 1, static_cast<unsigned>(local.tm_mday),
		static_cast<unsigned>(local.tm_hour), static_cast<unsigned>(local.tm_min),
		static_cast<unsigned>(std::min(local.tm_sec, 59)),   // a leap second's 60 is the minute's last second on the wall
		static_cast<unsigned>(milliseconds < 0 ? milliseconds + 1000 : milliseconds));
}

long long ibDateTime::ElapsedSince(const ibDateTime& earlier) const noexcept
{
	// The instant of a reading on this machine: its local parts, the zone deciding whether a clock change
	// lies between two of them (tm_isdst -1: the machine's own rule). -1 is mktime's "cannot place it".
	const auto instantOf = [](const ibDateTime& reading, long long& milliseconds) -> std::time_t {
		const CivilDay day = CivilOf(reading);
		unsigned hour, minute, second, millisecond;
		TimeOf(reading, hour, minute, second, millisecond);
		milliseconds = millisecond;
		std::tm local{};
		local.tm_year = day.m_year - 1900;
		local.tm_mon = static_cast<int>(day.m_month) - 1;
		local.tm_mday = static_cast<int>(day.m_day);
		local.tm_hour = static_cast<int>(hour);
		local.tm_min = static_cast<int>(minute);
		local.tm_sec = static_cast<int>(second);
		local.tm_isdst = -1;
		return std::mktime(&local);
	};
	long long laterMs = 0, earlierMs = 0;
	const std::time_t later = instantOf(*this, laterMs);
	const std::time_t before = instantOf(earlier, earlierMs);
	if (later == static_cast<std::time_t>(-1) || before == static_cast<std::time_t>(-1))
		return *this - earlier;
	return (static_cast<long long>(later) - static_cast<long long>(before)) * s_msPerSecond + (laterMs - earlierMs);
}

ibDateTime ibDateTime::OfWxDateTime(const wxDateTime& moment)
{
	if (!moment.IsValid())
		return ibDateTime();
	const wxDateTime::Tm tm = moment.GetTm();
	return ibDateTime(tm.year, static_cast<unsigned>(tm.mon) + 1, tm.mday, tm.hour, tm.min, tm.sec, tm.msec);
}

wxDateTime ibDateTime::ToWxDateTime() const
{
	if (IsEmpty())
		return wxDateTime();
	const CivilDay day = CivilOf(*this);
	unsigned hour, minute, second, millisecond;
	TimeOf(*this, hour, minute, second, millisecond);
	return wxDateTime(static_cast<wxDateTime::wxDateTime_t>(day.m_day), static_cast<wxDateTime::Month>(day.m_month - 1), day.m_year,
		static_cast<wxDateTime::wxDateTime_t>(hour), static_cast<wxDateTime::wxDateTime_t>(minute),
		static_cast<wxDateTime::wxDateTime_t>(second), static_cast<wxDateTime::wxDateTime_t>(millisecond));
}

void ibDateTime::ToParts(ibDateTimeParts& parts) const noexcept
{
	const long long days = GetDays();
	const CivilDay day = CivilFromDays(days + s_emptyDay);
	parts.m_year = day.m_year;
	parts.m_month = day.m_month;
	parts.m_day = day.m_day;
	TimeOf(*this, parts.m_hour, parts.m_minute, parts.m_second, parts.m_millisecond);
	parts.m_weekDay = WeekDayOf(days);
	parts.m_yearDay = YearDayOf(days, day.m_year);
}

// ===========================================================================================================
// THE CALENDAR'S PERIODS. Every branch mirrors what the dialect's SQL does (fdatetime.h). Nothing here asks
// the machine's clock, so the answer is the one the server's TIMESTAMP arithmetic gives, on every machine. (The
// forms over a wxDateTime that stood in the query layer before could not meet that across a clock change:
// `+ wxTimeSpan::Hours(1)` is an hour of real time, and 02:30 + 1 on the morning the clocks go forward read
// 04:30 on the wall, where the server says 03:30. DATEDIFF in days across that morning was a day short.)
// ===========================================================================================================

ibDateTime ibDateTime::BeginOfPeriod(ibTotalsPeriod period) const noexcept
{
	// Floor to a whole unit - a reading before the empty date still belongs to the unit that began before it.
	const auto floorTo = [this](long long unit) { return ibDateTime(m_value - ((m_value % unit) + unit) % unit); };
	switch (period) {
		case ibTotalsPeriod::Second: return floorTo(s_msPerSecond);
		case ibTotalsPeriod::Minute: return floorTo(s_msPerMinute);
		case ibTotalsPeriod::Hour:   return floorTo(s_msPerHour);
		case ibTotalsPeriod::Day:    return GetDayStart();
		case ibTotalsPeriod::Week:   return GetDayStart().AddDays(1 - static_cast<long long>(WeekDayOf(GetDays())));
		case ibTotalsPeriod::TenDays:
		case ibTotalsPeriod::Month:
		case ibTotalsPeriod::Quarter:
		case ibTotalsPeriod::HalfYear:
		case ibTotalsPeriod::Year: {
			CivilDay day = CivilOf(*this);
			FirstOfPeriod(day, period);
			return ibDateTime(day.m_year, day.m_month, day.m_day);
		}
	}
	return *this;
}

ibDateTime ibDateTime::BeginOfNextPeriod(ibTotalsPeriod period) const noexcept
{
	const ibDateTime start = BeginOfPeriod(period);
	switch (period) {
		case ibTotalsPeriod::Second:   return start.AddMilliseconds(s_msPerSecond);
		case ibTotalsPeriod::Minute:   return start.AddMilliseconds(s_msPerMinute);
		case ibTotalsPeriod::Hour:     return start.AddMilliseconds(s_msPerHour);
		case ibTotalsPeriod::Day:      return start.AddDays(1);
		case ibTotalsPeriod::Week:     return start.AddDays(7);
		case ibTotalsPeriod::TenDays: {
			// The third bucket runs to the END of the month: what follows it is the 1st of the next.
			const CivilDay day = CivilOf(start);
			if (day.m_day >= 21)
				return AddMonths(ibDateTime(day.m_year, day.m_month, 1), 1);
			return start.AddDays(10);
		}
		case ibTotalsPeriod::Month:    return AddMonths(start, 1);
		case ibTotalsPeriod::Quarter:  return AddMonths(start, 3);
		case ibTotalsPeriod::HalfYear: return AddMonths(start, 6);
		case ibTotalsPeriod::Year:     return AddMonths(start, 12);
	}
	return start;
}

ibDateTime ibDateTime::EndOfPeriod(ibTotalsPeriod period) const noexcept
{
	return BeginOfNextPeriod(period).AddMilliseconds(-s_msPerSecond);
}

ibDateTime ibDateTime::AddPeriods(ibTotalsPeriod period, long count) const noexcept
{
	switch (period) {
		case ibTotalsPeriod::Second:   return AddMilliseconds(count * s_msPerSecond);
		case ibTotalsPeriod::Minute:   return AddMilliseconds(count * s_msPerMinute);
		case ibTotalsPeriod::Hour:     return AddMilliseconds(count * s_msPerHour);
		case ibTotalsPeriod::Day:      return AddDays(count);
		case ibTotalsPeriod::Week:     return AddDays(static_cast<long long>(count) * 7);
		case ibTotalsPeriod::TenDays: {
			ibDateTime d = *this;
			for (long i = 0; i < count; ++i) d = d.BeginOfNextPeriod(period);
			for (long i = 0; i > count; --i) d = d.BeginOfPeriod(period).AddMilliseconds(-s_msPerSecond);
			return d;
		}
		case ibTotalsPeriod::Month:    return AddMonths(*this, count);
		case ibTotalsPeriod::Quarter:  return AddMonths(*this, count * 3);
		case ibTotalsPeriod::HalfYear: return AddMonths(*this, count * 6);
		case ibTotalsPeriod::Year:     return AddMonths(*this, count * 12);
	}
	return *this;
}

long ibDateTime::PeriodsUntil(const ibDateTime& to, ibTotalsPeriod period) const noexcept
{
	// Every sub-day unit is one wall-clock difference, so none can disagree with Day about a boundary;
	// the calendar units count the boundaries between the periods the two readings fall in.
	const long long dayDelta = to.GetDays() - GetDays();
	const long long secDelta = (to - to.GetDayStart()) / s_msPerSecond - (*this - GetDayStart()) / s_msPerSecond + dayDelta * 86400;
	switch (period) {
		case ibTotalsPeriod::Second:   return static_cast<long>(secDelta);
		case ibTotalsPeriod::Minute:   return static_cast<long>(secDelta / 60);
		case ibTotalsPeriod::Hour:     return static_cast<long>(secDelta / 3600);
		case ibTotalsPeriod::Day:      return static_cast<long>(dayDelta);
		case ibTotalsPeriod::Week:     return static_cast<long>((to.BeginOfPeriod(period).GetDays() - BeginOfPeriod(period).GetDays()) / 7);
		case ibTotalsPeriod::TenDays: {
			ibDateTime cur = BeginOfPeriod(period);
			const ibDateTime end = to.BeginOfPeriod(period);
			long steps = 0;
			while (cur < end) { cur = cur.BeginOfNextPeriod(period); ++steps; }
			while (cur > end) { cur = cur.AddMilliseconds(-s_msPerSecond).BeginOfPeriod(period); --steps; }
			return steps;
		}
		case ibTotalsPeriod::Month:
		case ibTotalsPeriod::Quarter:
		case ibTotalsPeriod::HalfYear:
		case ibTotalsPeriod::Year: {
			const CivilDay a = CivilOf(BeginOfPeriod(period));
			const CivilDay b = CivilOf(to.BeginOfPeriod(period));
			const long months = (b.m_year - a.m_year) * 12 + (static_cast<long>(b.m_month) - static_cast<long>(a.m_month));
			switch (period) {
				case ibTotalsPeriod::Month:    return months;
				case ibTotalsPeriod::Quarter:  return months / 3;
				case ibTotalsPeriod::HalfYear: return months / 6;
				default:                       return months / 12;
			}
		}
	}
	return 0;
}

long ibDateTime::GetPart(ibDatePart part) const noexcept
{
	// Each part asks for what it needs and no more (the top of the file).
	unsigned hour, minute, second, millisecond;
	switch (part) {
		case ibDatePart::Year:      return CivilOf(*this).m_year;
		case ibDatePart::Quarter:   return static_cast<long>((CivilOf(*this).m_month - 1) / 3 + 1);
		case ibDatePart::Month:     return static_cast<long>(CivilOf(*this).m_month);
		case ibDatePart::Day:       return static_cast<long>(CivilOf(*this).m_day);
		case ibDatePart::DayOfYear: return static_cast<long>(YearDayOf(GetDays(), CivilOf(*this).m_year));
		case ibDatePart::WeekDay:   return static_cast<long>(WeekDayOf(GetDays()));
		// ISO 8601: week 1 is the week with the year's first Thursday; the days before it belong to the
		// last week (52 or 53) of the year before. Pinned here so every engine's own numbering is
		// irrelevant, and so the script's GetWeekOfYear cannot say otherwise.
		case ibDatePart::Week: {
			ibDateTimeParts parts;
			ToParts(parts);
			return static_cast<long>(parts.IsoWeek());
		}
		case ibDatePart::Hour:      TimeOf(*this, hour, minute, second, millisecond); return static_cast<long>(hour);
		case ibDatePart::Minute:    TimeOf(*this, hour, minute, second, millisecond); return static_cast<long>(minute);
		case ibDatePart::Second:    TimeOf(*this, hour, minute, second, millisecond); return static_cast<long>(second);
	}
	return 0;
}

bool ibDateTime::FromParts(int year, int month, int day, int hour, int minute, int second, int millisecond) noexcept
{
	if (month < 0 || day < 0 || hour < 0 || minute < 0 || second < 0 || millisecond < 0
		|| !IsADate(year, static_cast<unsigned>(month), static_cast<unsigned>(day), static_cast<unsigned>(hour),
		            static_cast<unsigned>(minute), static_cast<unsigned>(second), static_cast<unsigned>(millisecond)))
		return false;
	*this = ibDateTime(year, static_cast<unsigned>(month), static_cast<unsigned>(day), static_cast<unsigned>(hour),
		static_cast<unsigned>(minute), static_cast<unsigned>(second), static_cast<unsigned>(millisecond));
	return true;
}

bool ibDateTime::FromString(const ibString& text)
{
	switch (FromDigits(text)) {
	case DigitForm::Read:     return true;
	case DigitForm::Refused:  return false;   // one of the engine's forms naming no date — not a guess for wx
	case DigitForm::NotAForm: break;
	}
	// Not one of the digit forms: wx's free-form reader, by the local parts of what it read.
	const wxString freeForm = text.ToWxString();
	wxDateTime parsed;
	if (parsed.ParseDateTime(freeForm)) {
		*this = OfWxDateTime(parsed);
		return true;
	}
	if (parsed.ParseDate(freeForm)) {
		*this = OfWxDateTime(parsed.GetDateOnly());
		return true;
	}
	return false;
}

ibDateTime::DigitForm ibDateTime::FromDigits(const ibString& text) noexcept
{
	const wchar_t* const chars = text.wc_str();
	const size_t length = text.Len();
	size_t at = 0;
	const auto isDigit = [&](size_t i) { return i < length && chars[i] >= L'0' && chars[i] <= L'9'; };
	const auto digitAt = [&](size_t i) { return static_cast<long long>(chars[i] - L'0'); };
	// Up to `most` digits, at least `least`; the value read, the cursor past them.
	const auto digits = [&](size_t least, size_t most, long long& value) -> bool {
		size_t n = 0; value = 0;
		while (n < most && isDigit(at)) {
			value = value * 10 + digitAt(at);
			++at; ++n;
		}
		return n >= least;
	};
	const auto take = [&](wchar_t c) -> bool {
		if (at < length && chars[at] == c) { ++at; return true; }
		return false;
	};
	// The digits read are at most four a piece, so each fits an int; the calendar decides the rest. Once a
	// form's date is spelled, the text IS that form, and anything wrong after it is a refusal.
	const auto settle = [&](long long y, long long mo, long long d, long long h, long long mi, long long s, long long ms) {
		return FromParts(static_cast<int>(y), static_cast<int>(mo), static_cast<int>(d), static_cast<int>(h),
			static_cast<int>(mi), static_cast<int>(s), static_cast<int>(ms)) ? DigitForm::Read : DigitForm::Refused;
	};
	long long year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, millisecond = 0;

	// A run of digits alone: yyyymmdd or yyyymmddhhmmss.
	size_t run = 0;
	while (isDigit(run)) ++run;
	if (run == length && (run == 8 || run == 14)) {
		const auto piece = [&](size_t from, size_t n) {
			long long v = 0;
			for (size_t i = from; i < from + n; ++i) v = v * 10 + digitAt(i);
			return v;
		};
		year = piece(0, 4); month = piece(4, 2); day = piece(6, 2);
		if (run == 14) { hour = piece(8, 2); minute = piece(10, 2); second = piece(12, 2); }
		return settle(year, month, day, hour, minute, second, 0);
	}

	// dd.mm.yyyy[ hh:mm:ss]
	at = 0;
	if (digits(1, 2, day) && take(L'.') && digits(1, 2, month) && take(L'.') && digits(4, 4, year)) {
		if (at == length)
			return settle(year, month, day, 0, 0, 0, 0);
		while (take(L' ')) {}
		if (digits(1, 2, hour) && take(L':') && digits(1, 2, minute) && take(L':') && digits(1, 2, second) && at == length)
			return settle(year, month, day, hour, minute, second, 0);
		return DigitForm::Refused;
	}

	// yyyy-mm-dd[ hh:mm[:ss[.fraction]]]  (ISO 8601, `T` or a space between the halves)
	at = 0;
	if (digits(4, 4, year) && take(L'-') && digits(2, 2, month) && take(L'-') && digits(2, 2, day)) {
		if (at == length)
			return settle(year, month, day, 0, 0, 0, 0);
		if (!(take(L' ') || take(L'T')))
			return DigitForm::Refused;
		if (!(digits(2, 2, hour) && take(L':') && digits(2, 2, minute)))
			return DigitForm::Refused;
		if (take(L':')) {
			if (!digits(2, 2, second))
				return DigitForm::Refused;
			if (take(L'.')) {
				// The first three digits of the fraction are the milliseconds; the rest is finer than the reading.
				size_t n = 0; long long fraction = 0;
				while (isDigit(at)) { if (n < 3) fraction = fraction * 10 + digitAt(at); ++at; ++n; }
				if (n == 0) return DigitForm::Refused;
				while (n < 3) { fraction *= 10; ++n; }
				millisecond = fraction;
			}
		}
		return at == length ? settle(year, month, day, hour, minute, second, millisecond) : DigitForm::Refused;
	}
	return DigitForm::NotAForm;
}

ibString ibDateTime::ToString() const
{
	// Written digit by digit on the stack and taken over once: a date is printed for every cell a list or a
	// report shows, so no format is parsed and no second string is built for it. A year outside 1..9999
	// (before the empty date, or past the axis) is printed by the general formatter, sign and all.
	const CivilDay day = CivilOf(*this);
	unsigned hour, minute, second, millisecond;
	TimeOf(*this, hour, minute, second, millisecond);
	wchar_t text[48];
	if (day.m_year < 1 || day.m_year > 9999) {
		const int length = std::swprintf(text, sizeof(text) / sizeof(text[0]), L"%02u.%02u.%04d %02u:%02u:%02u",
			day.m_day, day.m_month, day.m_year, hour, minute, second);
		return length > 0 ? ibString(text, static_cast<size_t>(length)) : ibString();
	}
	wchar_t* at = text;
	at = PutTwo(at, day.m_day);    *at++ = L'.';
	at = PutTwo(at, day.m_month);  *at++ = L'.';
	at = PutFour(at, static_cast<unsigned>(day.m_year)); *at++ = L' ';
	at = PutTwo(at, hour);         *at++ = L':';
	at = PutTwo(at, minute);       *at++ = L':';
	at = PutTwo(at, second);
	return ibString(text, static_cast<size_t>(at - text));
}
