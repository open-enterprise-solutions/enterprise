////////////////////////////////////////////////////////////////////////////
//	Description : Firebird counts a region's time by the zone rules laid out beside it (_fb/tzdata).
//	Without them the ICU built into the kit knows 2018's rules, and a zone named since - Europe/Kyiv,
//	2022 - is taken, reported, and counted as UTC without a word.
//	Runs where the Firebird kit is laid out beside the binary (CI's Firebird job); skips elsewhere.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include <chrono>
#include <memory>

#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"

namespace {

class FirebirdZoneRules : public ::testing::Test {
protected:
	std::shared_ptr<ibDatabaseLayerFirebird> layer;
	wxString dbPath;

	void SetUp() override {
		const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		dbPath = wxFileName(wxStandardPaths::Get().GetTempDir(), wxString::Format(wxT("oes_fb_zones_%s_%lld.fdb"),
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
	}
	void TearDown() override {
		if (layer && layer->IsOpen()) layer->Close();
		layer.reset();
		if (wxFileExists(dbPath)) wxRemoveFile(dbPath);
	}

	// The zone's offset from UTC, in hours, at a moment named in that zone.
	int HoursAt(const wxString& moment) {
		return layer->GetSingleResultInt(wxT("SELECT EXTRACT(TIMEZONE_HOUR FROM TIMESTAMP '") + moment +
			wxT("') FROM RDB$DATABASE"), 1);
	}
};

} // namespace

// Kyiv's winter and summer offsets come from the rules beside the engine. Under the rules built into the
// kit's ICU both are 0: the name is accepted and the clock is UTC.
TEST_F(FirebirdZoneRules, KyivIsCountedByItsOwnRules)
{
	EXPECT_EQ(2, HoursAt(wxT("2026-01-15 12:00:00 Europe/Kyiv")));
	EXPECT_EQ(3, HoursAt(wxT("2026-07-15 12:00:00 Europe/Kyiv")));
}
