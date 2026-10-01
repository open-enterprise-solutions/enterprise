// =============================================================================
// The engine's own date — backend/fdatetime.h: a wall-clock reading counted in
// milliseconds from 0001-01-01, and the calendar it answers.
//
// What is pinned: that the count and the parts are exact inverses on the whole
// axis, that the empty date is the zero the header says, and that the calendar
// answers what wxDateTime answers wherever the two CAN agree — every date of a
// leap year at noon, through every unit and every part. The reference the
// calendar is held to is thus the older implementation itself, over a grid
// where a clock change cannot reach it; the places where the two are DESIGNED
// to differ (an hour across a clock change) are outside the grid on purpose and
// are stated in fdatetime.h.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/backend_core.h"   // ibDateTime, and the ibString its text is

#include <wx/datetime.h>

#include <cstdio>    // snprintf - the text of a reading in a failure message
#include <string>
#include <vector>

// ---------------------------------------------------------------- the count

static_assert(ibDateTime().IsEmpty(), "the empty date is the zero count");
static_assert(ibDateTime(1, 1, 1).GetValue() == 0, "counted from 0001-01-01 00:00:00");
static_assert(ibDateTime(1, 1, 2).GetValue() == 86400000ll, "a day on the wall is 86 400 000 of them");
static_assert(ibDateTime(1970, 1, 1).GetValue() == 62135596800000ll, "719 162 days to 1970, leap days counted");
static_assert(ibDateTime(1970, 1, 1, 0, 0, 1) - ibDateTime(1970, 1, 1) == 1000, "a second is 1000");
static_assert(ibDateTime(9999, 12, 31, 23, 59, 59, 999).GetValue() == 315537897599999ll, "the last reading of the axis");
static_assert(ibDateTime(2026, 3, 29).AddDays(1) == ibDateTime(2026, 3, 30), "a day is a day, whatever the clocks do");
static_assert(ibDateTime(2026, 3, 29, 17, 5).GetDayStart() == ibDateTime(2026, 3, 29), "");
static_assert(ibDateTime(2026, 3, 29) < ibDateTime(2026, 3, 29, 0, 0, 0, 1), "");

namespace {

ibDateTimeParts Parts(const ibDateTime& date)
{
	ibDateTimeParts parts;
	date.ToParts(parts);
	return parts;
}

ibDateTime DateOf(const ibDateTimeParts& p)
{
	return ibDateTime(p.m_year, p.m_month, p.m_day, p.m_hour, p.m_minute, p.m_second, p.m_millisecond);
}

bool SameParts(const ibDateTimeParts& a, const ibDateTimeParts& b)
{
	return a.m_year == b.m_year && a.m_month == b.m_month && a.m_day == b.m_day && a.m_hour == b.m_hour
		&& a.m_minute == b.m_minute && a.m_second == b.m_second && a.m_millisecond == b.m_millisecond;
}

std::string Text(const ibDateTimeParts& p)
{
	char buf[40];
	snprintf(buf, sizeof buf, "%04d-%02u-%02u %02u:%02u:%02u.%03u", p.m_year, p.m_month, p.m_day, p.m_hour, p.m_minute, p.m_second, p.m_millisecond);
	return buf;
}

// A wxDateTime with these parts on the machine's own clock - what wx's own calendar reads.
wxDateTime Wx(int year, unsigned month, unsigned day, unsigned hour = 0, unsigned minute = 0, unsigned second = 0)
{
	return wxDateTime(static_cast<wxDateTime::wxDateTime_t>(day), static_cast<wxDateTime::Month>(month - 1), year,
		static_cast<wxDateTime::wxDateTime_t>(hour), static_cast<wxDateTime::wxDateTime_t>(minute),
		static_cast<wxDateTime::wxDateTime_t>(second));
}

// The wall-clock reading a wxDateTime shows - its parts, not its instant.
ibDateTime DateOf(const wxDateTime& dt)
{
	const wxDateTime::Tm tm = dt.GetTm();
	return ibDateTime(tm.year, static_cast<unsigned>(tm.mon) + 1, tm.mday, tm.hour, tm.min, tm.sec, tm.msec);
}

bool IsLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

unsigned DaysIn(int y, unsigned m)
{
	static const unsigned days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	return m == 2 && IsLeap(y) ? 29u : days[m - 1];
}

} // namespace

// -------------------------------------------------------------- the parts

TEST(DateTime, EveryDayOfFourYearsRoundTripsAndCountsUp)
{
	const int years[] = { 1, 1899, 1969, 1970, 2000, 2024, 9999 };
	for (const int year : years) {
		ibDateTime previous = ibDateTime(year, 1, 1).AddMilliseconds(-1);
		for (unsigned month = 1; month <= 12; ++month) {
			for (unsigned day = 1; day <= DaysIn(year, month); ++day) {
				const ibDateTimeParts written = { year, month, day, 12, 34, 56, 789 };
				const ibDateTime date = DateOf(written);
				EXPECT_GT(date, previous) << Text(written);
				previous = date;
				const ibDateTimeParts read = Parts(date);
				EXPECT_TRUE(SameParts(written, read)) << Text(written) << " read back as " << Text(read);
			}
		}
	}
}

TEST(DateTime, MidnightAndTheLastMillisecondOfADay)
{
	// Below the empty date the count floors to the day that began before it - year 0 of the
	// proleptic calendar - the way a calendar reads, not the way `/` truncates toward zero.
	const ibDateTimeParts first = { 0, 12, 31, 0, 0, 0, 0 };
	const ibDateTimeParts last  = { 0, 12, 31, 23, 59, 59, 999 };
	EXPECT_EQ(-86400000ll, DateOf(first).GetValue());
	EXPECT_EQ(-1ll, DateOf(last).GetValue());
	EXPECT_TRUE(SameParts(first, Parts(ibDateTime(-86400000ll))));
	EXPECT_TRUE(SameParts(last, Parts(ibDateTime(-1ll))));
	EXPECT_EQ(1, Parts(ibDateTime()).m_year);
	EXPECT_EQ(0u, Parts(ibDateTime()).m_hour);
	EXPECT_EQ(-1, ibDateTime(-1ll).GetDays());
	EXPECT_EQ(ibDateTime(-86400000ll), ibDateTime(-1ll).GetDayStart());
}

TEST(DateTime, TheDayCountAgreesWithWxOnAGridOfDates)
{
	// wx counts the same days when asked for the day of the year and the weekday; those are the
	// two readings a calendar mistake would show up in first.
	const int years[] = { 1900, 1999, 2000, 2024, 2100 };
	for (const int year : years) {
		for (unsigned month = 1; month <= 12; ++month) {
			for (unsigned day = 1; day <= DaysIn(year, month); day += 5) {
				const wxDateTime dt = Wx(year, month, day, 12);
				const ibDateTime date(year, month, day, 12);
				EXPECT_EQ(static_cast<long>(dt.GetDayOfYear()), date.GetPart(ibDatePart::DayOfYear)) << year << '-' << month << '-' << day;
				const int wd = static_cast<int>(dt.GetWeekDay());
				EXPECT_EQ(wd == wxDateTime::Sun ? 7L : static_cast<long>(wd), date.GetPart(ibDatePart::WeekDay)) << year << '-' << month << '-' << day;
			}
		}
	}
}

// ---------------------------------------------------------- the calendar

// Every date of a leap year at noon, and the turns of two years: the calendar answers what wx's own
// calendar answers where wx has a word - the month and year steps (wxDateSpan, which clamps the day as
// DATEADD does), the week's Monday, the ISO week, the weekday, the day of the year. Noon, so that
// nothing here can cross the hour a clock changes on the machine running the test: the reading is
// asked through wx's LOCAL parts, and a skipped hour is the one place those are not a mirror.
TEST(DateTime, TheCalendarAgreesWithWxWhereWxIsAsked)
{
	std::vector<ibDateTimeParts> grid;
	for (unsigned month = 1; month <= 12; ++month)
		for (unsigned day = 1; day <= DaysIn(2024, month); ++day)
			grid.push_back({ 2024, month, day, 12, 0, 0, 0 });
	// The turns of two years, where the ISO week counts from the year before or into the next.
	for (unsigned day = 25; day <= 31; ++day) grid.push_back({ 2020, 12, day, 12, 0, 0, 0 });
	for (unsigned day = 1;  day <= 10; ++day) grid.push_back({ 2021, 1,  day, 12, 0, 0, 0 });
	for (unsigned day = 25; day <= 31; ++day) grid.push_back({ 2024, 12, day, 12, 0, 0, 0 });
	for (unsigned day = 1;  day <= 10; ++day) grid.push_back({ 2025, 1,  day, 12, 0, 0, 0 });

	for (const ibDateTimeParts& p : grid) {
		const wxDateTime dt = Wx(p.m_year, p.m_month, p.m_day, p.m_hour);
		const ibDateTime date = DateOf(p);
		ASSERT_EQ(date, DateOf(dt)) << Text(p);
		for (const long count : { 1L, -1L, 3L, -3L, 11L, -13L }) {
			EXPECT_EQ(DateOf(dt + wxDateSpan::Months(static_cast<int>(count))), date.AddPeriods(ibTotalsPeriod::Month, count)) << Text(p) << " months " << count;
			EXPECT_EQ(DateOf(dt + wxDateSpan::Years(static_cast<int>(count))),  date.AddPeriods(ibTotalsPeriod::Year,  count)) << Text(p) << " years " << count;
			EXPECT_EQ(DateOf(dt + wxDateSpan::Days(static_cast<int>(count))),   date.AddPeriods(ibTotalsPeriod::Day,   count)) << Text(p) << " days " << count;
			EXPECT_EQ(DateOf(dt + wxDateSpan::Weeks(static_cast<int>(count))),  date.AddPeriods(ibTotalsPeriod::Week,  count)) << Text(p) << " weeks " << count;
		}
		const int wd = static_cast<int>(dt.GetWeekDay());
		const long isoWeekDay = wd == wxDateTime::Sun ? 7L : static_cast<long>(wd);
		EXPECT_EQ(isoWeekDay, date.GetPart(ibDatePart::WeekDay)) << Text(p);
		EXPECT_EQ(static_cast<long>(dt.GetDayOfYear()), date.GetPart(ibDatePart::DayOfYear)) << Text(p);
		EXPECT_EQ(static_cast<long>(dt.GetWeekOfYear(wxDateTime::Monday_First)), date.GetPart(ibDatePart::Week)) << Text(p);
		EXPECT_EQ(DateOf((dt - wxDateSpan::Days(static_cast<int>(isoWeekDay) - 1)).GetDateOnly()), date.BeginOfPeriod(ibTotalsPeriod::Week)) << Text(p);
		EXPECT_EQ(DateOf(wxDateTime(1, dt.GetMonth(), dt.GetYear())), date.BeginOfPeriod(ibTotalsPeriod::Month)) << Text(p);
		EXPECT_EQ(DateOf(wxDateTime(1, wxDateTime::Jan, dt.GetYear())), date.BeginOfPeriod(ibTotalsPeriod::Year)) << Text(p);
		EXPECT_EQ(DateOf(dt.GetDateOnly()), date.BeginOfPeriod(ibTotalsPeriod::Day)) << Text(p);
	}
}

// ------------------------------------------------------------- the bridge

// A reading crosses to wxDateTime by its local parts and comes back the same reading; an invalid
// wxDateTime comes over as the empty date. Asked at noon and at midnight, on days of every season,
// so the machine's own clock change is not on the grid (it is the one thing the bridge cannot mirror).
TEST(DateTime, TheBridgeCarriesAReadingByItsParts)
{
	const ibDateTimeParts readings[] = {
		{ 1, 1, 1, 0, 0, 1, 0 }, { 1899, 12, 31, 23, 59, 59, 999 }, { 1969, 12, 31, 12, 0, 0, 0 }, { 1970, 1, 1, 0, 0, 0, 0 },
		{ 2024, 2, 29, 12, 0, 0, 0 }, { 2026, 1, 15, 0, 0, 0, 0 }, { 2026, 7, 15, 12, 30, 45, 250 }, { 2026, 10, 25, 12, 0, 0, 0 },
		{ 9999, 12, 31, 23, 59, 59, 999 },
	};
	for (const ibDateTimeParts& p : readings) {
		const ibDateTime date = DateOf(p);
		const wxDateTime dt = date.ToWxDateTime();
		ASSERT_TRUE(dt.IsValid()) << Text(p);
		const wxDateTime::Tm tm = dt.GetTm();
		EXPECT_EQ(p.m_year, tm.year) << Text(p);
		EXPECT_EQ(p.m_month, static_cast<unsigned>(tm.mon) + 1) << Text(p);
		EXPECT_EQ(p.m_day, static_cast<unsigned>(tm.mday)) << Text(p);
		EXPECT_EQ(p.m_hour, static_cast<unsigned>(tm.hour)) << Text(p);
		EXPECT_EQ(p.m_minute, static_cast<unsigned>(tm.min)) << Text(p);
		EXPECT_EQ(p.m_second, static_cast<unsigned>(tm.sec)) << Text(p);
		EXPECT_EQ(p.m_millisecond, static_cast<unsigned>(tm.msec)) << Text(p);
		EXPECT_EQ(date, ibDateTime::OfWxDateTime(dt)) << Text(p);
	}
	EXPECT_TRUE(ibDateTime::OfWxDateTime(wxDateTime()).IsEmpty());
	EXPECT_TRUE(ibDateTime::OfWxDateTime(wxInvalidDateTime).IsEmpty());
	EXPECT_FALSE(ibDateTime().ToWxDateTime().IsValid()) << "the empty date crosses back as no date";
}

// "Now" is what the machine's clock on the wall shows - the same reading wx's local parts give, under
// whichever of the three clocks the suite runs.
TEST(DateTime, NowIsWhatTheWallClockShows)
{
	const ibDateTime before = ibDateTime::OfWxDateTime(wxDateTime::Now());
	const ibDateTime now = ibDateTime::Now();
	const ibDateTime after = ibDateTime::OfWxDateTime(wxDateTime::Now());
	EXPECT_FALSE(now.IsEmpty());
	EXPECT_LE(before.AddMilliseconds(-1000), now);   // a second's slack: wx reads the clock by its own road
	EXPECT_LE(now, after.AddMilliseconds(1000));
}

// Real time between two readings of this machine's clock counts the hour its clocks skip as the hour it
// was - under whichever of the three clocks the suite runs, so the expected span is asked of wx's own
// instants. On a clock with no change the wall's span and the real one agree.
TEST(DateTime, ElapsedSinceIsRealTimeOnThisMachine)
{
	const ibDateTime before(2026, 3, 29, 0, 30), after(2026, 3, 29, 4, 30);   // the European change is between them
	const long long real = (after.ToWxDateTime() - before.ToWxDateTime()).GetMilliseconds().GetValue();
	EXPECT_EQ(real, after.ElapsedSince(before));
	EXPECT_EQ(-real, before.ElapsedSince(after));
	const ibDateTime quiet(2026, 7, 1, 12), later(2026, 7, 1, 12, 0, 1, 250);
	EXPECT_EQ(1250, later.ElapsedSince(quiet));
}

// ------------------------------------------------------------- the text

// A text in the engine's forms spells a reading by its digits - the reference system's forms and ISO 8601
// as the engines write a TIMESTAMP - and is judged by them alone: no clock, so the skipped hour reads as
// written, and no rolling over, so a day the calendar does not have is refused, not handed to a guesser.
TEST(DateTime, ATextSpellsAReadingByItsDigits)
{
	ibDateTime date;
	EXPECT_TRUE(date.FromString(wxT("2026-03-29 02:30:00")));      EXPECT_EQ(ibDateTime(2026, 3, 29, 2, 30), date);
	EXPECT_TRUE(date.FromString(wxT("2026-03-29T02:30:00")));      EXPECT_EQ(ibDateTime(2026, 3, 29, 2, 30), date);
	EXPECT_TRUE(date.FromString(wxT("2026-03-29 02:30")));         EXPECT_EQ(ibDateTime(2026, 3, 29, 2, 30), date);
	EXPECT_TRUE(date.FromString(wxT("2026-03-29")));               EXPECT_EQ(ibDateTime(2026, 3, 29), date);
	EXPECT_TRUE(date.FromString(wxT("2026-03-29 02:30:45.1234"))); EXPECT_EQ(ibDateTime(2026, 3, 29, 2, 30, 45, 123), date);
	EXPECT_TRUE(date.FromString(wxT("2026-03-29 02:30:45.5")));    EXPECT_EQ(ibDateTime(2026, 3, 29, 2, 30, 45, 500), date);
	EXPECT_TRUE(date.FromString(wxT("0001-01-01 00:00:00")));      EXPECT_TRUE(date.IsEmpty());
	EXPECT_TRUE(date.FromString(wxT("29.03.2026 2:30:00")));       EXPECT_EQ(ibDateTime(2026, 3, 29, 2, 30), date);
	EXPECT_TRUE(date.FromString(wxT("5.3.2026")));                 EXPECT_EQ(ibDateTime(2026, 3, 5), date);
	EXPECT_TRUE(date.FromString(wxT("20260329")));                 EXPECT_EQ(ibDateTime(2026, 3, 29), date);
	EXPECT_TRUE(date.FromString(wxT("20260329023000")));           EXPECT_EQ(ibDateTime(2026, 3, 29, 2, 30), date);
	EXPECT_TRUE(date.FromString(wxT("9999-12-31 23:59:59.999")));  EXPECT_EQ(ibDateTime(9999, 12, 31, 23, 59, 59, 999), date);

	const ibDateTime untouched(2026, 9, 30, 12);
	date = untouched;
	EXPECT_FALSE(date.FromString(wxT("2026-02-30")));            // no such day
	EXPECT_FALSE(date.FromString(wxT("2026-13-01")));
	EXPECT_FALSE(date.FromString(wxT("20260231")));
	EXPECT_FALSE(date.FromString(wxT("31.04.2026")));
	EXPECT_FALSE(date.FromString(wxT("2026-03-29 24:00:00")));
	EXPECT_FALSE(date.FromString(wxT("2026-03-29 02:30:00+02")));   // a zone is not a reading
	EXPECT_FALSE(date.FromString(wxT("29.03.2026 02:30")));       // the reference form carries the seconds
	EXPECT_FALSE(date.FromString(wxEmptyString));
	EXPECT_EQ(untouched, date);

	// Parts a program hands over: the same calendar, the same refusal.
	EXPECT_TRUE(date.FromParts(2024, 2, 29, 23, 59, 59));          EXPECT_EQ(ibDateTime(2024, 2, 29, 23, 59, 59), date);
	EXPECT_FALSE(date.FromParts(2023, 2, 29));
	EXPECT_FALSE(date.FromParts(2026, 1, 1, 24));
	EXPECT_FALSE(date.FromParts(2026, -1, 1));
	EXPECT_EQ(ibDateTime(2024, 2, 29, 23, 59, 59), date);

	EXPECT_EQ(wxString(wxT("29.03.2026 02:30:45")), ibDateTime(2026, 3, 29, 2, 30, 45).ToString());
	EXPECT_EQ(wxString(wxT("01.01.0001 00:00:00")), ibDateTime().ToString());

	static_assert(ibDateTime::IsADate(2024, 2, 29), "a leap day");
	static_assert(!ibDateTime::IsADate(2023, 2, 29), "not in a common year");
	static_assert(!ibDateTime::IsADate(2026, 4, 31) && !ibDateTime::IsADate(2026, 0, 1) && !ibDateTime::IsADate(2026, 1, 0), "");
	static_assert(ibDateTime::IsADate(2026, 1, 1, 23, 59, 59, 999) && !ibDateTime::IsADate(2026, 1, 1, 24) && !ibDateTime::IsADate(2026, 1, 1, 0, 60), "");
}

// The parts know the day's place: weekday (Monday 1), day of the year, ISO week - and the days a
// month has.
TEST(DateTime, ThePartsPlaceTheDayInItsWeekAndYear)
{
	EXPECT_EQ(4u, Parts(ibDateTime(1970, 1, 1)).m_weekDay);                      // a Thursday
	EXPECT_EQ(1u, Parts(ibDateTime(2024, 1, 1)).m_weekDay);                      // a Monday
	EXPECT_EQ(7u, Parts(ibDateTime(2024, 9, 1)).m_weekDay);                      // a Sunday
	EXPECT_EQ(1u, Parts(ibDateTime()).m_weekDay);                                // year 1 opened on a Monday
	EXPECT_EQ(366u, Parts(ibDateTime(2024, 12, 31, 23)).m_yearDay);
	EXPECT_EQ(60u, Parts(ibDateTime(2024, 2, 29)).m_yearDay);
	EXPECT_EQ(53u, Parts(ibDateTime(2021, 1, 3)).IsoWeek());                     // still week 53 of 2020
	EXPECT_EQ(1u, Parts(ibDateTime(2024, 12, 30)).IsoWeek());                    // already week 1 of 2025
	EXPECT_EQ(52u, Parts(ibDateTime(2023, 12, 31)).IsoWeek());
	EXPECT_EQ(53u, Parts(ibDateTime(2020, 12, 31)).IsoWeek());
	EXPECT_EQ(1u, Parts(ibDateTime(2026, 1, 1)).IsoWeek());                      // a Thursday
	EXPECT_EQ(53u, Parts(ibDateTime(2027, 1, 1)).IsoWeek());                     // a Friday: week 53 of 2026
	// …and its place in the month and the day, as a calendar that picks days asks it.
	EXPECT_EQ(0u, Parts(ibDateTime(2024, 2, 29)).DaysToMonthEnd());             // the last day
	EXPECT_EQ(28u, Parts(ibDateTime(2024, 2, 1)).DaysToMonthEnd());
	EXPECT_EQ(2u, Parts(ibDateTime(2026, 9, 8)).WeekDayOccurrence());           // the second Tuesday of September
	EXPECT_TRUE(Parts(ibDateTime(2026, 9, 29)).IsLastWeekDayOfMonth());          // the last Tuesday
	EXPECT_FALSE(Parts(ibDateTime(2026, 9, 22)).IsLastWeekDayOfMonth());
	EXPECT_EQ(9u * 60 + 30, Parts(ibDateTime(2026, 9, 8, 9, 30, 59)).MinuteOfDay());
	EXPECT_EQ(2, ibDateTime(2026, 1, 19, 8).WeeksSince(ibDateTime(2026, 1, 5, 23)));   // the time of day moves nothing
	EXPECT_EQ(-1, ibDateTime(2026, 1, 4).WeeksSince(ibDateTime(2026, 1, 5)));           // floor before the anchor
	static_assert(ibDateTime::DaysInMonth(2024, 2) == 29, "a leap year");
	static_assert(ibDateTime::DaysInMonth(1900, 2) == 28, "a century that is not");
	static_assert(ibDateTime::DaysInMonth(2000, 2) == 29, "a century that is");
	static_assert(ibDateTime::DaysInMonth(2026, 4) == 30 && ibDateTime::DaysInMonth(2026, 12) == 31, "");
	static_assert(ibDateTime::DaysInMonth(2026, 13) == 0 && ibDateTime::DaysInMonth(2026, 0) == 0, "no such month");
}

// The rules, stated on their own so a change to the calendar and to wx at once could not slip through
// the cross-check above.
TEST(DateTime, TheCalendarRulesStatedOutright)
{
	// A month added lands on the last day the month has, at the same time of day.
	EXPECT_EQ(ibDateTime(2024, 2, 29), ibDateTime(2024, 1, 31).AddPeriods(ibTotalsPeriod::Month, 1));
	EXPECT_EQ(ibDateTime(2023, 2, 28), ibDateTime(2023, 1, 31).AddPeriods(ibTotalsPeriod::Month, 1));
	EXPECT_EQ(ibDateTime(2023, 11, 30), ibDateTime(2024, 1, 31).AddPeriods(ibTotalsPeriod::Month, -2));
	EXPECT_EQ(ibDateTime(2025, 1, 31), ibDateTime(2024, 1, 31).AddPeriods(ibTotalsPeriod::Year, 1));
	EXPECT_EQ(ibDateTime(2026, 2, 28, 10, 30), ibDateTime(2026, 1, 31, 10, 30).AddPeriods(ibTotalsPeriod::Month, 1));
	// Ten-day buckets: the 1st, the 11th, the 21st - and the third runs to the end of the month.
	EXPECT_EQ(ibDateTime(2024, 1, 21), ibDateTime(2024, 1, 31, 8).BeginOfPeriod(ibTotalsPeriod::TenDays));
	EXPECT_EQ(ibDateTime(2024, 2, 1),  ibDateTime(2024, 1, 31, 8).BeginOfNextPeriod(ibTotalsPeriod::TenDays));
	EXPECT_EQ(ibDateTime(2024, 1, 11), ibDateTime(2024, 1, 5).BeginOfNextPeriod(ibTotalsPeriod::TenDays));
	EXPECT_EQ(3, ibDateTime(2024, 1, 5).PeriodsUntil(ibDateTime(2024, 2, 5), ibTotalsPeriod::TenDays));
	// Whole units between the units two readings fall in.
	EXPECT_EQ(1, ibDateTime(2024, 1, 31).PeriodsUntil(ibDateTime(2024, 2, 1), ibTotalsPeriod::Month));
	EXPECT_EQ(0, ibDateTime(2024, 1, 1).PeriodsUntil(ibDateTime(2024, 1, 31, 23, 59, 59), ibTotalsPeriod::Month));
	EXPECT_EQ(90, ibDateTime(2026, 1, 1).PeriodsUntil(ibDateTime(2026, 4, 1), ibTotalsPeriod::Day));   // the count the server gives, clock change or not
	EXPECT_EQ(-1, ibDateTime(2024, 1, 1).PeriodsUntil(ibDateTime(2023, 12, 31, 23), ibTotalsPeriod::Hour));
	// The week starts on Monday; a Sunday is day 7 and truncates six days back.
	EXPECT_EQ(7, ibDateTime(2024, 9, 1).GetPart(ibDatePart::WeekDay));                        // a Sunday
	EXPECT_EQ(ibDateTime(2024, 8, 26), ibDateTime(2024, 9, 1, 15).BeginOfPeriod(ibTotalsPeriod::Week));
	EXPECT_EQ(ibDateTime(2024, 9, 1, 23, 59, 59), ibDateTime(2024, 8, 27, 9).EndOfPeriod(ibTotalsPeriod::Week));
	// ISO weeks: the first week holds the first Thursday; a year can have 53.
	EXPECT_EQ(53, ibDateTime(2021, 1, 3).GetPart(ibDatePart::Week));    // a Sunday, still week 53 of 2020
	EXPECT_EQ(1,  ibDateTime(2024, 12, 30).GetPart(ibDatePart::Week));  // a Monday, already week 1 of 2025
	EXPECT_EQ(53, ibDateTime(2020, 12, 31).GetPart(ibDatePart::Week));
	EXPECT_EQ(52, ibDateTime(2023, 12, 31).GetPart(ibDatePart::Week));  // 2023 ends on a Sunday: week 52
	// Quarters and halves; an end is the last second of its period.
	EXPECT_EQ(4, ibDateTime(2024, 10, 1).GetPart(ibDatePart::Quarter));
	EXPECT_EQ(ibDateTime(2024, 7, 1), ibDateTime(2024, 9, 5, 4, 3, 2).BeginOfPeriod(ibTotalsPeriod::HalfYear));
	EXPECT_EQ(ibDateTime(2024, 12, 31, 23, 59, 59), ibDateTime(2024, 9, 5).EndOfPeriod(ibTotalsPeriod::Year));
	EXPECT_EQ(ibDateTime(2024, 9, 30, 23, 59, 59), ibDateTime(2024, 8, 5).EndOfPeriod(ibTotalsPeriod::Quarter));
	EXPECT_EQ(ibDateTime(2024, 2, 29, 23, 59, 59), ibDateTime(2024, 2, 5).EndOfPeriod(ibTotalsPeriod::Month));
	// Below the empty date the truncations still floor: the day a reading before it falls in.
	EXPECT_EQ(ibDateTime(-86400000ll), ibDateTime(-1ll).BeginOfPeriod(ibTotalsPeriod::Day));
	EXPECT_EQ(ibDateTime(-60000ll), ibDateTime(-1ll).BeginOfPeriod(ibTotalsPeriod::Minute));
	// An hour on the wall is 3 600 000 milliseconds, every day of the year.
	EXPECT_EQ(ibDateTime(2026, 3, 29, 3, 30), ibDateTime(2026, 3, 29, 2, 30).AddPeriods(ibTotalsPeriod::Hour, 1));
	EXPECT_EQ(3600000ll, ibDateTime(2026, 3, 29, 3, 30) - ibDateTime(2026, 3, 29, 2, 30));
}
