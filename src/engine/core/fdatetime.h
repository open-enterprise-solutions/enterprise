#ifndef __FDATETIME_H__
#define __FDATETIME_H__

#include "core/core.h"    // CORE_API

class ibString;                          // core/fstring.h - the date's text; the family meets in backend_core.h
class WXDLLIMPEXP_FWD_BASE wxDateTime;   // the bridge at the edges, below - <wx/datetime.h> stays with its callers

// ibDateTime — a date and a time of day, 8 bytes: a WALL-CLOCK READING. The engine's own date, as
// ibNumber is its number and ibString its text: the server works with dates in it, and wxDateTime is
// left to the windows.
//
// Built for the hot path. An order, an equality, a shift and a span are one integer operation on the
// count and live in this header, constexpr, so every caller - a sort, a keyed lookup, a period fold
// over a hundred thousand rows - sees them inline; no copy costs more than a word, and nothing is on
// the heap. Only the calendar's division into parts and the text stay in fdatetime.cpp.
//
// What a calendar and a clock on the wall show - `2026-03-29 02:30:00` - and nothing else: no zone
// in it. That is the reading a TIMESTAMP holds and the reading the reference system's Date is. It is
// counted as milliseconds from 0001-01-01 00:00:00 of the proleptic Gregorian calendar, by integer
// arithmetic that never asks the machine what its clock says or which zone it stands in: the same
// parts give the same number on every machine, and the same number gives the same parts. Every day
// is 86 400 seconds on this calendar, so a shift and a span are plain arithmetic on the count.
//
// ALL-ZERO BITS ARE THE EMPTY DATE: ibDateTime() is 0001-01-01 00:00:00, so a zeroed word is a
// valid, empty ibDateTime - which is what lets ibValue keep one in its union beside the pointers (the
// number 0 is all-zero bits in fnumber.h for the same reason). It used to be a literal,
// -62135604000000: the INSTANT of that midnight on a machine two hours east of Greenwich, taken off one
// such machine, so on any other clock `Date(1,1,1)` was not empty and an empty date written in one zone
// read as 02:00 in another (measured 2026-09-25). A stored form older than this class still holds it.
//
// The count is the class's own. It goes in and out only where a date is STORED as a number - a data
// node, the AOT cache, a job's schedule, a dump - through GetValue and the explicit constructor;
// everything else asks the date itself: its parts, its text, a shift, a span, an order.
//
// ⭐ THE BRIDGE TO wxDateTime, and where it stands. wx keeps an INSTANT - milliseconds of real time,
// read through the machine's zone - and the server holds none: its jobs, sessions, locks and settings
// keep this date. What still meets one is a WINDOW (a picker, a list that prints a time), wx's free-form
// reader of a text no digit door knows, and a stored form written before this class. The date crosses
// by its LOCAL PARTS and by nothing else: the reading 10:30 is the wxDateTime whose local time is 10:30,
// on whichever machine, and back. The one place the bridge is not an identity is a local time that does not exist on the
// machine (02:30 on the morning its clocks go forward): wx moves that instant an hour on, and a
// reading carried across and back has moved with it. Nothing of the value's goes over it.

// The parts of a reading, with the day's place in the week and the year.
struct ibDateTimeParts {
	int      m_year = 1;
	unsigned m_month = 1;         // 1..12
	unsigned m_day = 1;           // 1..31
	unsigned m_hour = 0;          // 0..23
	unsigned m_minute = 0;        // 0..59
	unsigned m_second = 0;        // 0..59
	unsigned m_millisecond = 0;   // 0..999
	unsigned m_weekDay = 1;       // 1..7, Monday first - ISO; 0001-01-01 was a Monday
	unsigned m_yearDay = 1;       // 1..366

	// ⭐ THE DAY'S PLACE, answered off these parts with no second reading of the date - what a calendar
	// that picks days (a job's schedule: "the last day", "the second Tuesday", "09:00-18:00") asks, once
	// per moment it tests. Defined below the class.
	unsigned MinuteOfDay() const noexcept;            // 0..1439
	unsigned DaysToMonthEnd() const noexcept;         // 0 on the month's last day
	unsigned WeekDayOccurrence() const noexcept;      // 1..5: the second Tuesday of its month is 2
	bool     IsLastWeekDayOfMonth() const noexcept;   // no later day of the same weekday in its month
	unsigned IsoWeek() const noexcept;                // 1..53, ISO 8601: the week holding the year's first Thursday is 1
};

// A STRETCH OF THE CALENDAR — "the minute / week / month / … holding this reading": what a register
// totals by, what a query truncates to (BEGINOFPERIOD), what a script begins a month with.
//
// ORDERED COARSENING, and that ordering is a CONTRACT: every member is coarser than the one
// before it. Consumers rely on it to decide derivability — a value truncated to unit A can be
// re-truncated to any B >= A, and to nothing finer, because the finer information is gone.
// Insert new members in order.
//
// (Not to be confused with ibPeriodicity — that is the granularity of a record KEY, a different
// question that happens to share a word.)
enum class ibTotalsPeriod
{
	Second,
	Minute,
	Hour,
	Day,
	Week,       // starts Monday (ISO) on every engine — the truncations encode that per dialect
	TenDays,    // the 1st / 11th / 21st of the month; the last one runs 8-11 days
	Month,
	Quarter,
	HalfYear,
	Year
};

// ⭐ ONE PIECE OF A DATE, AS A NUMBER — what `YEAR(x)` / `WEEKDAY(x)` answer with. A different
// question from ibTotalsPeriod, which names a STRETCH of time and answers with a date: truncating to
// the month gives the 1st of it, taking the month gives 9. They share most of their words and none
// of their meaning, so they are separate enums rather than one with two readings.
//
// WeekDay is Monday = 1 … Sunday = 7 (ISO), pinned here rather than left to each engine's own
// numbering — a rule that differs per dialect is a report that reads differently per deployment.
enum class ibDatePart
{
	Year,
	Quarter,
	Month,
	DayOfYear,
	Day,
	Week,
	WeekDay,
	Hour,
	Minute,
	Second
};

// Days since 1970-01-01 of a calendar day, proleptic Gregorian, any year: the standard days-from-civil
// arithmetic, whole integers over (year, month, day) - the integer heart of the count below. Outside
// the class, because a constexpr member cannot be called in the class's own static initializers.
constexpr long long ibDaysFromCivil(long long year, unsigned month, unsigned day) noexcept
{
	year -= month <= 2 ? 1 : 0;
	const long long era = (year >= 0 ? year : year - 399) / 400;
	const unsigned long long yoe = static_cast<unsigned long long>(year - era * 400);                 // [0, 399]
	const unsigned long long doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;           // [0, 365]
	const unsigned long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                              // [0, 146096]
	return era * 146097 + static_cast<long long>(doe) - 719468;
}

class CORE_API ibDateTime
{
public:
	// The empty date, 0001-01-01 00:00:00.
	constexpr ibDateTime() noexcept : m_value(0) {}

	// The reading of these parts, taken as written: a 31st of April is the 1st of May here. A door that
	// makes a date out of what a person or a program handed over asks IsADate first, so a 30th of
	// February is refused (or is the empty date) rather than rolled over into March.
	constexpr ibDateTime(int year, unsigned month, unsigned day,
		unsigned hour = 0, unsigned minute = 0, unsigned second = 0, unsigned millisecond = 0) noexcept
		: m_value((ibDaysFromCivil(year, month, day) - s_emptyDay) * s_msPerDay
			+ ((static_cast<long long>(hour) * 60 + minute) * 60 + second) * 1000 + millisecond) {}

	// The count itself - milliseconds from the empty date - for a date stored as a number.
	constexpr explicit ibDateTime(long long value) noexcept : m_value(value) {}
	constexpr long long GetValue() const noexcept { return m_value; }

	// NOW: what the machine's clock on the wall shows - the clock's reading in the machine's zone, by its
	// parts, to the millisecond, with no wx on the way. The one door the engine asks "now" through, so the
	// day "now" becomes the base's clock it changes here and nowhere else.
	static ibDateTime Now();

	// The bridge (above): the reading of an instant's LOCAL parts, and the wxDateTime whose local parts
	// are this reading's. The empty date and an invalid wxDateTime are each other's - "no date" on
	// either side - so a NULL column, an unset bound or a cleared picker crosses both ways as itself.
	static ibDateTime OfWxDateTime(const wxDateTime& moment);
	wxDateTime ToWxDateTime() const;

	constexpr bool IsEmpty() const noexcept { return m_value == 0; }

	// Whether the parts name a day and a time the calendar has. Any year.
	static constexpr bool IsADate(long long year, unsigned month, unsigned day,
		unsigned hour = 0, unsigned minute = 0, unsigned second = 0, unsigned millisecond = 0) noexcept {
		return month >= 1 && month <= 12 && day >= 1 && day <= DaysInMonth(year, month)
			&& hour <= 23 && minute <= 59 && second <= 59 && millisecond <= 999;
	}

	// The days a month has, leap years included. Months outside 1..12 have none.
	static constexpr unsigned DaysInMonth(long long year, unsigned month) noexcept {
		return month == 2 ? ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0 ? 29u : 28u)
			: (month == 4 || month == 6 || month == 9 || month == 11) ? 30u
			: (month >= 1 && month <= 12) ? 31u : 0u;
	}

	// The parts. Defined for any count: a reading before year 1 or past 9999 gets the year the calendar
	// gives it (0, -1, 10000), so a caller printing it sees where it is.
	void ToParts(ibDateTimeParts& parts) const noexcept;

	// Whole days from the empty date - the day this reading falls on, counted down from its midnight.
	constexpr long long GetDays() const noexcept {
		return m_value >= 0 ? m_value / s_msPerDay : -((-m_value + s_msPerDay - 1) / s_msPerDay);
	}

	// Midnight of the day this reading falls on.
	constexpr ibDateTime GetDayStart() const noexcept { return ibDateTime(GetDays() * s_msPerDay); }

	// Whole seven-day weeks from the day `from` falls on to the day this one falls on - floor, negative
	// before it; the time of day moves nothing. What "every second week, counted from an anchor" counts.
	constexpr long long WeeksSince(const ibDateTime& from) const noexcept {
		const long long days = GetDays() - from.GetDays();
		return (days >= 0 ? days : days - 6) / 7;
	}

	constexpr ibDateTime AddMilliseconds(long long milliseconds) const noexcept { return ibDateTime(m_value + milliseconds); }
	constexpr ibDateTime AddDays(long long days) const noexcept { return ibDateTime(m_value + days * s_msPerDay); }

	// The span between two readings, in milliseconds - negative when `earlier` is the later one.
	friend constexpr long long operator-(const ibDateTime& later, const ibDateTime& earlier) noexcept { return later.m_value - earlier.m_value; }

	// REAL TIME between two readings of THIS machine's clock, in milliseconds: through its zone, so the hour
	// its clocks skip or repeat counts as the hour it was. The wall's own span (`-`) cannot answer that across
	// a clock change - a beat written at 01:59:59 is a second old at 03:00:00 on the morning the clocks go
	// forward, not an hour - and liveness, which asks exactly this, must not mistake a live peer for a dead
	// one. The one question besides Now that asks the machine; outside the machine's calendar (a year it
	// cannot place) it answers the wall's span.
	long long ElapsedSince(const ibDateTime& earlier) const noexcept;

	// ⭐⭐ THE CALENDAR, ASKED OF THE DATE. The script's BegOfMonth / AddMonth / Year, the query's
	// BEGINOFPERIOD / DATEADD / DATEDIFF / YEAR and the RAM twins of the dialect's expressions all read
	// these, so a period a script begins, a period a query folds in memory and the period the server
	// folds are one answer. The SQL twins MUST agree with the dialect exactly - or a query answers
	// differently depending on whether it pushed down, which shows up as totals that reconcile in one
	// deployment and not in another - so the calendar units walk the calendar (month lengths, leap
	// years, the ten-day bucket that ends a month) rather than approximating with fixed-length steps.

	// The period holding the reading, at its start - `BEGINOFPERIOD(x, Month)`, BegOfMonth.
	ibDateTime BeginOfPeriod(ibTotalsPeriod period) const noexcept;
	// The start of the NEXT period - the first reading a stored row of that grain no longer covers. A
	// read whose lower boundary falls inside a grain cannot use that grain's stored row (it holds the
	// part before the boundary too), so it starts here and takes the head from the movements instead.
	ibDateTime BeginOfNextPeriod(ibTotalsPeriod period) const noexcept;
	// The LAST second the period still covers - `ENDOFPERIOD(x, Month)`, EndOfMonth: the start of the
	// next period less one second, said ONCE so no road can disagree about whether the boundary belongs
	// to the period (it does).
	ibDateTime EndOfPeriod(ibTotalsPeriod period) const noexcept;
	// Moved by whole periods - `DATEADD(x, Month, 3)`, AddMonth. A month after the 31st of a 31-day
	// month is the last day of a shorter one, which is what a person means by "a month later"; the time
	// of day travels with the date.
	ibDateTime AddPeriods(ibTotalsPeriod period, long count) const noexcept;
	// How many WHOLE periods lie from this reading to `to` - `DATEDIFF(x, to, Day)`. Negative when `to`
	// is earlier, zero when both fall in the same period.
	long PeriodsUntil(const ibDateTime& to, ibTotalsPeriod period) const noexcept;
	// One piece as a number - `YEAR(x)`, `WEEKDAY(x)`, Year / WeekOfYear; the week is ISO 8601's.
	long GetPart(ibDatePart part) const noexcept;

	friend constexpr bool operator==(const ibDateTime& a, const ibDateTime& b) noexcept { return a.m_value == b.m_value; }
	friend constexpr bool operator!=(const ibDateTime& a, const ibDateTime& b) noexcept { return a.m_value != b.m_value; }
	friend constexpr bool operator< (const ibDateTime& a, const ibDateTime& b) noexcept { return a.m_value <  b.m_value; }
	friend constexpr bool operator> (const ibDateTime& a, const ibDateTime& b) noexcept { return a.m_value >  b.m_value; }
	friend constexpr bool operator<=(const ibDateTime& a, const ibDateTime& b) noexcept { return a.m_value <= b.m_value; }
	friend constexpr bool operator>=(const ibDateTime& a, const ibDateTime& b) noexcept { return a.m_value >= b.m_value; }

	// ⭐ THE TWO DOORS A DATE COMES IN THROUGH, and what they refuse.
	//
	// Parts a person or a program hands over (a script's Date(y, m, d, …), a value built from parts) are
	// checked against the calendar first: a 30th of February or a 25th hour is refused and the date left
	// alone - never rolled over into the day after, and never whatever a rolled-over clock makes of it.
	bool FromParts(int year, int month, int day, int hour = 0, int minute = 0, int second = 0, int millisecond = 0) noexcept;

	// A text. The engine's own forms are read by their DIGITS and judged by them alone: `dd.mm.yyyy[ hh:mm:ss]`
	// (one or two digits a piece, four for the year), `yyyymmdd`, `yyyymmddhhmmss`, and ISO 8601 as the engines
	// write a TIMESTAMP - `yyyy-mm-dd[ hh:mm[:ss[.fraction]]]`, a `T` allowed for the space - with no clock
	// consulted, so 02:30 on the morning a machine's clocks go forward reads as 02:30, and a day the calendar
	// does not have is refused, never guessed. A text in none of these forms goes to wx's free-form reader
	// and comes back by its local parts (the bridge). Refused: the date is left alone. The one door for "this
	// text as a date" - a value's string, a driver's TIMESTAMP text, a script's Date("…").
	bool FromString(const ibString& text);

	// `dd.mm.yyyy hh:mm:ss` - the text a date value gives.
	ibString ToString() const;

private:
	// The digit forms of FromString: read, refused (the text IS one of them and names no date), or not theirs.
	enum class DigitForm { Read, Refused, NotAForm };
	DigitForm FromDigits(const ibString& text) noexcept;

	static constexpr long long s_msPerDay = 86400000ll;

	// The day the count starts on, in the days-from-civil numbering.
	static constexpr long long s_emptyDay = ibDaysFromCivil(1, 1, 1);

	long long m_value;
};

inline unsigned ibDateTimeParts::MinuteOfDay() const noexcept { return m_hour * 60 + m_minute; }
inline unsigned ibDateTimeParts::DaysToMonthEnd() const noexcept { return ibDateTime::DaysInMonth(m_year, m_month) - m_day; }
inline unsigned ibDateTimeParts::WeekDayOccurrence() const noexcept { return (m_day - 1) / 7 + 1; }
inline bool ibDateTimeParts::IsLastWeekDayOfMonth() const noexcept { return DaysToMonthEnd() < 7; }

// Week 1 holds January 4th, and a year has 53 weeks when it begins on a Thursday, or on a Wednesday in a leap
// year: the last days of December can belong to week 1 of the next year and the first days of January to the
// last week of the one before - the rule wxDateTime::GetWeekOfYear(Monday_First) applies.
inline unsigned ibDateTimeParts::IsoWeek() const noexcept
{
	const auto weeksOf = [](long long year) -> unsigned {
		const long long jan1 = ibDaysFromCivil(year, 1, 1) - ibDaysFromCivil(1, 1, 1);   // from 0001-01-01, a Monday
		const unsigned weekDay = static_cast<unsigned>((jan1 % 7 + 7) % 7 + 1);
		return weekDay == 4 || (weekDay == 3 && ibDateTime::DaysInMonth(year, 2) == 29) ? 53u : 52u;
	};
	const long long week = (static_cast<long long>(m_yearDay) - m_weekDay + 10) / 7;
	return week < 1 ? weeksOf(m_year - 1)
	     : week > weeksOf(m_year) ? 1u : static_cast<unsigned>(week);
}

static_assert(sizeof(ibDateTime) == 8, "a date is one word");
static_assert(ibDateTime(1, 1, 1).IsEmpty(), "the empty date is 0001-01-01 00:00:00");

#endif
