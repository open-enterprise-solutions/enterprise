// =============================================================================
// L4-2 — LINQ push-down EXECUTION parity. The gap the recorder / dispatch tests leave.
//
// The recorder tests (test_lambdaRecorder) stop at the AST SHAPE; the method-table
// tests (test_linqMethod) stop at name -> enum dispatch. NEITHER runs the lowered
// predicate against data. This harness closes the loop end to end:
//
//   lambda body text --(script lexer + ibBuildLambdaQueryAst)--> ibQueryAstExpr   [L4-2 recorder]
//                    --(ibQueryLowering::LowerLambdaPredicate)--> ibQueryPredicate [L4-2 lowering]
//                    --(ibQueryComposer::FilterRows)------------> rows             [L3 RAM core]
//
// and asserts the row count equals the SAME filter as SQL against an in-memory SQLite —
// the LINQ trap through the LINQ front-end: a predicate LOWERED FROM A LAMBDA must mean
// the SAME thing server-side and RAM-side, NULL / three-valued (Kleene) logic included.
// Plus the bail path: an untranslatable-at-LOWERING body (an unresolvable column) yields
// null -> the Queryable fold falls back to RAM, never a thrown user error.
//
// Pure + session-free: LowerLambdaPredicate takes the source AS AN ARGUMENT (no
// activeMetaData), so a metadata-free TestQueryable drives it — exactly what
// test_queryComposer proves a queryable may be. (L4-1 text Execute() resolves names
// against activeMetaData and is NOT reachable here — L4-2 is where the executable-parity
// test lives. See docs/query-engine-layers.md §L4, docs/query-language-arc.md §23.5.)
// =============================================================================

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <vector>

#include "backend/compiler/value.h"           // ibValue / ibNumber
#include "backend/compiler/compileCode.h"     // ibCompileCode — the script lexer feeding the recorder
#include "backend/compiler/lambdaQueryAst.h"  // ibBuildLambdaQueryAst (L4-2 recorder)
#include "backend/query/queryAst.h"           // ibQueryAstExpr
#include "backend/query/queryLowering.h"      // ibQueryLowering::LowerLambdaPredicate (L4-2 lowering)
#include "backend/query/queryProvider.h"      // ibQueryComposer::FilterRows + ibQueryRamTable
#include "backend/query/queryable.h"          // ibBackendQueryable / ibQueryPredicate(Ptr)
#include "backend/query/queryColumn.h"        // ibBackendQueryColumn / ibTypeDescription

#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"

namespace {

const ibTypeDescription kNoType;

// Minimal metadata-free column (name + model id read key). Mirrors TestCol elsewhere.
class TestCol : public ibBackendQueryColumn {
public:
	TestCol(const wxString& name, ibMetaID id) : m_name(name), m_id(id) {}
	wxString           GetName()         const override { return m_name; }
	wxString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }
	ibMetaID           GetColumnId()     const override { return m_id; }
private:
	wxString                  m_name;
	ibMetaID                  m_id;
	mutable ibTypeDescription m_type;
};

// Metadata-free source vending its columns by name — what LowerLambdaPredicate resolves the
// lambda's member paths through. (Same shape as test_queryComposer's mock queryable.)
class TestQueryable : public ibBackendQueryable {
public:
	TestQueryable(const wxString& table, ibMetaID metaId) : m_table(table), m_metaId(metaId) {}
	void AddCol(const ibBackendQueryColumn* c) { m_cols.push_back(c); }

	wxString GetQueryTableName() const override { return m_table; }
	ibMetaID GetQueryTableId()   const override { return m_metaId; }
	ibGuid   GetQueryTableGuid() const override {
		ibGuidImpl impl{};
		impl.m_data1 = static_cast<unsigned long>(m_metaId);
		return ibGuid(impl);
	}
	bool     IsComputedInRam()   const override { return false; }   // physical source (dot-walk allowed; unused here)
	const ibMetaData* GetMetaData() const override { return nullptr; }
	std::vector<const ibBackendQueryColumn*> GetColumns() const override { return m_cols; }
	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c->GetName() == name) return c;
		return nullptr;
	}
	bool OwnsColumn(const ibBackendQueryColumn* col) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c == col) return true;
		return false;
	}
private:
	wxString m_table;
	ibMetaID m_metaId;
	std::vector<const ibBackendQueryColumn*> m_cols;
};

// The shared fixture: region { North, South, East, <NULL> } with qty — same as test_queryParity.
// The NULL-region row is the whole point: it is where three-valued logic bites, now reached
// through the LINQ front-end instead of a hand-built predicate.
const ibMetaID REGION = 1, QTY = 2;

ibQueryRamTable MakeRamFixture()
{
	ibQueryRamTable t;
	t.AddColumn(REGION, wxT("region"), kNoType);
	t.AddColumn(QTY,    wxT("qty"),    kNoType);
	auto row = [&](const wxString& reg, long q, bool regNull) {
		const long r = t.AppendRow();
		// A DB NULL materialises as ibValue(TYPE_NULL) (the driver), not an unset/Undefined cell.
		t.SetCell(r, REGION, regNull ? ibValue(ibValueTypes::TYPE_NULL) : ibValue(reg));
		t.SetCell(r, QTY, ibValue(ibNumber(q)));
	};
	row(wxT("North"), 10, false);
	row(wxT("South"),  5, false);
	row(wxT("East"),   7, false);
	row(wxString(),    3, true);   // region IS NULL
	return t;
}

bool MakeSqlFixture(ibDatabaseLayerSQLite& db)
{
	if (!db.Open(wxT(":memory:")))
		return false;
	db.RunQuery(wxT("CREATE TABLE t (region TEXT, qty INTEGER)"));
	db.RunQuery(wxT("INSERT INTO t (region, qty) VALUES ('North', 10)"));
	db.RunQuery(wxT("INSERT INTO t (region, qty) VALUES ('South', 5)"));
	db.RunQuery(wxT("INSERT INTO t (region, qty) VALUES ('East', 7)"));
	db.RunQuery(wxT("INSERT INTO t (region, qty) VALUES (NULL, 3)"));
	return true;
}

int SqlCount(ibDatabaseLayerSQLite& db, const wxString& where)
{
	const wxString q = wxT("SELECT COUNT(*) AS cnt FROM t WHERE ") + where;
	// Pass the query as a %s ARGUMENT, not the format string (a literal '%' in a LIKE would fuse).
	ibDatabaseResultSet* rs = db.RunQueryWithResults(wxT("%s"), q);
	if (rs == nullptr)
		return -1;
	const int n = rs->Next() ? rs->GetResultInt(wxT("cnt")) : -1;
	db.CloseResultSet(rs);
	return n;
}

// Tokenize a lambda BODY snippet and record it into the L4-2 query AST (as test_lambdaRecorder does):
// the SCRIPT lexer produces the lexemes, the recorder builds the ibQueryAstExpr — no compiler run,
// no metadata, no database.
std::shared_ptr<ibQueryAstExpr> Record(const wxString& body, const wxString& rowParam = wxT("x"))
{
	ibCompileCode cc;
	cc.Load(body);
	if (!cc.PrepareLexem())
		return nullptr;
	const std::vector<ibLexem>& lex = cc.GetLexems();
	size_t to = lex.size();
	while (to > 0 && lex[to - 1].m_lexType == ENDPROGRAM) --to;
	return ibBuildLambdaQueryAst(lex, 0, to, rowParam);
}

} // namespace

// A test holding the metadata-free source, the RAM fixture, and the SQLite twin side by side.
struct LinqExecFix : ::testing::Test {
	TestCol               region{ wxT("region"), REGION };
	TestCol               qty{ wxT("qty"), QTY };
	TestQueryable         src{ wxT("t"), 100 };
	ibQueryRamTable       ram = MakeRamFixture();
	ibDatabaseLayerSQLite db;

	void SetUp() override {
		ibCompileCode::SetCodeStyle(CODE_CES);   // the lambda bodies below are CES ('{ return … ; }')
		src.AddCol(&region);
		src.AddCol(&qty);
		ASSERT_TRUE(MakeSqlFixture(db)) << "SQLite in-memory fixture failed to open";
	}

	// The full L4-2 chain: lambda body text -> recorded AST -> lowered predicate (null on bail).
	ibQueryPredicatePtr Lower(const wxString& body, const std::map<wxString, ibValue>& captured = {}) {
		auto expr = Record(body);
		if (expr == nullptr) return nullptr;
		return ibQueryLowering::LowerLambdaPredicate(&src, *expr, captured);
	}
	int RamCount(const ibQueryPredicate* p) { return (int) ibQueryComposer::FilterRows(ram, p).RowCount(); }
};

// ---- executable parity: a lambda-lowered predicate == the SAME filter run as SQL ----

TEST_F(LinqExecFix, Eq_ParityWithSql)
{
	auto p = Lower(wxT("{ return x.region == \"North\"; }"));
	ASSERT_TRUE(p != nullptr) << "a translatable lambda body must lower to a predicate";
	EXPECT_EQ(RamCount(p.get()), SqlCount(db, wxT("region = 'North'")));   // 1 == 1
}

TEST_F(LinqExecFix, Or_ParityWithSql)
{
	auto p = Lower(wxT("{ return x.region == \"North\" Or x.region == \"South\"; }"));
	ASSERT_TRUE(p != nullptr);
	EXPECT_EQ(RamCount(p.get()),
	          SqlCount(db, wxT("region = 'North' OR region = 'South'")));   // 2 == 2
}

// THE LINQ TRAP through the LINQ front-end: `!=` over a NULL operand. SQL three-valued logic
// drops the NULL-region row (NULL <> 'North' is UNKNOWN); the lowered predicate + RAM core
// must agree. This is the executable proof the L4-2 path inherits Kleene NULL semantics.
TEST_F(LinqExecFix, NotEq_NullThreeValued_ParityWithSql)
{
	auto p = Lower(wxT("{ return x.region != \"North\"; }"));
	ASSERT_TRUE(p != nullptr);
	EXPECT_EQ(RamCount(p.get()), SqlCount(db, wxT("region <> 'North'")))   // South, East -> 2 == 2
		<< "L4-2 lowered `!=` must drop the NULL-region row (three-valued), like SQL.";
}

TEST_F(LinqExecFix, NumericGt_ParityWithSql)
{
	auto p = Lower(wxT("{ return x.qty > 6; }"));
	ASSERT_TRUE(p != nullptr);
	EXPECT_EQ(RamCount(p.get()), SqlCount(db, wxT("qty > 6")));   // North(10), East(7) -> 2 == 2
}

TEST_F(LinqExecFix, AndLogic_ParityWithSql)
{
	auto p = Lower(wxT("{ return x.qty >= 5 And x.region != \"South\"; }"));
	ASSERT_TRUE(p != nullptr);
	EXPECT_EQ(RamCount(p.get()),
	          SqlCount(db, wxT("qty >= 5 AND region <> 'South'")));   // North, East -> 2 == 2
}

// A captured outer local -> a Param node, resolved from the captured map (the &parameter analogy).
TEST_F(LinqExecFix, CapturedParam_ParityWithSql)
{
	std::map<wxString, ibValue> captured{ { wxT("minQty"), ibValue(ibNumber(6)) } };
	auto p = Lower(wxT("{ return x.qty > minQty; }"), captured);
	ASSERT_TRUE(p != nullptr) << "a captured identifier must lower via the captured map";
	EXPECT_EQ(RamCount(p.get()), SqlCount(db, wxT("qty > 6")));   // 2 == 2
}

// Bail at LOWERING (not recording): a well-formed AST naming a column the SOURCE cannot resolve.
// The recorder accepts the shape; ResolveColumnByName fails in lowering -> null -> the fold falls
// back to RAM. (Distinct from the recorder-level bails in test_lambdaRecorder.)
TEST_F(LinqExecFix, Bail_UnresolvableColumn)
{
	auto expr = Record(wxT("{ return x.nosuchcol > 1; }"));
	ASSERT_TRUE(expr != nullptr) << "the recorder accepts the shape; lowering is where an unknown column bails";
	EXPECT_TRUE(ibQueryLowering::LowerLambdaPredicate(&src, *expr, {}) == nullptr);
}
