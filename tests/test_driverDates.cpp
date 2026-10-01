////////////////////////////////////////////////////////////////////////////
//	Description : A date through a driver - the reading goes in as its parts and comes back the same
//	reading, whatever the machine's clock does. Asked of SQLite, the one driver the suite executes.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/backend_core.h"                              // ibDateTime
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"

#include <wx/filefn.h>   // wxFileExists / wxRemoveFile - the cross-zone base

#include "backend/compiler/value.h"   // ibValue - the reading as the script sees it

#include <cstdlib>   // getenv, and the TZ setters below
#include <ctime>     // tzset / _tzset
#include <memory>    // std::shared_ptr, make_shared
#include <utility>   // std::pair

namespace {

// The machine's clock set to another zone for the length of a test, and put back after. The C
// runtime reads TZ (in its POSIX spelling, on Windows too) when told to, and wxDateTime's local
// parts and mktime follow it - which is exactly the road the bridge takes and the value must not.
struct ZoneScope {
	wxString m_before;
	bool     m_had = false;
	explicit ZoneScope(const char* tz) {
		if (const char* had = std::getenv("TZ")) { m_had = true; m_before = wxString::FromAscii(had); }
		Set(tz);
	}
	~ZoneScope() { Set(m_had ? static_cast<const char*>(m_before.ToAscii()) : nullptr); }
	static void Set(const char* tz) {
#ifdef _WIN32
		_putenv_s("TZ", tz != nullptr ? tz : "");
		_tzset();
#else
		if (tz != nullptr) setenv("TZ", tz, 1); else unsetenv("TZ");
		tzset();
#endif
	}
};

struct Base {
	wxInitializer wx;
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	Base() : db(std::make_shared<ibDatabaseLayerSQLite>()) {
		EXPECT_TRUE(db->Open(wxT(":memory:")));
		db->RunQuery(wxT("CREATE TABLE stamps (id INTEGER, at TIMESTAMP)"));
	}
	void Put(int id, const ibDateTime& reading) {
		ibPreparedStatement* statement = db->PrepareStatement(wxT("INSERT INTO stamps (id, at) VALUES (?, ?)"));
		ASSERT_NE(statement, nullptr);
		statement->SetParamInt(1, id);
		statement->SetParamDate(2, reading);
		statement->RunQuery();
		db->CloseStatement(statement);
	}
	// The reading and the text the row holds, by id.
	std::pair<ibDateTime, wxString> Get(int id, bool* isNull = nullptr) {
		std::pair<ibDateTime, wxString> out{ ibDateTime(), wxString() };
		ibPreparedStatement* statement = db->PrepareStatement(wxT("SELECT at FROM stamps WHERE id = ?"));
		EXPECT_NE(statement, nullptr);
		statement->SetParamInt(1, id);
		ibDatabaseResultSet* rs = statement->RunQueryWithResults();
		EXPECT_NE(rs, nullptr);
		EXPECT_TRUE(rs->Next());
		out.first = rs->GetResultDate(1);
		out.second = rs->GetResultString(1);
		if (isNull != nullptr)
			*isNull = rs->IsFieldNull(1);
		statement->CloseResultSet(rs);
		db->CloseStatement(statement);
		return out;
	}
};

} // namespace

// 02:30 on the morning most of Europe's clocks go forward: a reading no instant on such a machine
// shows, so a driver that built one out of the parts read it back an hour later. The parts go in,
// the parts come out, and the text in the row is the parts spelled the ISO way.
TEST(DriverDates, TheReadingComesBackAsItWentInWhateverTheClockDoes)
{
	Base base;
	base.Put(1, ibDateTime(2026, 3, 29, 2, 30, 0));
	base.Put(2, ibDateTime());
	base.Put(3, ibDateTime(9999, 12, 31, 23, 59, 59));
	base.Put(4, ibDateTime(1970, 1, 1));
	base.Put(5, ibDateTime(2026, 10, 25, 3, 30, 0));   // the hour that happens twice, the morning the clocks go back

	EXPECT_EQ(ibDateTime(2026, 3, 29, 2, 30, 0), base.Get(1).first);
	EXPECT_EQ(wxT("2026-03-29 02:30:00"), base.Get(1).second);
	EXPECT_TRUE(base.Get(2).first.IsEmpty());
	EXPECT_EQ(wxT("0001-01-01 00:00:00"), base.Get(2).second);
	EXPECT_EQ(ibDateTime(9999, 12, 31, 23, 59, 59), base.Get(3).first);
	EXPECT_EQ(ibDateTime(1970, 1, 1), base.Get(4).first);
	EXPECT_EQ(ibDateTime(2026, 10, 25, 3, 30, 0), base.Get(5).first);
}

// The same reading on a machine whose clock SKIPS that very hour. Kyiv's clocks jump at 03:00, so the
// test above cannot tell a driver that goes by the parts from one that builds an instant on this
// machine. So the zone is set here for the length of the test - to the American Eastern one, whose
// clocks skip 02:00-03:00 on the second Sunday of March: 2026-03-08 02:30 does not exist under it,
// wx moves the instant on to 03:30, and a driver on that road reads 03:30 out of a row that holds
// 02:30. (Eastern rather than a European rule because every C runtime agrees on it: Windows' reads a
// rule of its own into any TZ with a summer name, and that rule is the American one.)
TEST(DriverDates, TheSkippedHourIsHeldEvenWhereTheMachineSkipsIt)
{
	ZoneScope eastern("EST5EDT");
	const wxDateTime instant(8, wxDateTime::Mar, 2026, 2, 30, 0);
	if (!instant.IsValid() || instant.GetHour() == 2)
		GTEST_SKIP() << "this C runtime does not follow TZ, so the clock does not skip the hour here";

	Base base;
	base.Put(1, ibDateTime(2026, 3, 8, 2, 30, 0));
	EXPECT_EQ(ibDateTime(2026, 3, 8, 2, 30, 0), base.Get(1).first);
	EXPECT_EQ(wxT("2026-03-08 02:30:00"), base.Get(1).second);
	EXPECT_EQ(ibDateTime(2026, 3, 8, 2, 30, 0), ibValue(2026, 3, 8, 2, 30, 0).GetDate());
	EXPECT_EQ(wxT("08.03.2026 02:30:00"), ibValue(2026, 3, 8, 2, 30, 0).GetString());
	// ...and the bridge is the one road that cannot hold it, which is why nothing of the value's goes over it.
	EXPECT_EQ(3u, static_cast<unsigned>(ibDateTime(2026, 3, 8, 2, 30, 0).ToWxDateTime().GetHour()));
}

// ⭐ A BASE WRITTEN UNDER ONE CLOCK AND READ UNDER ANOTHER - the measurement of 2026-09-26 as a test.
// CTest runs the two halves as two processes (tests/CMakeLists.txt): the writer under UTC, the reader
// two hours east, with the file's path and the step in the environment. Without those this is not the
// place, and the case says so rather than pretending.
namespace {

const ibDateTime kCrossZoneRows[] = {
	ibDateTime(),
	ibDateTime(2026, 9, 5),
	ibDateTime(2026, 3, 29, 2, 30, 0),
	ibDateTime(1950, 5, 1, 12, 0, 0),
	ibDateTime(9999, 12, 31, 23, 59, 59),
};

wxString CrossZonePath() { const char* p = std::getenv("OES_CROSS_ZONE_BASE"); return p != nullptr ? wxString::FromUTF8(p) : wxString(); }
wxString CrossZoneStep() { const char* s = std::getenv("OES_CROSS_ZONE_STEP"); return s != nullptr ? wxString::FromAscii(s) : wxString(); }

} // namespace

TEST(CrossZoneBase, WrittenUnderOneClock)
{
	if (CrossZoneStep() != wxT("write") || CrossZonePath().IsEmpty())
		GTEST_SKIP() << "the writing half runs from CTest with OES_CROSS_ZONE_BASE and OES_CROSS_ZONE_STEP=write";
	wxInitializer wx;
	if (wxFileExists(CrossZonePath()))
		wxRemoveFile(CrossZonePath());
	auto db = std::make_shared<ibDatabaseLayerSQLite>();
	ASSERT_TRUE(db->Open(CrossZonePath()));
	db->RunQuery(wxT("CREATE TABLE stamps (id INTEGER, at TIMESTAMP)"));
	int id = 0;
	for (const ibDateTime& reading : kCrossZoneRows) {
		ibPreparedStatement* statement = db->PrepareStatement(wxT("INSERT INTO stamps (id, at) VALUES (?, ?)"));
		ASSERT_NE(statement, nullptr);
		statement->SetParamInt(1, ++id);
		statement->SetParamDate(2, reading);
		statement->RunQuery();
		db->CloseStatement(statement);
	}
	db->Close();
}

TEST(CrossZoneBase, ReadUnderAnotherAsWritten)
{
	if (CrossZoneStep() != wxT("read") || CrossZonePath().IsEmpty())
		GTEST_SKIP() << "the reading half runs from CTest after the writer, with OES_CROSS_ZONE_STEP=read";
	wxInitializer wx;
	ASSERT_TRUE(wxFileExists(CrossZonePath())) << "the writing half did not run";
	auto db = std::make_shared<ibDatabaseLayerSQLite>();
	ASSERT_TRUE(db->Open(CrossZonePath()));
	int id = 0;
	for (const ibDateTime& reading : kCrossZoneRows) {
		ibPreparedStatement* statement = db->PrepareStatement(wxT("SELECT at FROM stamps WHERE id = ?"));
		ASSERT_NE(statement, nullptr);
		statement->SetParamInt(1, ++id);
		ibDatabaseResultSet* rs = statement->RunQueryWithResults();
		ASSERT_NE(rs, nullptr);
		ASSERT_TRUE(rs->Next());
		EXPECT_EQ(reading, rs->GetResultDate(1)) << "row " << id;
		EXPECT_EQ(ibValue(reading).GetString(), ibValue(rs->GetResultDate(1)).GetString()) << "row " << id;
		statement->CloseResultSet(rs);
		db->CloseStatement(statement);
	}
	EXPECT_TRUE(ibValue(ibDateTime()).IsEmpty());
	db->Close();
}

// NULL reads as the empty date, and IsFieldNull is what tells the two apart.
TEST(DriverDates, NullReadsAsTheEmptyDateAndSaysSo)
{
	Base base;
	base.db->RunQuery(wxT("INSERT INTO stamps (id, at) VALUES (7, NULL)"));
	base.Put(8, ibDateTime());
	bool isNull = false;
	EXPECT_TRUE(base.Get(7, &isNull).first.IsEmpty());
	EXPECT_TRUE(isNull);
	EXPECT_TRUE(base.Get(8, &isNull).first.IsEmpty());
	EXPECT_FALSE(isNull);
}

// A text a base already holds in another spelling still reads: the free-form road is kept behind
// the digits, and a date-only text is that day's midnight.
TEST(DriverDates, AnotherSpellingInTheRowStillReads)
{
	Base base;
	base.db->RunQuery(wxT("INSERT INTO stamps (id, at) VALUES (9, '2026-03-05'), (10, '2026-03-05 14:00:00.250')"));
	EXPECT_EQ(ibDateTime(2026, 3, 5), base.Get(9).first);
	EXPECT_EQ(ibDateTime(2026, 3, 5, 14, 0, 0, 250), base.Get(10).first);
}

// The dialect's WEEK is the ISO week - what the script's GetWeekOfYear (ibDateTime::GetPart, fdatetime.h) and
// the other dialects' WEEK count: a year's first week is the one holding its first Thursday, so
// 3 January 2021 is week 53 of 2020 and 30 December 2024 is week 1 of 2025. (strftime's %W counted
// from the year's first Monday and had a week 0.) The expected numbers are the calendar's, written
// down; the engine's own reading of the same rule is checked beside them, not instead of them.
TEST(DriverDates, TheWeekIsTheIsoWeekOnSqliteToo)
{
	Base base;
	const wxString tpl = base.db->GetDialect().m_datePart.at(ibDatePart::Week);
	struct Case { ibDateTime reading; const wxChar* text; long week; };
	const Case cases[] = {
		{ ibDateTime(2021, 1, 3),   wxT("'2021-01-03'"), 53 },
		{ ibDateTime(2024, 12, 30), wxT("'2024-12-30'"), 1 },
		{ ibDateTime(2020, 12, 31), wxT("'2020-12-31'"), 53 },
		{ ibDateTime(2023, 12, 31), wxT("'2023-12-31'"), 52 },
		{ ibDateTime(2026, 9, 26),  wxT("'2026-09-26'"), 39 },
	};
	for (const Case& c : cases) {
		wxString expr = tpl;
		expr.Replace(wxT("{expr}"), c.text);
		// The query door is printf-shaped, and the template holds a '%': the text goes in as an argument.
		ibDatabaseResultSet* rs = base.db->RunQueryWithResults(wxT("%s"), wxT("SELECT ") + expr + wxT(" AS w"));
		ASSERT_NE(rs, nullptr);
		ASSERT_TRUE(rs->Next());
		EXPECT_EQ(c.week, static_cast<long>(rs->GetResultInt(1))) << c.text;
		base.db->CloseResultSet(rs);
		EXPECT_EQ(c.week, c.reading.GetPart(ibDatePart::Week)) << c.text;
	}
}
