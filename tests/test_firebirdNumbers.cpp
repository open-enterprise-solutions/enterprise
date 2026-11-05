////////////////////////////////////////////////////////////////////////////
//	Description : A number through the Firebird driver - a NUMERIC is its digits and its scale; a floating
//	value the server computed is the decimal it stands for, at its type's own digits.
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

namespace {

class FirebirdNumbers : public ::testing::Test {
protected:
	std::shared_ptr<ibDatabaseLayerFirebird> layer;
	ibString dbPath;

	void SetUp() override {
		const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		dbPath = wxFileName(wxStandardPaths::Get().GetTempDir(), ibString::Format(wxT("oes_fb_numbers_%s_%lld.fdb"),
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
};

} // namespace

// A floating value reads at its type's own digits - fifteen for a DOUBLE PRECISION, six for a FLOAT - as SQLite spells
// its doubles: 0.43, not the double's 0.42999999999999999, and 0.1, not the float's 0.10000000149011612 (2026-11-05).
// A NUMERIC reads exactly, and NULL as zero.
TEST_F(FirebirdNumbers, AFloatingValueReadsAsTheDecimalItStandsFor)
{
	ibDatabaseResultSet* rs = layer->RunQueryWithResults(wxT("SELECT CAST(0.43 AS DOUBLE PRECISION), CAST(0.1 AS FLOAT), CAST(12.345 AS NUMERIC(9,3)), CAST(NULL AS DOUBLE PRECISION) FROM RDB$DATABASE"));
	ASSERT_NE(rs, nullptr);
	ASSERT_TRUE(rs->Next());
	EXPECT_EQ(0, rs->GetResultNumber(1).Compare(ibNumber(ibString(wxT("0.43"))))) << rs->GetResultNumber(1).ToString();
	EXPECT_EQ(0, rs->GetResultNumber(2).Compare(ibNumber(ibString(wxT("0.1"))))) << rs->GetResultNumber(2).ToString();
	EXPECT_EQ(0, rs->GetResultNumber(3).Compare(ibNumber(ibString(wxT("12.345"))))) << rs->GetResultNumber(3).ToString();
	EXPECT_EQ(0, rs->GetResultNumber(4).Compare(ibNumber())) << rs->GetResultNumber(4).ToString();
	layer->CloseResultSet(rs);
}
