// =============================================================================
// The calendar without a clock — backend/fdate.h, and the six calendar twins
// over a wall-clock reading (databaseLayer.h).
//
// What is pinned: that the day count and the parts are exact inverses on the
// whole axis, that the empty date is the number the header says, and that the
// wall-clock twins answer what their wxDateTime forms answer wherever the two
// CAN agree — every date of a leap year at noon, through every unit and every
// part. The reference the twins are held to is thus the older implementation
// itself, over a grid where a clock change cannot reach it; the places where
// the two are DESIGNED to differ (an hour across a clock change) are outside
// the grid on purpose and are stated in databaseLayer.h.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/fdate.h"
#include "backend/databaseLayer/databaseLayer.h"   // the six twins, both forms

#include <wx/datetime.h>

#include <cstdio>    // snprintf - the text of a reading in a failure message
#include <string>
#include <vector>

// ---------------------------------------------------------------- the number

static_assert(ibDaysFromCivil(1970, 1, 1) == 0, "the epoch is day zero");
static_assert(ibDaysFromCivil(1970, 1, 2) == 1, "");
static_assert(ibDaysFromCivil(1969, 12, 31) == -1, "the day before the epoch is minus one");
static_assert(ibDaysFromCivil(2000, 3, 1) == 11017, "a leap day in between is counted");
static_assert(ibDaysFromCivil(1, 1, 1) == -719162, "the first day of year 1");
static_assert(ibWallFromParts(1, 1, 1) == -62135596800000ll, "the empty date, as the wall reads it");
static_assert(ibWallFromParts(1970, 1, 1, 0, 0, 1) == 1000, "");
static_assert(ibWallFromParts(9999, 12, 31, 23, 59, 59, 999) == 253402300799999ll, "the last reading of the axis");

namespace {

ibDateParts Parts(wxLongLong_t wall)
{
	ibDateParts parts;
	ibWallToParts(wall, parts);
	return parts;
}

wxLongLong_t Wall(const ibDateParts& p)
{
	return ibWallFromParts(p.m_year, p.m_month, p.m_day, p.m_hour, p.m_minute, p.m_second, p.m_millisecond);
}

bool SameParts(const ibDateParts& a, const ibDateParts& b)
{
	return a.m_year == b.m_year && a.m_month == b.m_month && a.m_day == b.m_day && a.m_hour == b.m_hour
		&& a.m_minute == b.m_minute && a.m_second == b.m_second && a.m_millisecond == b.m_millisecond;
}

std::string Text(const ibDateParts& p)
{
	char buf[40];
	snprintf(buf, sizeof buf, "%04d-%02u-%02u %02u:%02u:%02u.%03u", p.m_year, p.m_month, p.m_day, p.m_hour, p.m_minute, p.m_second, p.m_millisecond);
	return buf;
}

// A wxDateTime with these parts on the machine's own clock - what the wxDateTime forms read.
wxDateTime Wx(int year, unsigned month, unsigned day, unsigned hour = 0, unsigned minute = 0, unsigned second = 0)
{
	return wxDateTime(static_cast<wxDateTime::wxDateTime_t>(day), static_cast<wxDateTime::Month>(month - 1), year,
		static_cast<wxDateTime::wxDateTime_t>(hour), static_cast<wxDateTime::wxDateTime_t>(minute),
		static_cast<wxDateTime::wxDateTime_t>(second));
}

// The wall-clock reading a wxDateTime shows - its parts, not its instant.
wxLongLong_t WallOf(const wxDateTime& dt)
{
	const wxDateTime::Tm tm = dt.GetTm();
	return ibWallFromParts(tm.year, static_cast<unsigned>(tm.mon) + 1, tm.mday, tm.hour, tm.min, tm.sec, tm.msec);
}

bool IsLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

unsigned DaysIn(int y, unsigned m)
{
	static const unsigned days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	return m == 2 && IsLeap(y) ? 29u : days[m - 1];
}

} // namespace

// -------------------------------------------------------------- the parts

TEST(FDate, EveryDayOfFourYearsRoundTripsAndCountsUp)
{
	const int years[] = { 1, 1899, 1969, 1970, 2000, 2024, 9999 };
	for (const int year : years) {
		wxLongLong_t previous = ibWallFromParts(year, 1, 1) - 1;
		for (unsigned month = 1; month <= 12; ++month) {
			for (unsigned day = 1; day <= DaysIn(year, month); ++day) {
				const ibDateParts written = { year, month, day, 12, 34, 56, 789 };
				const wxLongLong_t wall = Wall(written);
				EXPECT_GT(wall, previous) << Text(written);
				previous = wall;
				const ibDateParts read = Parts(wall);
				EXPECT_TRUE(SameParts(written, read)) << Text(written) << " read back as " << Text(read);
			}
		}
	}
}

TEST(FDate, MidnightAndTheLastMillisecondOfADay)
{
	const ibDateParts first = { 1969, 12, 31, 0, 0, 0, 0 };
	const ibDateParts last  = { 1969, 12, 31, 23, 59, 59, 999 };
	EXPECT_EQ(-ibWallMsPerDay, Wall(first));
	EXPECT_EQ(-1, Wall(last));
	EXPECT_TRUE(SameParts(first, Parts(-ibWallMsPerDay)));
	EXPECT_TRUE(SameParts(last, Parts(-1)));
	EXPECT_EQ(1970, Parts(0).m_year);
	EXPECT_EQ(0u, Parts(0).m_hour);
}

TEST(FDate, TheDayCountAgreesWithWxOnAGridOfDates)
{
	// wx counts the same days when asked for the day of the year and the weekday; those are the
	// two readings a calendar mistake would show up in first.
	const int years[] = { 1900, 1999, 2000, 2024, 2100 };
	for (const int year : years) {
		for (unsigned month = 1; month <= 12; ++month) {
			for (unsigned day = 1; day <= DaysIn(year, month); day += 5) {
				const wxDateTime dt = Wx(year, month, day, 12);
				const wxLongLong_t wall = ibWallFromParts(year, month, day, 12);
				EXPECT_EQ(static_cast<long>(dt.GetDayOfYear()), ibReadDatePart(wall, ibDatePart::DayOfYear)) << year << '-' << month << '-' << day;
				const int wd = static_cast<int>(dt.GetWeekDay());
				EXPECT_EQ(wd == wxDateTime::Sun ? 7L : static_cast<long>(wd), ibReadDatePart(wall, ibDatePart::WeekDay)) << year << '-' << month << '-' << day;
			}
		}
	}
}

// ------------------------------------------------------------- the twins

// Every date of a leap year at noon, and the turns of two years: the calendar twins answer what wx's
// own calendar answers where wx has a word - the month and year steps (wxDateSpan, which clamps the
// day as DATEADD does), the week's Monday, the ISO week, the weekday, the day of the year. Noon, so
// that nothing here can cross the hour a clock changes on the machine running the test: the reading
// is asked through wx's LOCAL parts, and a skipped hour is the one place those are not a mirror.
TEST(FDate, TheCalendarAgreesWithWxWhereWxIsAsked)
{
	std::vector<ibDateParts> grid;
	for (unsigned month = 1; month <= 12; ++month)
		for (unsigned day = 1; day <= DaysIn(2024, month); ++day)
			grid.push_back({ 2024, month, day, 12, 0, 0, 0 });
	// The turns of two years, where the ISO week counts from the year before or into the next.
	for (unsigned day = 25; day <= 31; ++day) grid.push_back({ 2020, 12, day, 12, 0, 0, 0 });
	for (unsigned day = 1;  day <= 10; ++day) grid.push_back({ 2021, 1,  day, 12, 0, 0, 0 });
	for (unsigned day = 25; day <= 31; ++day) grid.push_back({ 2024, 12, day, 12, 0, 0, 0 });
	for (unsigned day = 1;  day <= 10; ++day) grid.push_back({ 2025, 1,  day, 12, 0, 0, 0 });

	for (const ibDateParts& p : grid) {
		const wxDateTime dt = Wx(p.m_year, p.m_month, p.m_day, p.m_hour);
		const wxLongLong_t wall = Wall(p);
		ASSERT_EQ(wall, WallOf(dt)) << Text(p);
		for (const long count : { 1L, -1L, 3L, -3L, 11L, -13L }) {
			EXPECT_EQ(WallOf(dt + wxDateSpan::Months(static_cast<int>(count))), ibDateAddUnits(wall, ibTotalsPeriod::Month, count)) << Text(p) << " months " << count;
			EXPECT_EQ(WallOf(dt + wxDateSpan::Years(static_cast<int>(count))),  ibDateAddUnits(wall, ibTotalsPeriod::Year,  count)) << Text(p) << " years " << count;
			EXPECT_EQ(WallOf(dt + wxDateSpan::Days(static_cast<int>(count))),   ibDateAddUnits(wall, ibTotalsPeriod::Day,   count)) << Text(p) << " days " << count;
			EXPECT_EQ(WallOf(dt + wxDateSpan::Weeks(static_cast<int>(count))),  ibDateAddUnits(wall, ibTotalsPeriod::Week,  count)) << Text(p) << " weeks " << count;
		}
		const int wd = static_cast<int>(dt.GetWeekDay());
		const long isoWeekDay = wd == wxDateTime::Sun ? 7L : static_cast<long>(wd);
		EXPECT_EQ(isoWeekDay, ibReadDatePart(wall, ibDatePart::WeekDay)) << Text(p);
		EXPECT_EQ(static_cast<long>(dt.GetDayOfYear()), ibReadDatePart(wall, ibDatePart::DayOfYear)) << Text(p);
		EXPECT_EQ(static_cast<long>(dt.GetWeekOfYear(wxDateTime::Monday_First)), ibReadDatePart(wall, ibDatePart::Week)) << Text(p);
		EXPECT_EQ(WallOf((dt - wxDateSpan::Days(static_cast<int>(isoWeekDay) - 1)).GetDateOnly()), ibTruncateToPeriod(wall, ibTotalsPeriod::Week)) << Text(p);
		EXPECT_EQ(WallOf(wxDateTime(1, dt.GetMonth(), dt.GetYear())), ibTruncateToPeriod(wall, ibTotalsPeriod::Month)) << Text(p);
		EXPECT_EQ(WallOf(wxDateTime(1, wxDateTime::Jan, dt.GetYear())), ibTruncateToPeriod(wall, ibTotalsPeriod::Year)) << Text(p);
		EXPECT_EQ(WallOf(dt.GetDateOnly()), ibTruncateToPeriod(wall, ibTotalsPeriod::Day)) << Text(p);
	}
}

// ------------------------------------------------------------- the bridge

// A reading crosses to wxDateTime by its local parts and comes back the same reading; an invalid
// wxDateTime comes over as the empty date. Asked at noon and at midnight, on days of every season,
// so the machine's own clock change is not on the grid (it is the one thing the bridge cannot mirror).
TEST(FDate, TheBridgeCarriesAReadingByItsParts)
{
	const ibDateParts readings[] = {
		{ 1, 1, 1, 0, 0, 0, 0 }, { 1899, 12, 31, 23, 59, 59, 999 }, { 1969, 12, 31, 12, 0, 0, 0 }, { 1970, 1, 1, 0, 0, 0, 0 },
		{ 2024, 2, 29, 12, 0, 0, 0 }, { 2026, 1, 15, 0, 0, 0, 0 }, { 2026, 7, 15, 12, 30, 45, 250 }, { 2026, 10, 25, 12, 0, 0, 0 },
		{ 9999, 12, 31, 23, 59, 59, 999 },
	};
	for (const ibDateParts& p : readings) {
		const wxLongLong_t wall = Wall(p);
		const wxDateTime dt = ibDateTimeOfWall(wall);
		ASSERT_TRUE(dt.IsValid()) << Text(p);
		const wxDateTime::Tm tm = dt.GetTm();
		EXPECT_EQ(p.m_year, tm.year) << Text(p);
		EXPECT_EQ(p.m_month, static_cast<unsigned>(tm.mon) + 1) << Text(p);
		EXPECT_EQ(p.m_day, static_cast<unsigned>(tm.mday)) << Text(p);
		EXPECT_EQ(p.m_hour, static_cast<unsigned>(tm.hour)) << Text(p);
		EXPECT_EQ(p.m_minute, static_cast<unsigned>(tm.min)) << Text(p);
		EXPECT_EQ(p.m_second, static_cast<unsigned>(tm.sec)) << Text(p);
		EXPECT_EQ(p.m_millisecond, static_cast<unsigned>(tm.msec)) << Text(p);
		EXPECT_EQ(wall, ibWallOfDateTime(dt)) << Text(p);
	}
	EXPECT_EQ(ibWallFromParts(1, 1, 1), ibWallOfDateTime(wxDateTime()));
	EXPECT_EQ(ibWallFromParts(1, 1, 1), ibWallOfDateTime(wxInvalidDateTime));
}

// The parts know the day's place: weekday (Monday 1), day of the year, ISO week - and the days a
// month has.
TEST(FDate, ThePartsPlaceTheDayInItsWeekAndYear)
{
	EXPECT_EQ(4u, Parts(0).m_weekDay);                                            // 1970-01-01, a Thursday
	EXPECT_EQ(1u, Parts(ibWallFromParts(2024, 1, 1)).m_weekDay);                  // a Monday
	EXPECT_EQ(7u, Parts(ibWallFromParts(2024, 9, 1)).m_weekDay);                  // a Sunday
	EXPECT_EQ(1u, Parts(ibWallFromParts(1, 1, 1)).m_weekDay);                     // year 1 opened on a Monday
	EXPECT_EQ(366u, Parts(ibWallFromParts(2024, 12, 31, 23)).m_yearDay);
	EXPECT_EQ(60u, Parts(ibWallFromParts(2024, 2, 29)).m_yearDay);
	EXPECT_EQ(53u, Parts(ibWallFromParts(2021, 1, 3)).m_isoWeek);                 // still week 53 of 2020
	EXPECT_EQ(1u, Parts(ibWallFromParts(2024, 12, 30)).m_isoWeek);                // already week 1 of 2025
	EXPECT_EQ(52u, Parts(ibWallFromParts(2023, 12, 31)).m_isoWeek);
	EXPECT_EQ(53u, Parts(ibWallFromParts(2020, 12, 31)).m_isoWeek);
	EXPECT_EQ(1u, Parts(ibWallFromParts(2026, 1, 1)).m_isoWeek);                  // a Thursday
	EXPECT_EQ(53u, Parts(ibWallFromParts(2027, 1, 1)).m_isoWeek);                 // a Friday: week 53 of 2026
	static_assert(ibDaysInMonth(2024, 2) == 29, "a leap year");
	static_assert(ibDaysInMonth(1900, 2) == 28, "a century that is not");
	static_assert(ibDaysInMonth(2000, 2) == 29, "a century that is");
	static_assert(ibDaysInMonth(2026, 4) == 30 && ibDaysInMonth(2026, 12) == 31, "");
	static_assert(ibDaysInMonth(2026, 13) == 0 && ibDaysInMonth(2026, 0) == 0, "no such month");
}

// The rules, stated on their own so a change to BOTH twins at once could not slip through the
// cross-check above.
TEST(FDate, TheCalendarRulesStatedOutright)
{
	// A month added lands on the last day the month has.
	EXPECT_EQ(ibWallFromParts(2024, 2, 29), ibDateAddUnits(ibWallFromParts(2024, 1, 31), ibTotalsPeriod::Month, 1));
	EXPECT_EQ(ibWallFromParts(2023, 2, 28), ibDateAddUnits(ibWallFromParts(2023, 1, 31), ibTotalsPeriod::Month, 1));
	EXPECT_EQ(ibWallFromParts(2023, 11, 30), ibDateAddUnits(ibWallFromParts(2024, 1, 31), ibTotalsPeriod::Month, -2));
	EXPECT_EQ(ibWallFromParts(2025, 1, 31), ibDateAddUnits(ibWallFromParts(2024, 1, 31), ibTotalsPeriod::Year, 1));
	// Ten-day buckets: the 1st, the 11th, the 21st - and the third runs to the end of the month.
	EXPECT_EQ(ibWallFromParts(2024, 1, 21), ibTruncateToPeriod(ibWallFromParts(2024, 1, 31, 8), ibTotalsPeriod::TenDays));
	EXPECT_EQ(ibWallFromParts(2024, 2, 1),  ibNextPeriodStart(ibWallFromParts(2024, 1, 31, 8), ibTotalsPeriod::TenDays));
	EXPECT_EQ(ibWallFromParts(2024, 1, 11), ibNextPeriodStart(ibWallFromParts(2024, 1, 5), ibTotalsPeriod::TenDays));
	EXPECT_EQ(3, ibDateDiffUnits(ibWallFromParts(2024, 1, 5), ibWallFromParts(2024, 2, 5), ibTotalsPeriod::TenDays));
	// Whole units between the units two readings fall in.
	EXPECT_EQ(1, ibDateDiffUnits(ibWallFromParts(2024, 1, 31), ibWallFromParts(2024, 2, 1), ibTotalsPeriod::Month));
	EXPECT_EQ(0, ibDateDiffUnits(ibWallFromParts(2024, 1, 1), ibWallFromParts(2024, 1, 31, 23, 59, 59), ibTotalsPeriod::Month));
	EXPECT_EQ(90, ibDateDiffUnits(ibWallFromParts(2026, 1, 1), ibWallFromParts(2026, 4, 1), ibTotalsPeriod::Day));   // the count the server gives, clock change or not
	EXPECT_EQ(-1, ibDateDiffUnits(ibWallFromParts(2024, 1, 1), ibWallFromParts(2023, 12, 31, 23), ibTotalsPeriod::Hour));
	// The week starts on Monday; a Sunday is day 7 and truncates six days back.
	EXPECT_EQ(7, ibReadDatePart(ibWallFromParts(2024, 9, 1), ibDatePart::WeekDay));                        // a Sunday
	EXPECT_EQ(ibWallFromParts(2024, 8, 26), ibTruncateToPeriod(ibWallFromParts(2024, 9, 1, 15), ibTotalsPeriod::Week));
	// ISO weeks: the first week holds the first Thursday; a year can have 53.
	EXPECT_EQ(53, ibReadDatePart(ibWallFromParts(2021, 1, 3), ibDatePart::Week));    // a Sunday, still week 53 of 2020
	EXPECT_EQ(1,  ibReadDatePart(ibWallFromParts(2024, 12, 30), ibDatePart::Week));  // a Monday, already week 1 of 2025
	EXPECT_EQ(53, ibReadDatePart(ibWallFromParts(2020, 12, 31), ibDatePart::Week));
	EXPECT_EQ(52, ibReadDatePart(ibWallFromParts(2023, 12, 31), ibDatePart::Week));  // 2023 ends on a Sunday: week 52
	// Quarters and halves.
	EXPECT_EQ(4, ibReadDatePart(ibWallFromParts(2024, 10, 1), ibDatePart::Quarter));
	EXPECT_EQ(ibWallFromParts(2024, 7, 1), ibTruncateToPeriod(ibWallFromParts(2024, 9, 5, 4, 3, 2), ibTotalsPeriod::HalfYear));
	EXPECT_EQ(ibWallFromParts(2024, 12, 31, 23, 59, 59), ibEndOfPeriod(ibWallFromParts(2024, 9, 5), ibTotalsPeriod::Year));
	// Below the epoch the truncations still floor: the day a reading before 1970 falls in.
	EXPECT_EQ(-ibWallMsPerDay, ibTruncateToPeriod(-1, ibTotalsPeriod::Day));
	EXPECT_EQ(-60000, ibTruncateToPeriod(-1, ibTotalsPeriod::Minute));
	// An hour on the wall is 3 600 000 milliseconds, every day of the year.
	EXPECT_EQ(ibWallFromParts(2026, 3, 29, 3, 30), ibDateAddUnits(ibWallFromParts(2026, 3, 29, 2, 30), ibTotalsPeriod::Hour, 1));
}
