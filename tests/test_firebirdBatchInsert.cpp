// =============================================================================
// OES Enterprise — the BATCHED INSERT, against a live Firebird
//
// Firebird has no multi-row VALUES, so L2 spells a batch as
// `INSERT … SELECT … UNION ALL SELECT …` (ibQueryRenderer::RenderDML). That form
// was only ever checked as TEXT, and only against SQLite — which HAS multi-row
// VALUES and so never renders it. The first live Firebird write of a tabular
// section with several rows failed:
//
//     Dynamic SQL Error / SQL error code = -804 / Data type unknown
//
// In `INSERT INTO t (a) VALUES (?)` the parameter takes its type from the target
// column. In `INSERT INTO t (a) SELECT ? FROM …` it does not: the SELECT is typed
// on its own, and a bare `?` there has nothing to be typed from.
//
// The four PROBE tests below are the answers this engine actually gave, kept
// because they are what rules out the cheaper spellings. The last test is the
// one that guards the fix: the REAL renderer's output, executed for real.
//
// SKIPS when no Firebird client can be loaded — CI runners have none (see
// docs/portability.md), which is exactly why the defect reached a user.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/backend_exception.h"
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/preparedStatement.h"
#include "backend/databaseLayer/databaseResultSet.h"

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include <chrono>

namespace {

wxString ScratchDbPath(const char* testName)
{
	const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	return wxFileName(wxStandardPaths::Get().GetTempDir(),
		wxString::Format(wxT("oes_fb_batch_%s_%lld.fdb"), testName, (long long)now)).GetFullPath();
}

// The failing table's shape, trimmed to the column KINDS that matter: the row
// owner (a reference key), a type tag, a number and a string.
const wxChar* kCreateTable =
	wxT("CREATE TABLE oes_batch_probe (")
	wxT("Row_RRRef VARCHAR(36), ")
	wxT("fld_TYPE SMALLINT, ")
	wxT("fld_N NUMERIC(18,4), ")
	wxT("fld_S VARCHAR(150))");

class FirebirdBatchInsert : public ::testing::Test {
protected:
	// shared_ptr, not a stack object: a statement holds a weak_ptr back to the
	// layer, and a stack layer makes the first RunQuery throw bad_weak_ptr.
	std::shared_ptr<ibDatabaseLayerFirebird> layer;
	wxString dbPath;

	void SetUp() override {
		dbPath = ScratchDbPath(::testing::UnitTest::GetInstance()->current_test_info()->name());
		layer = std::make_shared<ibDatabaseLayerFirebird>();
		layer->SetUser(wxT("SYSDBA"));
		layer->SetPassword(wxT("masterkey"));
		bool opened = false;
		try { opened = layer->Open(dbPath); }
		catch (...) { opened = false; }
		if (!opened)
			GTEST_SKIP() << "no Firebird client / cannot create " << dbPath.ToStdString();
		layer->RunQuery(kCreateTable);
		layer->Commit();
	}

	void TearDown() override {
		if (layer && layer->IsOpen()) layer->Close();
		layer.reset();
		if (wxFileExists(dbPath)) wxRemoveFile(dbPath);
	}

	// Prepare only — -804 is a PREPARE failure, and preparing writes nothing.
	// Empty string = the engine accepted it; otherwise its complaint.
	std::string PrepareError(const wxString& sql) {
		try {
			ibPreparedStatement* stmt = layer->PrepareStatement(wxT("%s"), sql);
			if (stmt == nullptr) return "prepare returned null";
			layer->CloseStatement(stmt);
			return std::string();
		}
		catch (const ibBackendException& err) { return err.what(); }
		catch (...) { return "unknown exception"; }
	}

	static bool IsTypeUnknown(const std::string& err) {
		return err.find("-804") != std::string::npos;
	}
};

// ---------------------------------------------------------------------------
// PROBES — what this engine will and will not accept. Three cheaper spellings
// were candidates for the fix; these are the answers that ruled them out.
// ---------------------------------------------------------------------------

TEST_F(FirebirdBatchInsert, BareParametersInUnionBranchesAreUntyped)
{
	// The form that shipped, and the reported failure.
	EXPECT_TRUE(IsTypeUnknown(PrepareError(
		wxT("INSERT INTO oes_batch_probe (Row_RRRef, fld_TYPE, fld_N, fld_S)")
		wxT(" SELECT ?, ?, ?, ? FROM RDB$DATABASE")
		wxT(" UNION ALL SELECT ?, ?, ?, ? FROM RDB$DATABASE"))));
}

TEST_F(FirebirdBatchInsert, CastingEveryPlaceholderPrepares)
{
	// The fix. TYPE OF COLUMN so the ENGINE looks the type up, rather than the
	// renderer holding a second opinion about what the column holds.
	EXPECT_EQ(PrepareError(
		wxT("INSERT INTO oes_batch_probe (Row_RRRef, fld_TYPE, fld_N, fld_S)")
		wxT(" SELECT CAST(? AS TYPE OF COLUMN oes_batch_probe.Row_RRRef),")
		wxT(" CAST(? AS TYPE OF COLUMN oes_batch_probe.fld_TYPE),")
		wxT(" CAST(? AS TYPE OF COLUMN oes_batch_probe.fld_N),")
		wxT(" CAST(? AS TYPE OF COLUMN oes_batch_probe.fld_S) FROM RDB$DATABASE")
		wxT(" UNION ALL SELECT CAST(? AS TYPE OF COLUMN oes_batch_probe.Row_RRRef),")
		wxT(" CAST(? AS TYPE OF COLUMN oes_batch_probe.fld_TYPE),")
		wxT(" CAST(? AS TYPE OF COLUMN oes_batch_probe.fld_N),")
		wxT(" CAST(? AS TYPE OF COLUMN oes_batch_probe.fld_S) FROM RDB$DATABASE")), std::string());
}

// REJECTED CHEAPER SPELLING #1 — type the first branch and let the union carry
// the rest. It would have made the statement text far shorter. It does not work:
// Firebird types every branch independently.
TEST_F(FirebirdBatchInsert, TypingOnlyTheFirstBranchIsNotEnough)
{
	EXPECT_TRUE(IsTypeUnknown(PrepareError(
		wxT("INSERT INTO oes_batch_probe (Row_RRRef, fld_TYPE, fld_N, fld_S)")
		wxT(" SELECT CAST(? AS TYPE OF COLUMN oes_batch_probe.Row_RRRef),")
		wxT(" CAST(? AS TYPE OF COLUMN oes_batch_probe.fld_TYPE),")
		wxT(" CAST(? AS TYPE OF COLUMN oes_batch_probe.fld_N),")
		wxT(" CAST(? AS TYPE OF COLUMN oes_batch_probe.fld_S) FROM RDB$DATABASE")
		wxT(" UNION ALL SELECT ?, ?, ?, ? FROM RDB$DATABASE"))));
}

// REJECTED CHEAPER SPELLING #2 — seed the union with a zero-row SELECT off the
// target table, which types every union column while naming no type at all and
// costs one short branch. Same answer: the parameters stay untyped.
TEST_F(FirebirdBatchInsert, ATypedZeroRowSeedBranchIsNotEnough)
{
	EXPECT_TRUE(IsTypeUnknown(PrepareError(
		wxT("INSERT INTO oes_batch_probe (Row_RRRef, fld_TYPE, fld_N, fld_S)")
		wxT(" SELECT Row_RRRef, fld_TYPE, fld_N, fld_S FROM oes_batch_probe WHERE 1 = 0")
		wxT(" UNION ALL SELECT ?, ?, ?, ? FROM RDB$DATABASE")
		wxT(" UNION ALL SELECT ?, ?, ?, ? FROM RDB$DATABASE"))));
}

// ---------------------------------------------------------------------------
// THE GUARD — the real renderer's output, executed, rows read back.
// ---------------------------------------------------------------------------

TEST_F(FirebirdBatchInsert, RenderedBatchWritesEveryRow)
{
	const int kRows = 12;   // the reported failure carried twelve tabular-section rows

	ibDmlStatement ins(ibDmlKind::Insert);
	ins.m_table = wxT("oes_batch_probe");

	const auto row = [](int n) {
		return std::vector<ibQueryExprPtr>{
			ibConst(ibValue(wxString::Format(wxT("owner-%d"), n))),
			ibConst(ibValue(ibNumber(1))),
			ibConst(ibValue(ibNumber(n))),
			ibConst(ibValue(wxString::Format(wxT("line %d"), n))),
		};
	};
	const std::vector<ibQueryExprPtr> first = row(0);
	const wxString columns[] = { wxT("Row_RRRef"), wxT("fld_TYPE"), wxT("fld_N"), wxT("fld_S") };
	for (size_t k = 0; k < 4; ++k)
		ins.m_assignments.push_back(ibDmlAssign{ columns[k], first[k] });
	for (int r = 1; r < kRows; ++r)
		ins.m_extraRows.push_back(row(r));

	const ibRenderedQuery rendered =
		ibQueryRenderer(ibDatabaseLayerFirebird::Dialect()).RenderDML(ins);

	ibPreparedStatement* stmt = layer->PrepareStatement(wxT("%s"), rendered.m_sql);
	ASSERT_NE(stmt, nullptr) << rendered.m_sql.ToStdString();

	ASSERT_EQ(rendered.m_params.size(), (size_t)(kRows * 4));
	for (size_t i = 0; i < rendered.m_params.size(); ++i) {
		const ibValue& v = rendered.m_params[i].m_value;
		if (v.GetType() == ibValueTypes::TYPE_NUMBER) stmt->SetParamNumber((int)i + 1, v.GetNumber());
		else                                          stmt->SetParamString((int)i + 1, v.GetString());
	}
	EXPECT_EQ(stmt->RunQuery(), kRows);
	layer->CloseStatement(stmt);
	layer->Commit();

	ibDatabaseResultSet* rs = layer->RunQueryWithResults(
		wxT("SELECT COUNT(*), SUM(fld_N) FROM oes_batch_probe"));
	ASSERT_NE(rs, nullptr);
	ASSERT_TRUE(rs->Next());
	EXPECT_EQ(rs->GetResultInt(1), kRows);
	EXPECT_EQ(rs->GetResultInt(2), (kRows - 1) * kRows / 2);   // 0+1+…+11, so no row was dropped
	layer->CloseResultSet(rs);
}

// THE WIDTH QUESTION. A cast is ~50 characters where a bare `?` was one, and the
// batch chunk is 50 rows. On the widest thing that writes this way — a register
// line or a tabular section with twenty-odd physical columns — that is a statement
// of tens of kilobytes, and Firebird's DSQL text has historically had a ceiling.
// So: build the realistic worst case and ask the engine, rather than inventing a
// narrower chunk against a limit nobody measured.
//
// MEASURED: 56548 characters, 1050 parameters — and it prepares. No chunk cap was
// added, because at the widest thing that exists there is nothing to cap. It is
// not far from 64 KB though, so a much wider row would want the chunk sized in
// CELLS rather than rows; this test is where that would be noticed, loudly.
TEST_F(FirebirdBatchInsert, WidestRealisticChunkStillPrepares)
{
	const int kCols = 21;   // the reported catalog's own width
	const int kRows = 50;   // dbTableProvider's kRowsPerStatement

	wxString create = wxT("CREATE TABLE oes_batch_wide (Row_RRRef VARCHAR(36)");
	std::vector<wxString> columns{ wxT("Row_RRRef") };
	for (int c = 1; c < kCols; ++c) {
		const wxString name = wxString::Format(wxT("fld%d_S"), 1100 + c);   // real names are this long
		columns.push_back(name);
		create += wxT(", ") + name + wxT(" VARCHAR(150)");
	}
	layer->RunQuery(wxT("%s"), create + wxT(")"));
	layer->Commit();

	ibDmlStatement ins(ibDmlKind::Insert);
	ins.m_table = wxT("oes_batch_wide");
	const auto row = [&](int n) {
		std::vector<ibQueryExprPtr> v;
		for (int c = 0; c < kCols; ++c)
			v.push_back(ibConst(ibValue(wxString::Format(wxT("r%dc%d"), n, c))));
		return v;
	};
	const std::vector<ibQueryExprPtr> first = row(0);
	for (int c = 0; c < kCols; ++c)
		ins.m_assignments.push_back(ibDmlAssign{ columns[c], first[c] });
	for (int r = 1; r < kRows; ++r)
		ins.m_extraRows.push_back(row(r));

	const ibRenderedQuery rendered =
		ibQueryRenderer(ibDatabaseLayerFirebird::Dialect()).RenderDML(ins);
	std::cout << "widest chunk: " << rendered.m_sql.length() << " chars, "
	          << rendered.m_params.size() << " parameters" << std::endl;

	EXPECT_EQ(PrepareError(rendered.m_sql), std::string());
}

} // namespace
