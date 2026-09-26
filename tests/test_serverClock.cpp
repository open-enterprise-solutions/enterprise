////////////////////////////////////////////////////////////////////////////
//	Description : "now" is the server's clock - ibServerClock measured through the dialect
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <wx/init.h>

#include <cstdlib>   // std::llabs

#include "backend/session/serverClock.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/system/systemManager.h"   // ibValueSystemFunction::CurrentDate - the script's door to "now"

namespace {

wxLongLong_t MachineNow() { return ibWallOfDateTime(wxDateTime::Now()); }
long long Apart(wxLongLong_t a, wxLongLong_t b) { return std::llabs(static_cast<long long>(a - b)); }

// The clock is process-wide state; every test here leaves it as it found it.
struct ClockScope {
	wxInitializer wx;
	~ClockScope() { ibServerClock::Reset(); }
};

// A SQLite base whose dialect names a FIXED moment as the server's clock - what a base an hour or a
// year away from this machine looks like to the measurement.
class FrozenClockBase : public ibDatabaseLayerSQLite {
public:
	static ibDialectDictionary s_dialect;
	const ibDialectDictionary& GetDialect() const override { return s_dialect; }
};
ibDialectDictionary FrozenClockBase::s_dialect = [] {
	ibDialectDictionary d = ibDatabaseLayerSQLite::Dialect();
	d.m_localTimestamp = wxT("'2030-01-01 12:00:00'");
	return d;
}();

// ...and one whose dialect has no word for its clock at all (the ODBC baseline's case).
class WordlessBase : public ibDatabaseLayerSQLite {
public:
	static ibDialectDictionary s_dialect;
	const ibDialectDictionary& GetDialect() const override { return s_dialect; }
};
ibDialectDictionary WordlessBase::s_dialect = [] {
	ibDialectDictionary d = ibDatabaseLayerSQLite::Dialect();
	d.m_localTimestamp = wxEmptyString;
	return d;
}();

} // namespace

// SQLite has no server, so its clock is the process's own: measured through the dialect's word, the
// difference is within seconds of nothing, and Now() is the machine's clock.
TEST(ServerClock, TheBaseClockIsReadThroughTheDialect)
{
	ClockScope scope;
	ibDatabaseLayerSQLite base;
	ASSERT_TRUE(base.Open(wxT(":memory:")));
	ASSERT_TRUE(ibServerClock::Refresh(base));
	EXPECT_LT(Apart(ibServerClock::Offset(), 0), 5000);
	EXPECT_LT(Apart(ibServerClock::Now(), MachineNow()), 5000);
	ibServerClock::Reset();
	EXPECT_EQ(0, ibServerClock::Offset());
}

// A base whose clock stands elsewhere moves Now() there - and CurrentDate() with it, which is the
// whole point: a script on this machine reads the base's "now", not this machine's.
TEST(ServerClock, NowFollowsTheBaseClockAndSoDoesCurrentDate)
{
	ClockScope scope;
	FrozenClockBase base;
	ASSERT_TRUE(base.Open(wxT(":memory:")));
	ASSERT_TRUE(ibServerClock::Refresh(base));
	const wxLongLong_t frozen = ibWallFromParts(2030, 1, 1, 12);
	EXPECT_LT(Apart(ibServerClock::Now(), frozen), 5000);
	EXPECT_LT(Apart(ibValueSystemFunction::CurrentDate().GetDate(), frozen), 5000);
	EXPECT_EQ(ibValueTypes::TYPE_DATE, ibValueSystemFunction::CurrentDate().GetType());
	// The difference is what was measured: the frozen moment less this machine's clock.
	EXPECT_LT(Apart(ibServerClock::Offset(), frozen - MachineNow()), 5000);
}

// A dialect with no word for its clock leaves the difference as it was - the machine's clock stands,
// or the last measurement does - and says so.
TEST(ServerClock, ADialectWithoutAWordForItsClockLeavesTheDifferenceAlone)
{
	ClockScope scope;
	FrozenClockBase frozen;
	ASSERT_TRUE(frozen.Open(wxT(":memory:")));
	ASSERT_TRUE(ibServerClock::Refresh(frozen));
	const wxLongLong_t measured = ibServerClock::Offset();
	ASSERT_NE(0, measured);

	WordlessBase wordless;
	ASSERT_TRUE(wordless.Open(wxT(":memory:")));
	EXPECT_FALSE(ibServerClock::Refresh(wordless));
	EXPECT_EQ(measured, ibServerClock::Offset());
	ibServerClock::Reset();
	EXPECT_EQ(0, ibServerClock::Offset());
	EXPECT_LT(Apart(ibValueSystemFunction::CurrentDate().GetDate(), MachineNow()), 5000);
}
