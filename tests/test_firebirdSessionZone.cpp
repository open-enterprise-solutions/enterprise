////////////////////////////////////////////////////////////////////////////
//	Description : a Firebird session put into the base's zone - LOCALTIMESTAMP reads in it.
//	Runs where the Firebird kit is laid out beside the binary (CI's Firebird job); skips elsewhere.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include <chrono>
#include <cstdlib>

#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#include "backend/session/serverClock.h"   // the clock, measured through a session in the base's zone

namespace {

wxString ScratchDbPath(const char* testName)
{
	const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	return wxFileName(wxStandardPaths::Get().GetTempDir(),
		wxString::Format(wxT("oes_fb_zone_%s_%lld.fdb"), testName, (long long)now)).GetFullPath();
}

class FirebirdSessionZone : public ::testing::Test {
protected:
	std::shared_ptr<ibDatabaseLayerFirebird> layer;
	wxString dbPath;

	void SetUp() override {
		dbPath = ScratchDbPath(::testing::UnitTest::GetInstance()->current_test_info()->name());
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
	}
	void TearDown() override {
		if (layer && layer->IsOpen()) layer->Close();
		layer.reset();
		if (wxFileExists(dbPath)) wxRemoveFile(dbPath);
	}

	// The machine's clock read in UTC, as a reading - the oracle, outside the server. (A CAST of a
	// zoned timestamp to a plain one converts to the SESSION's zone first, so the server cannot be
	// asked for UTC that way.)
	static wxLongLong_t MachineUtc() {
		const wxDateTime::Tm tm = wxDateTime::Now().GetTm(wxDateTime::UTC);
		return ibWallFromParts(tm.year, static_cast<unsigned>(tm.mon) + 1, tm.mday, tm.hour, tm.min, tm.sec, tm.msec);
	}
	// The server's local reading against the machine's UTC one, milliseconds apart.
	long long LocalMinusUtcMs(ibDatabaseLayer& on) {
		const wxLongLong_t local = on.GetSingleResultDate(wxT("SELECT LOCALTIMESTAMP FROM RDB$DATABASE"), 1);
		return static_cast<long long>(local - MachineUtc());
	}
};

} // namespace

// The attach stands at UTC (as it always did), so a session's local time is UTC until the base's
// zone is put on it; put into Kyiv's it reads two hours ahead in winter and three in summer.
TEST_F(FirebirdSessionZone, TheSessionReadsInTheZoneItIsPutInto)
{
	ASSERT_TRUE(layer->HasSessionTimeZone());
	EXPECT_LT(std::llabs(LocalMinusUtcMs(*layer)), 5000) << "the attach is at UTC";

	ASSERT_TRUE(layer->SetSessionTimeZone(wxT("Europe/Kyiv")));
	EXPECT_EQ(wxT("Europe/Kyiv"), layer->GetSessionTimeZone());
	const long long apart = LocalMinusUtcMs(*layer);
	EXPECT_TRUE(std::llabs(apart - 7200000) < 5000 || std::llabs(apart - 10800000) < 5000) << "Kyiv is UTC+2 or UTC+3, read " << apart;

	// A zone the server does not know is refused, and the one in force stays.
	EXPECT_FALSE(layer->SetSessionTimeZone(wxT("Nowhere/Nowhere")));
	EXPECT_EQ(wxT("Europe/Kyiv"), layer->GetSessionTimeZone());

	// Back to UTC by name.
	ASSERT_TRUE(layer->SetSessionTimeZone(wxEmptyString));
	EXPECT_LT(std::llabs(LocalMinusUtcMs(*layer)), 5000);
}

// A clone of the connection - what the pool hands out - starts in the engine's own zone (nothing
// owns it while it is being made, so no statement can run on it) and takes the base's zone when
// told, which is what the pool does the moment it hands the clone out.
TEST_F(FirebirdSessionZone, ACloneTakesTheZoneWhenHandedOut)
{
	ASSERT_TRUE(layer->SetSessionTimeZone(wxT("Europe/Kyiv")));
	std::shared_ptr<ibDatabaseLayer> clone(layer->Clone());
	ASSERT_TRUE(clone != nullptr && clone->IsOpen());
	EXPECT_TRUE(clone->GetSessionTimeZone().IsEmpty());
	ASSERT_TRUE(clone->SetSessionTimeZone(wxT("Europe/Kyiv")));
	EXPECT_EQ(wxT("Europe/Kyiv"), clone->GetSessionTimeZone());
	const long long apart = LocalMinusUtcMs(*clone);
	EXPECT_TRUE(std::llabs(apart - 7200000) < 5000 || std::llabs(apart - 10800000) < 5000) << apart;
	clone->Close();
}

// The clock is measured in the base's zone or not at all: with no zone named the session's clock at UTC
// is a reading of nothing for this base and is not taken; a zone the server refuses is not measured
// in; a zone it takes is put on THIS connection first and LOCALTIMESTAMP read there - so a connection
// bound before the zone was saved comes to stand in it at its next measurement.
TEST_F(FirebirdSessionZone, TheClockIsMeasuredInTheBasesZoneOrNotAtAll)
{
	ibServerClock::Reset();
	EXPECT_FALSE(ibServerClock::Refresh(*layer, wxEmptyString)) << "no zone named";
	EXPECT_EQ(0, ibServerClock::Offset());
	EXPECT_TRUE(layer->GetSessionTimeZone().IsEmpty());

	EXPECT_FALSE(ibServerClock::Refresh(*layer, wxT("Nowhere/Nowhere"))) << "a zone the server refuses";
	EXPECT_EQ(0, ibServerClock::Offset());
	EXPECT_TRUE(layer->GetSessionTimeZone().IsEmpty());

	ASSERT_TRUE(ibServerClock::Refresh(*layer, wxT("Europe/Kyiv")));
	EXPECT_EQ(wxT("Europe/Kyiv"), layer->GetSessionTimeZone()) << "put into the zone by the measurement";
	// Now() is Kyiv's wall clock, wherever this machine stands: two or three hours ahead of its UTC reading.
	const long long apart = static_cast<long long>(ibServerClock::Now() - MachineUtc());
	EXPECT_TRUE(std::llabs(apart - 7200000) < 5000 || std::llabs(apart - 10800000) < 5000) << apart;
	ibServerClock::Reset();
}

// A closed connection forgets its zone: the next attach stands at UTC, and a connection reporting a
// zone it no longer stands in would be measured in the wrong one.
TEST_F(FirebirdSessionZone, AClosedConnectionForgetsItsZone)
{
	ASSERT_TRUE(layer->SetSessionTimeZone(wxT("Europe/Kyiv")));
	layer->Close();
	EXPECT_TRUE(layer->GetSessionTimeZone().IsEmpty());
	ASSERT_TRUE(layer->Open(dbPath));
	EXPECT_TRUE(layer->GetSessionTimeZone().IsEmpty());
	EXPECT_LT(std::llabs(LocalMinusUtcMs(*layer)), 5000) << "the fresh attach is at UTC";
}
