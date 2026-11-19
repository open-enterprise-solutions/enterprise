////////////////////////////////////////////////////////////////////////////
//	Description : A number through the Firebird driver - a NUMERIC is its digits and its scale; a floating
//	value the server computed is the decimal it stands for, at its type's own digits; a remainder is a number in memory's.
//	Runs where the Firebird kit is laid out beside the binary (CI's Firebird job); skips elsewhere.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include <chrono>
#include <iterator>   // std::size
#include <memory>

#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/preparedStatement.h"

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

// ⭐ A REMAINDER IS THE ONE A NUMBER IN MEMORY GIVES (ibDialectDictionary::m_modulo): its dividend's sign, its fraction
// kept. The renderer wrote `%`, which Firebird has not got - the statement was refused - and Firebird's MOD() rounds a
// fraction first, 7.5 MOD 2 = 0. Two columns, and a column and a constant fraction.
TEST_F(FirebirdNumbers, ARemainderIsTheOneANumberInMemoryGives)
{
	layer->RunQuery(wxT("CREATE TABLE oes_remainder (id INTEGER, a NUMERIC(15,3), b NUMERIC(15,3))"));
	layer->Commit();
	const wxChar* const rows[][2] = {
		{ wxT("7"), wxT("3") }, { wxT("7.5"), wxT("2") }, { wxT("-7.5"), wxT("2") }, { wxT("10.25"), wxT("-3") },
		{ wxT("0.3"), wxT("0.1") }, { wxT("123456789.125"), wxT("0.25") },
	};
	for (size_t r = 0; r < std::size(rows); ++r)
		layer->RunQuery(wxT("INSERT INTO oes_remainder (id, a, b) VALUES (%d, %s, %s)"),
			(int)r, ibString(rows[r][0]), ibString(rows[r][1]));
	layer->Commit();

	const ibNumber byConstant(ibString(wxT("2.5")));
	const ibQueryIR ir(ibProject(ibScan(wxT("oes_remainder")), {
		{ ibCol(wxT("id")), wxT("id") },
		{ ibBinOp(ibQueryBinOp::Mod, ibCol(wxT("a")), ibCol(wxT("b"))), wxT("v") },
		{ ibBinOp(ibQueryBinOp::Mod, ibCol(wxT("a")), ibConst(ibValue(byConstant))), wxT("w") },
	}));
	const ibRenderedQuery rendered = ibQueryRenderer(ibDatabaseLayerFirebird::Dialect()).Render(ir);
	ibPreparedStatement* statement = layer->PrepareStatement(wxT("%s"), rendered.m_sql);
	ASSERT_NE(statement, nullptr) << rendered.m_sql.ToStdString();
	for (size_t i = 0; i < rendered.m_params.size(); ++i)
		statement->SetParamNumber((int)i + 1, rendered.m_params[i].m_value.GetNumber());
	ibDatabaseResultSet* rs = statement->RunQueryWithResults();
	ASSERT_NE(rs, nullptr) << rendered.m_sql.ToStdString();
	size_t read = 0;
	while (rs->Next()) {
		const int id = rs->GetResultInt(1);
		ASSERT_TRUE(id >= 0 && id < (int)std::size(rows));
		const ibString aText(rows[id][0]), bText(rows[id][1]);
		const ibNumber a(aText), b(bText);
		EXPECT_EQ(0, rs->GetResultNumber(2).Compare(a % b)) << aText.ToStdString() << " % " << bText.ToStdString()
			<< " = " << rs->GetResultNumber(2).ToString().ToStdString() << ", not " << (a % b).ToString().ToStdString();
		EXPECT_EQ(0, rs->GetResultNumber(3).Compare(a % byConstant)) << aText.ToStdString() << " % 2.5 = "
			<< rs->GetResultNumber(3).ToString().ToStdString() << ", not " << (a % byConstant).ToString().ToStdString();
		++read;
	}
	EXPECT_EQ(read, std::size(rows));
	statement->CloseResultSet(rs);
	layer->CloseStatement(statement);
}
