////////////////////////////////////////////////////////////////////////////
//	Description : A date through the Firebird driver - the reading goes in as its parts and comes back
//	the same reading; a DATE is its day, a TIME is its time of day on the empty date's day.
//	Runs where the Firebird kit is laid out beside the binary (CI's Firebird job); skips elsewhere.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include <chrono>
#include <memory>

#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/preparedStatement.h"

namespace {

class FirebirdDates : public ::testing::Test {
protected:
	std::shared_ptr<ibDatabaseLayerFirebird> layer;
	wxString dbPath;

	void SetUp() override {
		const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		dbPath = wxFileName(wxStandardPaths::Get().GetTempDir(), wxString::Format(wxT("oes_fb_dates_%s_%lld.fdb"),
			::testing::UnitTest::GetInstance()->current_test_info()->name(), (long long)now)).GetFullPath();
		try {
			layer = std::make_shared<ibDatabaseLayerFirebird>();
			layer->SetUser(wxT("SYSDBA"));
			layer->SetPassword(wxT("masterkey"));
			if (!layer->Open(dbPath))
				layer.reset();
		}
		catch (...) { layer.reset(); }
		if (!layer)
			GTEST_SKIP() << "no Firebird client on this machine (" << dbPath.ToStdString() << ")";
		layer->RunQuery(wxT("CREATE TABLE stamps (id INTEGER, stamp TIMESTAMP, d DATE, t TIME)"));
		layer->Commit();
	}
	void TearDown() override {
		if (layer && layer->IsOpen()) layer->Close();
		layer.reset();
		if (wxFileExists(dbPath)) wxRemoveFile(dbPath);
	}
};

} // namespace

// The parts a TIMESTAMP holds are the parts the value held (the column is not named `at`: a reserved word since Firebird 4): at both ends of the axis, on the empty date,
// and at 02:30 of a morning some machine's clock skips - no clock stands on the road.
TEST_F(FirebirdDates, ATimestampComesBackAsItWentIn)
{
	const ibDateTime readings[] = {
		ibDateTime(),
		ibDateTime(2026, 3, 29, 2, 30),        // the hour Central European clocks skip that morning
		ibDateTime(2026, 3, 8, 2, 30, 15),     // ...and the American ones
		ibDateTime(1950, 5, 1, 23, 59, 59),
		ibDateTime(9999, 12, 31, 23, 59, 59),
	};
	const int count = static_cast<int>(sizeof readings / sizeof readings[0]);
	for (int id = 1; id <= count; ++id) {
		ibPreparedStatement* statement = layer->PrepareStatement(wxT("INSERT INTO stamps (id, stamp) VALUES (?, ?)"));
		ASSERT_NE(statement, nullptr);
		statement->SetParamInt(1, id);
		statement->SetParamDate(2, readings[id - 1]);
		EXPECT_EQ(1, statement->RunQuery());
		layer->CloseStatement(statement);
	}
	layer->Commit();

	ibDatabaseResultSet* rs = layer->RunQueryWithResults(wxT("SELECT id, stamp FROM stamps ORDER BY id"));
	ASSERT_NE(rs, nullptr);
	int seen = 0;
	while (rs->Next()) {
		const int id = rs->GetResultInt(1);
		ASSERT_TRUE(id >= 1 && id <= count);
		EXPECT_EQ(readings[id - 1], rs->GetResultDate(2)) << "row " << id;
		++seen;
	}
	layer->CloseResultSet(rs);
	EXPECT_EQ(count, seen);
}

// A DATE column is its day at midnight; a TIME column is that time of day on the empty date's day
// (0001-01-01) - the reading the reference system keeps for a time with no date: filled, and nothing
// but the time in it. NULL in either reads as the empty date, and IsFieldNull tells it from a stored one.
TEST_F(FirebirdDates, ADateIsItsDayAndATimeIsItsTimeOfDay)
{
	layer->RunQuery(wxT("INSERT INTO stamps (id, d, t) VALUES (1, DATE '2026-09-26', TIME '10:30:15')"));
	layer->RunQuery(wxT("INSERT INTO stamps (id, d, t) VALUES (2, NULL, NULL)"));
	layer->Commit();

	ibDatabaseResultSet* rs = layer->RunQueryWithResults(wxT("SELECT id, d, t FROM stamps ORDER BY id"));
	ASSERT_NE(rs, nullptr);
	ASSERT_TRUE(rs->Next());
	EXPECT_EQ(ibDateTime(2026, 9, 26), rs->GetResultDate(2));
	EXPECT_EQ(ibDateTime(1, 1, 1, 10, 30, 15), rs->GetResultDate(3));
	EXPECT_FALSE(rs->GetResultDate(3).IsEmpty()) << "a time of day is a filled value";
	ASSERT_TRUE(rs->Next());
	EXPECT_TRUE(rs->GetResultDate(2).IsEmpty());
	EXPECT_TRUE(rs->IsFieldNull(2));
	EXPECT_TRUE(rs->GetResultDate(3).IsEmpty());
	EXPECT_TRUE(rs->IsFieldNull(3));
	layer->CloseResultSet(rs);
}
