////////////////////////////////////////////////////////////////////////////
//	Description : A date through a driver - the reading goes in as its parts and comes back the same
//	reading, whatever the machine's clock does. Asked of SQLite, the one driver the suite executes.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/backend_core.h"                              // emptyDate, ibWallFromParts
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"

namespace {

struct Base {
	wxInitializer wx;
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	Base() : db(std::make_shared<ibDatabaseLayerSQLite>()) {
		EXPECT_TRUE(db->Open(wxT(":memory:")));
		db->RunQuery(wxT("CREATE TABLE stamps (id INTEGER, at TIMESTAMP)"));
	}
	void Put(int id, wxLongLong_t reading) {
		ibPreparedStatement* statement = db->PrepareStatement(wxT("INSERT INTO stamps (id, at) VALUES (?, ?)"));
		ASSERT_NE(statement, nullptr);
		statement->SetParamInt(1, id);
		statement->SetParamDate(2, reading);
		statement->RunQuery();
		db->CloseStatement(statement);
	}
	// The reading and the text the row holds, by id.
	std::pair<wxLongLong_t, wxString> Get(int id, bool* isNull = nullptr) {
		std::pair<wxLongLong_t, wxString> out(0, wxString());
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
	base.Put(1, ibWallFromParts(2026, 3, 29, 2, 30, 0));
	base.Put(2, emptyDate);
	base.Put(3, ibWallFromParts(9999, 12, 31, 23, 59, 59));
	base.Put(4, ibWallFromParts(1970, 1, 1));
	base.Put(5, ibWallFromParts(2026, 10, 25, 3, 30, 0));   // the hour that happens twice, the morning the clocks go back

	EXPECT_EQ(ibWallFromParts(2026, 3, 29, 2, 30, 0), base.Get(1).first);
	EXPECT_EQ(wxT("2026-03-29 02:30:00"), base.Get(1).second);
	EXPECT_EQ(emptyDate, base.Get(2).first);
	EXPECT_EQ(wxT("0001-01-01 00:00:00"), base.Get(2).second);
	EXPECT_EQ(ibWallFromParts(9999, 12, 31, 23, 59, 59), base.Get(3).first);
	EXPECT_EQ(ibWallFromParts(1970, 1, 1), base.Get(4).first);
	EXPECT_EQ(ibWallFromParts(2026, 10, 25, 3, 30, 0), base.Get(5).first);
}

// NULL reads as the empty date, and IsFieldNull is what tells the two apart.
TEST(DriverDates, NullReadsAsTheEmptyDateAndSaysSo)
{
	Base base;
	base.db->RunQuery(wxT("INSERT INTO stamps (id, at) VALUES (7, NULL)"));
	base.Put(8, emptyDate);
	bool isNull = false;
	EXPECT_EQ(emptyDate, base.Get(7, &isNull).first);
	EXPECT_TRUE(isNull);
	EXPECT_EQ(emptyDate, base.Get(8, &isNull).first);
	EXPECT_FALSE(isNull);
}

// A text a base already holds in another spelling still reads: the free-form road is kept behind
// the digits, and a date-only text is that day's midnight.
TEST(DriverDates, AnotherSpellingInTheRowStillReads)
{
	Base base;
	base.db->RunQuery(wxT("INSERT INTO stamps (id, at) VALUES (9, '2026-03-05'), (10, '2026-03-05 14:00:00.250')"));
	EXPECT_EQ(ibWallFromParts(2026, 3, 5), base.Get(9).first);
	EXPECT_EQ(ibWallFromParts(2026, 3, 5, 14, 0, 0, 250), base.Get(10).first);
}

// The dialect's WEEK is the ISO week - what the script's GetWeekOfYear (ibReadDatePart, fdate.h) and
// the other dialects' WEEK count: a year's first week is the one holding its first Thursday, so
// 3 January 2021 is week 53 of 2020 and 30 December 2024 is week 1 of 2025. (strftime's %W counted
// from the year's first Monday and had a week 0.) The expected numbers are the calendar's, written
// down; the engine's own reading of the same rule is checked beside them, not instead of them.
TEST(DriverDates, TheWeekIsTheIsoWeekOnSqliteToo)
{
	Base base;
	const wxString tpl = base.db->GetDialect().m_datePart.at(ibDatePart::Week);
	struct Case { wxLongLong_t reading; const wxChar* text; long week; };
	const Case cases[] = {
		{ ibWallFromParts(2021, 1, 3),   wxT("'2021-01-03'"), 53 },
		{ ibWallFromParts(2024, 12, 30), wxT("'2024-12-30'"), 1 },
		{ ibWallFromParts(2020, 12, 31), wxT("'2020-12-31'"), 53 },
		{ ibWallFromParts(2023, 12, 31), wxT("'2023-12-31'"), 52 },
		{ ibWallFromParts(2026, 9, 26),  wxT("'2026-09-26'"), 39 },
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
		EXPECT_EQ(c.week, ibReadDatePart(c.reading, ibDatePart::Week)) << c.text;
	}
}
