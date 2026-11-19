// =============================================================================
// L4-2 — LINQ push-down EXECUTION parity. The gap the recorder / dispatch tests leave.
//
// The recorder tests (test_lambdaRecorder) stop at the AST SHAPE; the method-table
// tests (test_LINQMethod) stop at name -> enum dispatch. NEITHER runs the lowered
// predicate against data. This harness closes the loop end to end:
//
//   lambda body text --(compile + ibBuildLambdaQueryAstFromCode)--> ibQueryAstExpr [L4-2 recorder]
//                    --(ibQueryLowering::LowerLambdaPredicate)--> ibQueryPredicate [L4-2 lowering]
//                    --(ibDbTableProvider::BuildPredicateIR)----> rows of a base in memory [L3 -> L2]
//
// and asserts the row count equals the SAME filter as SQL against an in-memory SQLite —
// the LINQ trap through the LINQ front-end: a predicate LOWERED FROM A LAMBDA must mean
// the SAME thing on every base, NULL / three-valued (Kleene) logic included (memoryParityFix.h).
// Plus the bail path: an untranslatable-at-LOWERING body (an unresolvable column) yields
// null -> the Queryable fold falls back to RAM, never a thrown user error.
//
// Pure + session-free: LowerLambdaPredicate takes the source AS AN ARGUMENT (no
// activeMetaData), so a metadata-free source drives it — exactly what
// test_queryComposer proves a queryable may be. (L4-1 text Execute() resolves names
// against activeMetaData and is NOT reachable here — L4-2 is where the executable-parity
// test lives. See docs/private/query-engine-layers.md §L4, docs/private/query-language-arc.md §23.5.)
// =============================================================================

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include <algorithm>   // std::max — the depth of a folded IN

#include <map>
#include <memory>
#include <vector>

#include "backend/compiler/value.h"           // ibValue / ibNumber
#include "backend/compiler/compileCode.h"     // ibCompileCode — the script lexer feeding the recorder
#include "backend/compiler/lambdaQueryAST.h"  // ibBuildLambdaQueryAstFromCode (L4-2 recorder)
#include "core/query/queryAST.h"           // ibQueryAstExpr

#include "lambdaRecordFix.h"                      // ibTestRecordLambda — body -> compiled lambda -> AST
#include "memoryParityFix.h"                      // ibTestMemoryRegions — the source and the base in memory
#include "backend/query/queryLowering.h"          // ibQueryLowering::LowerLambdaPredicate (L4-2 lowering)
#include "backend/query/queryable.h"              // ibBackendQueryable / ibQueryPredicate(Ptr)

#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"

namespace {

// The shared fixture: region { North, South, East, <NULL> } with qty — same as test_queryParity.
// The NULL-region row is the whole point: it is where three-valued logic bites, now reached
// through the LINQ front-end instead of a hand-built predicate.
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

int SqlCount(ibDatabaseLayerSQLite& db, const ibString& where)
{
	const ibString q = wxT("SELECT COUNT(*) AS cnt FROM t WHERE ") + where;
	// Pass the query as a %s ARGUMENT, not the format string (a literal '%' in a LIKE would fuse).
	ibDatabaseResultSet* rs = db.RunQueryWithResults(wxT("%s"), q);
	if (rs == nullptr)
		return -1;
	const int n = rs->Next() ? rs->GetResultInt(wxT("cnt")) : -1;
	db.CloseResultSet(rs);
	return n;
}

// Record a lambda BODY into the L4-2 query AST (as test_lambdaRecorder does): the body is compiled
// as a real lambda and the recorder reads its instructions — no metadata, no database.
std::shared_ptr<ibQueryAstExpr> Record(const ibString& body, const ibString& rowParam = wxT("x"),
	const ibString& outerNames = wxEmptyString)
{
	return ibTestRecordLambda(body, rowParam, outerNames);
}

} // namespace

// A test holding the metadata-free source, the base in memory, and the SQLite twin side by side.
struct LinqExecFix : ::testing::Test {
	ibTestCodeStyle       style{ CODE_CES };   // the lambda bodies below are CES ('{ return … ; }')
	ibTestMemoryRegions   memory;
	std::shared_ptr<const ibBackendQueryable> src = memory.m_table;
	ibDatabaseLayerSQLite db;

	void SetUp() override {
		ASSERT_TRUE(MakeSqlFixture(db)) << "SQLite in-memory fixture failed to open";
	}

	// The full L4-2 chain: lambda body text -> recorded AST -> lowered predicate (null on bail).
	ibQueryPredicatePtr Lower(const ibString& body, const std::map<ibString, ibValue>& captured = {}) {
		// The captured map's KEYS are the names the body reads from outside it, so they are also
		// exactly what has to be declared around the lambda for it to compile at all. One list,
		// used for both — a second one written by hand would drift from this the first time a test
		// captured something new.
		ibString outerNames;
		for (const auto& one : captured)
			outerNames << (outerNames.IsEmpty() ? wxT("") : wxT(", ")) << one.first;

		auto expr = Record(body, wxT("x"), outerNames);
		if (expr == nullptr) return nullptr;
		return ibQueryLowering::LowerLambdaPredicate(src, *expr, captured);
	}
	int MemoryCount(const ibQueryPredicatePtr& p) { return memory.Count(p); }
};

// ---- executable parity: a lambda-lowered predicate == the SAME filter run as SQL ----

TEST_F(LinqExecFix, Eq_ParityWithSql)
{
	// The bodies are CES, so they compare with `==` / `!=` and join with `&&` / `||` - the dialect's own
	// spellings, which its compiler holds them to (the note on Record in test_lambdaRecorder.cpp).
	auto p = Lower(wxT("{ return x.region == \"North\"; }"));
	ASSERT_TRUE(p != nullptr) << "a translatable lambda body must lower to a predicate";
	EXPECT_EQ(MemoryCount(p), SqlCount(db, wxT("region = 'North'")));   // 1 == 1
}

TEST_F(LinqExecFix, Or_ParityWithSql)
{
	auto p = Lower(wxT("{ return x.region == \"North\" || x.region == \"South\"; }"));
	ASSERT_TRUE(p != nullptr);
	EXPECT_EQ(MemoryCount(p),
	          SqlCount(db, wxT("region = 'North' OR region = 'South'")));   // 2 == 2
}

// THE LINQ TRAP through the LINQ front-end: `<>` over a NULL operand. SQL three-valued logic
// drops the NULL-region row (NULL <> 'North' is UNKNOWN); the lowered predicate + RAM core
// must agree. This is the executable proof the L4-2 path inherits Kleene NULL semantics.
TEST_F(LinqExecFix, NotEq_NullThreeValued_ParityWithSql)
{
	auto p = Lower(wxT("{ return x.region != \"North\"; }"));
	ASSERT_TRUE(p != nullptr);
	EXPECT_EQ(MemoryCount(p), SqlCount(db, wxT("region <> 'North'")))   // South, East -> 2
		<< "L4-2 lowered `<>` must drop the NULL-region row (three-valued), like SQL.";
}

TEST_F(LinqExecFix, NumericGt_ParityWithSql)
{
	auto p = Lower(wxT("{ return x.qty > 6; }"));
	ASSERT_TRUE(p != nullptr);
	EXPECT_EQ(MemoryCount(p), SqlCount(db, wxT("qty > 6")));   // North(10), East(7) -> 2 == 2
}

TEST_F(LinqExecFix, AndLogic_ParityWithSql)
{
	auto p = Lower(wxT("{ return x.qty >= 5 && x.region != \"South\"; }"));
	ASSERT_TRUE(p != nullptr);
	EXPECT_EQ(MemoryCount(p),
	          SqlCount(db, wxT("qty >= 5 AND region <> 'South'")));   // North, East -> 2 == 2
}

// A captured outer local -> a Param node, resolved from the captured map (the &parameter analogy).
TEST_F(LinqExecFix, CapturedParam_ParityWithSql)
{
	std::map<ibString, ibValue> captured{ { wxT("minQty"), ibValue(ibNumber(6)) } };
	auto p = Lower(wxT("{ return x.qty > minQty; }"), captured);
	ASSERT_TRUE(p != nullptr) << "a captured identifier must lower via the captured map";
	EXPECT_EQ(MemoryCount(p), SqlCount(db, wxT("qty > 6")));   // 2 == 2
}

// Bail at LOWERING (not recording): a well-formed AST naming a column the SOURCE cannot resolve.
// The recorder accepts the shape; ResolveColumnByName fails in lowering -> null -> the fold falls
// back to RAM. (Distinct from the recorder-level bails in test_lambdaRecorder.)
TEST_F(LinqExecFix, Bail_UnresolvableColumn)
{
	auto expr = Record(wxT("{ return x.nosuchcol > 1; }"));
	ASSERT_TRUE(expr != nullptr) << "the recorder accepts the shape; lowering is where an unknown column bails";
	EXPECT_TRUE(ibQueryLowering::LowerLambdaPredicate(src, *expr, {}) == nullptr);
}

// =============================================================================
// `col IN (list)` — the SHAPE the lowering gives it, and that the shape changes nothing a row can see.
//
// A list used to fold as `acc = Or(acc, eq)`: a tree as DEEP as the list is long, walked recursively by
// everything downstream. A query with an array of some 155 references in `IN (&Items)` ran a checked
// build out of stack and took the client down with 0xc0000005 and no dump (measured 2026-09-19 on
// Firebird; reproducible on an empty catalog). The AST is built by hand here because a lambda body has
// no spelling for a thousand literals.
// =============================================================================
namespace {

std::shared_ptr<ibQueryAstExpr> InList(const ibString& column, const std::vector<ibVariant>& values, bool negated = false)
{
	auto in = ibQueryAstExpr::Make(ibQueryAstExprKind::In);
	in->m_negated = negated;
	in->m_lhs = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
	in->m_lhs->m_path = { column };
	for (const ibVariant& v : values) {
		auto literal = ibQueryAstExpr::Make(ibQueryAstExprKind::Literal);
		literal->m_literal = v;
		in->m_list.push_back(literal);
	}
	return in;
}

int DepthOf(const ibQueryPredicatePtr& p)
{
	if (!p) return 0;
	int deepest = 0;
	for (const ibQueryPredicatePtr& child : p->m_children)
		deepest = std::max(deepest, DepthOf(child));
	return deepest + 1;
}

} // namespace

// A plain list over one column travels as ONE set-valued leaf — the `In` the door already renders as the
// engine's own IN — however long it is.
TEST_F(LinqExecFix, In_AThousandValuesAreOneLeaf)
{
	std::vector<ibVariant> values{ ibVariant(ibT("North")), ibVariant(ibT("East")) };
	for (int i = 0; i < 1000; ++i)
		values.push_back(ibVariant(ibString::Format(wxT("Nowhere%d"), i)));

	const auto p = ibQueryLowering::LowerLambdaPredicate(src, *InList(wxT("region"), values), {});
	ASSERT_TRUE(p != nullptr);
	EXPECT_EQ(p->m_kind, ibQueryPredicateKind::Leaf);
	EXPECT_EQ(p->m_leaf.m_op, ibQueryFilterOp::In);
	EXPECT_EQ(p->m_leaf.m_values.size(), values.size());
	EXPECT_EQ(MemoryCount(p), SqlCount(db, wxT("region IN ('North', 'East')")));   // 2 == 2, the NULL row excluded
}

// NOT IN is the same leaf under a Not — and keeps SQL's three-valued answer for the NULL row.
TEST_F(LinqExecFix, NotIn_ParityWithSql)
{
	const auto p = ibQueryLowering::LowerLambdaPredicate(src,
		*InList(wxT("region"), { ibVariant(ibT("North")), ibVariant(ibT("East")) }, /*negated*/ true), {});
	ASSERT_TRUE(p != nullptr);
	EXPECT_EQ(p->m_kind, ibQueryPredicateKind::Not);
	EXPECT_EQ(MemoryCount(p), SqlCount(db, wxT("region NOT IN ('North', 'East')")));   // South alone
}

// One value is an equality, as it always was — nothing that rides an Equal leaf loses it to a set of one.
TEST_F(LinqExecFix, In_ASingleValueStaysAnEquality)
{
	const auto p = ibQueryLowering::LowerLambdaPredicate(src, *InList(wxT("region"), { ibVariant(ibT("North")) }), {});
	ASSERT_TRUE(p != nullptr);
	ASSERT_EQ(p->m_kind, ibQueryPredicateKind::Leaf);
	EXPECT_EQ(p->m_leaf.m_op, ibQueryFilterOp::Equal);
	EXPECT_EQ(MemoryCount(p), 1);
}

// 🛑 A list that cannot be one leaf (a NULL among the values) still folds pair by pair — as a BALANCED
// tree. A thousand values are some ten levels deep, not a thousand: this is the test that fails if the
// chain comes back.
TEST_F(LinqExecFix, In_APairwiseFoldIsBalancedNotChained)
{
	std::vector<ibVariant> values{ ibVariant(ibT("North")), ibVariant(ibValueTypes::TYPE_NULL) };
	for (int i = 0; i < 1000; ++i)
		values.push_back(ibVariant(ibString::Format(wxT("Nowhere%d"), i)));

	const auto p = ibQueryLowering::LowerLambdaPredicate(src, *InList(wxT("region"), values), {});
	ASSERT_TRUE(p != nullptr);
	EXPECT_EQ(p->m_kind, ibQueryPredicateKind::Or);
	EXPECT_LE(DepthOf(p), 12) << "an OR over " << values.size() << " values must be ~log2(N) deep";
	EXPECT_EQ(MemoryCount(p), 1);   // North; a NULL in the list matches nothing
}
