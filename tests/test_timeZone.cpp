// A ZONE AND THE ONE DOOR A READING CROSSES ZONES THROUGH (core/timeZone.h): a reading in universal time told on a
// zone's clock and back, by the IANA rules - summer time, the hour the clocks skip and the hour they repeat - and what
// is refused.

#include <gtest/gtest.h>

#include "core/fdatetime.h"
#include "core/fstring.h"
#include "core/timeZone.h"

namespace {

ibTimeZone Zone(const ibChar* name)
{
	ibTimeZone zone;
	EXPECT_TRUE(zone.FromName(name)) << ibString(name).ToUtf8();
	return zone;
}

} // namespace

// Summer and winter: Kyiv is three hours ahead in July, two in January.
TEST(TimeZone, AReadingIsToldOnTheZonesClockSummerAndWinter) {
	const ibTimeZone kyiv = Zone(ibT("Europe/Kyiv"));
	EXPECT_EQ(kyiv.ToLocal(ibDateTime(2026, 7, 1, 12, 0)), ibDateTime(2026, 7, 1, 15, 0));
	EXPECT_EQ(kyiv.ToLocal(ibDateTime(2026, 1, 15, 22, 30)), ibDateTime(2026, 1, 16, 0, 30));
	EXPECT_EQ(kyiv.ToUniversal(ibDateTime(2026, 7, 1, 15, 0)), ibDateTime(2026, 7, 1, 12, 0));
	EXPECT_EQ(kyiv.ToUniversal(ibDateTime(2026, 1, 16, 0, 30, 0, 250)), ibDateTime(2026, 1, 15, 22, 30, 0, 250));
	// West of Greenwich too.
	const ibTimeZone newYork = Zone(ibT("America/New_York"));
	EXPECT_EQ(newYork.ToLocal(ibDateTime(2026, 7, 1, 12, 0)), ibDateTime(2026, 7, 1, 8, 0));
}

// 03:30 on the morning Kyiv's clocks go from 03:00 to 04:00 is no reading at all: it is the moment they skip at.
// 03:30 on the morning they go back from 04:00 to 03:00 is shown twice: the first of the two, still summer time.
TEST(TimeZone, TheSkippedHourIsTheMomentOfTheSkipAndTheRepeatedOneTheFirst) {
	const ibTimeZone kyiv = Zone(ibT("Europe/Kyiv"));
	EXPECT_EQ(kyiv.ToUniversal(ibDateTime(2026, 3, 29, 3, 30)), ibDateTime(2026, 3, 29, 1, 0));
	EXPECT_EQ(kyiv.ToUniversal(ibDateTime(2026, 10, 25, 3, 30)), ibDateTime(2026, 10, 25, 0, 30));
	EXPECT_EQ(kyiv.ToLocal(ibDateTime(2026, 10, 25, 1, 30)), ibDateTime(2026, 10, 25, 3, 30)) << "the second 03:30";
}

// Past the last transition a system's zone file lists (they are written to 2037) the zone keeps its summer time:
// the file's own rule answers there, as the database's does on Windows.
TEST(TimeZone, SummerTimeHoldsPastTheLastListedTransition) {
	const ibTimeZone kyiv = Zone(ibT("Europe/Kyiv"));
	EXPECT_EQ(kyiv.ToLocal(ibDateTime(2040, 7, 1, 12, 0)), ibDateTime(2040, 7, 1, 15, 0));
	EXPECT_EQ(kyiv.ToLocal(ibDateTime(2040, 1, 1, 12, 0)), ibDateTime(2040, 1, 1, 14, 0));
	EXPECT_EQ(kyiv.ToUniversal(ibDateTime(9999, 7, 1, 15, 0)), ibDateTime(9999, 7, 1, 12, 0));
	const ibTimeZone newYork = Zone(ibT("America/New_York"));
	EXPECT_EQ(newYork.ToLocal(ibDateTime(2100, 7, 1, 12, 0)), ibDateTime(2100, 7, 1, 8, 0));
}

// A zone the database renamed answers to its old name and its new alike: Windows names Kyiv by the old one, a
// stock Linux knows the new alone.
TEST(TimeZone, ARenamedZoneIsFoundByEitherName) {
	const ibTimeZone kiev = Zone(ibT("Europe/Kiev"));
	const ibTimeZone kyiv = Zone(ibT("Europe/Kyiv"));
	EXPECT_EQ(kiev.ToLocal(ibDateTime(2026, 7, 1, 12, 0)), kyiv.ToLocal(ibDateTime(2026, 7, 1, 12, 0)));
	EXPECT_EQ(Zone(ibT("Asia/Calcutta")).ToLocal(ibDateTime(2026, 1, 1, 0, 0)), ibDateTime(2026, 1, 1, 5, 30));
	EXPECT_EQ(Zone(ibT("Asia/Kolkata")).ToLocal(ibDateTime(2026, 1, 1, 0, 0)), ibDateTime(2026, 1, 1, 5, 30));
}

// A reading the zone would put before year 1 is its first millisecond, not a date before the calendar or the empty
// one: Kyiv's local mean time was 2:02:04 ahead.
TEST(TimeZone, AReadingIsNotPutBeforeYearOne) {
	const ibTimeZone kyiv = Zone(ibT("Europe/Kyiv"));
	EXPECT_EQ(kyiv.ToUniversal(ibDateTime(1, 1, 1, 1, 0)).GetValue(), 1);
	EXPECT_EQ(kyiv.ToUniversal(ibDateTime(1, 1, 1, 2, 2, 4)).GetValue(), 1);
	EXPECT_FALSE(kyiv.ToUniversal(ibDateTime(1, 1, 1, 2, 2, 4)).IsEmpty());
}

// The empty date is no time at all, in any zone.
TEST(TimeZone, TheEmptyDateStaysEmpty) {
	const ibTimeZone kyiv = Zone(ibT("Europe/Kyiv"));
	EXPECT_TRUE(kyiv.ToLocal(ibDateTime()).IsEmpty());
	EXPECT_TRUE(kyiv.ToUniversal(ibDateTime()).IsEmpty());
}

// A zone made with no name is universal time; a name the rules have not is refused and the zone left as it was.
TEST(TimeZone, AnUnknownNameIsRefused) {
	ibTimeZone zone;
	EXPECT_EQ(zone.GetName(), ibString(ibT("UTC")));
	EXPECT_EQ(zone.ToLocal(ibDateTime(2026, 7, 1, 12, 0)), ibDateTime(2026, 7, 1, 12, 0));
	ASSERT_TRUE(zone.FromName(ibT("Europe/Kyiv")));
	EXPECT_FALSE(zone.FromName(ibT("Europe/Atlantis")));
	EXPECT_FALSE(zone.FromName(ibT("")));
	EXPECT_EQ(zone.GetName(), ibString(ibT("Europe/Kyiv")));
}

// The machine's zone has a name the rules have, and a reading crosses it and back unchanged away from a change of the
// clocks.
TEST(TimeZone, TheMachinesZoneIsOneTheRulesHave) {
	const ibTimeZone machine = ibTimeZone::Machine();
	ibTimeZone again;
	EXPECT_TRUE(again.FromName(machine.GetName())) << machine.GetName().ToUtf8();
	const ibDateTime noon(2026, 6, 15, 12, 0);
	EXPECT_EQ(machine.ToUniversal(machine.ToLocal(noon)), noon);
}
