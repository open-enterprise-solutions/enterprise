// =============================================================================
// Query PARITY harness — DB push-down vs RAM core, SAME fixture, SAME filter.
//
// The DB/RAM boundary is the classic LINQ trap: a predicate evaluated server-side
// (SQL) and the same predicate evaluated in the RAM core must give IDENTICAL
// results, or a report silently means two different things depending on where it
// ran. NULL / three-valued logic is the worst offender.
//
// This harness loads ONE fixture into both a real (in-memory) SQLite database and
// an ibQueryRamTable, then for each filter compares:
//   RAM  = ibQueryComposer::FilterRows(table, predicate).RowCount()
//   SQL  = SELECT COUNT(*) FROM t WHERE <equivalent clause>
//
// Every case here is LOCKED (regression guard). The former divergence —
// `NOT (region = 'North')`: SQL three-valued logic drops the NULL-region row, the RAM core used to
// keep it — is FIXED: the RAM evaluator adopted SQL NULL semantics (three-valued / Kleene), so
// NotEq_NullThreeValuedLogic and the IS NULL / AND / LIKE / aggregate cases all assert parity.
// A SQL NULL is modelled as ibValue(TYPE_NULL) (what the driver yields), NOT an unset/Undefined
// (TYPE_EMPTY) cell. See docs/query-language-arc.md and the form-attribute-binding / DynamicList
// note: the same settings feed both paths, so DB≡RAM parity is load-bearing for lists AND reports.
//
// Pure + self-contained: SQLite is always embedded; no appData session bring-up.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/compiler/value.h"          // ibValue / ibNumber
#include "backend/query/queryProvider.h"     // ibQueryComposer + ibQueryRamTable + BuildHierarchyTree
#include "backend/query/queryable.h"         // ibQueryPredicate / ibQueryCondition
#include "backend/query/queryColumn.h"       // ibBackendQueryColumn / ibTypeDescription
#include "backend/query/querySelector.h"     // ibSelectorTree / ibDimensionKind
#include "backend/query/dataQueryBuilder.h"  // ibDataQueryBuilder::AggregateItem / AggregateFn

#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"

namespace {

const ibTypeDescription kNoType;

// Minimal RAM-core column — name + model id (the row read key). No metaobject.
// (Mirrors TestCol in test_queryComposer.cpp / test_queryTotals.cpp.)
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

// The shared fixture: regions { North, South, East, <NULL> } with quantities.
// One NULL-region row is the whole point — it is where three-valued logic bites.
const ibMetaID REGION = 1, QTY = 2;

ibQueryRamTable MakeRamFixture()
{
	ibQueryRamTable t;
	t.AddColumn(REGION, wxT("region"), kNoType);
	t.AddColumn(QTY,    wxT("qty"),    kNoType);
	auto row = [&](const wxString& reg, long q, bool regNull) {
		const long r = t.AppendRow();
		// A DB NULL column materialises as ibValue(TYPE_NULL) (the driver) — model that, not an
		// unset/Undefined (TYPE_EMPTY) cell, which is a runtime placeholder and not a SQL null.
		t.SetCell(r, REGION, regNull ? ibValue(ibValueTypes::TYPE_NULL) : ibValue(reg));
		t.SetCell(r, QTY, ibValue(ibNumber(q)));
	};
	row(wxT("North"), 10, false);
	row(wxT("South"),  5, false);
	row(wxT("East"),   7, false);
	row(wxString(),    3, true);   // region IS NULL
	return t;
}

// The SAME fixture in a fresh in-memory SQLite.
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
	// Pass the query as a %s ARGUMENT, not as the format string itself — a literal '%'
	// (e.g. LIKE 'Nor%') would otherwise be read as a printf format specifier.
	ibDatabaseResultSet* rs = db.RunQueryWithResults(wxT("%s"), q);
	if (rs == nullptr)
		return -1;
	const int n = rs->Next() ? rs->GetResultInt(wxT("cnt")) : -1;
	db.CloseResultSet(rs);
	return n;
}

int RamCount(const ibQueryRamTable& t, const ibQueryPredicate* pred)
{
	return (int) ibQueryComposer::FilterRows(t, pred).RowCount();
}

ibQueryPredicatePtr Eq(const ibBackendQueryColumn* c, const wxString& v)
{
	ibQueryCondition cond;   // no explicit op = equality
	cond.m_col = c;
	cond.m_value = ibValue(v);
	return ibQueryPredicate::Leaf(cond);
}

ibQueryPredicatePtr Ne(const ibBackendQueryColumn* c, const wxString& v)
{
	ibQueryCondition cond;   // `<>` via the comparison flag (the m_comparison leaf path)
	cond.m_col = c;
	cond.m_value = ibValue(v);
	cond.m_comparison = ibComparisonType_NotEqual;
	return ibQueryPredicate::Leaf(cond);
}

ibQueryPredicatePtr Like(const ibBackendQueryColumn* c, const wxString& pat)
{
	ibQueryCondition cond;   // explicit LIKE op
	cond.m_col = c;
	cond.m_explicitOp = true;
	cond.m_op = ibQueryFilterOp::Like;
	cond.m_value = ibValue(pat);
	return ibQueryPredicate::Leaf(cond);
}

// A test holding the two fixtures side by side.
struct ParityFix : ::testing::Test {
	TestCol               region{ wxT("region"), REGION };
	TestCol               qty{ wxT("qty"), QTY };
	ibQueryRamTable       ram = MakeRamFixture();
	ibDatabaseLayerSQLite db;
	void SetUp() override { ASSERT_TRUE(MakeSqlFixture(db)) << "SQLite in-memory fixture failed to open"; }
};

} // namespace

// Sanity: both fixtures loaded the same number of rows before any filter.
TEST_F(ParityFix, Fixtures_SameCardinality)
{
	EXPECT_EQ((int) ram.RowCount(), SqlCount(db, wxT("1=1")));   // 4 == 4
}

// ---- LOCKED parity invariants (these already agree; keep them agreeing) ------

TEST_F(ParityFix, Eq_Match)
{
	EXPECT_EQ(RamCount(ram, Eq(&region, wxT("North")).get()),
	          SqlCount(db, wxT("region = 'North'")));            // 1 == 1
}

TEST_F(ParityFix, Or_Match)
{
	const ibQueryPredicatePtr orPred = ibQueryPredicate::Compose(
		ibQueryPredicateKind::Or, Eq(&region, wxT("North")), Eq(&region, wxT("South")));
	EXPECT_EQ(RamCount(ram, orPred.get()),
	          SqlCount(db, wxT("region = 'North' OR region = 'South'")));   // 2 == 2
}

TEST_F(ParityFix, IsNull_Match)
{
	EXPECT_EQ(RamCount(ram, ibQueryPredicate::Null(&region, /*negated*/ false).get()),
	          SqlCount(db, wxT("region IS NULL")));              // 1 == 1
}

TEST_F(ParityFix, IsNotNull_Match)
{
	EXPECT_EQ(RamCount(ram, ibQueryPredicate::Null(&region, /*negated*/ true).get()),
	          SqlCount(db, wxT("region IS NOT NULL")));          // 3 == 3
}

// ---- three-valued NULL logic (was the divergence; now LOCKED) -----------------
//
// SQL `NOT (region = 'North')` : NULL = 'North' is UNKNOWN, NOT UNKNOWN is UNKNOWN
//   -> the NULL-region row is EXCLUDED -> 2 rows (South, East).
// The RAM filter core now evaluates predicates in SQL three-valued (Kleene) logic
// (ibQueryComposer / RamEvalPredicate), so it drops the NULL row too -> parity.
TEST_F(ParityFix, NotEq_NullThreeValuedLogic)
{
	const ibQueryPredicatePtr notNorth = ibQueryPredicate::Not(Eq(&region, wxT("North")));
	EXPECT_EQ(RamCount(ram, notNorth.get()),
	          SqlCount(db, wxT("NOT (region = 'North')")))
		<< "RAM and SQL must agree on NOT over a NULL operand (three-valued logic).";
}

// `<>` (the m_comparison leaf path) over a NULL operand: SQL drops the NULL row -> 2.
TEST_F(ParityFix, NotEqual_NullThreeValuedLogic)
{
	EXPECT_EQ(RamCount(ram, Ne(&region, wxT("North")).get()),
	          SqlCount(db, wxT("region <> 'North'")));          // South, East -> 2 == 2
}

// AND with a NULL operand: the NULL row is UNKNOWN in both sides -> dropped.
TEST_F(ParityFix, And_NullThreeValuedLogic)
{
	const ibQueryPredicatePtr andP = ibQueryPredicate::Compose(
		ibQueryPredicateKind::And, Eq(&region, wxT("North")), Ne(&region, wxT("South")));
	EXPECT_EQ(RamCount(ram, andP.get()),
	          SqlCount(db, wxT("region = 'North' AND region <> 'South'")));   // North -> 1 == 1
}

// LIKE over a NULL operand: UNKNOWN -> dropped, on both sides.
TEST_F(ParityFix, Like_NullDropped)
{
	EXPECT_EQ(RamCount(ram, Like(&region, wxT("Nor%")).get()),
	          SqlCount(db, wxT("region LIKE 'Nor%'")));          // North -> 1 == 1
}

// ---- aggregate NULL semantics: SQL ignores NULL operands ---------------------
//
// The RAM fold (ibQueryComposer / AggregateOne) used to count NULL rows — COUNT(col)
// = bucket size, AVG denominator inflated, MIN/MAX could pick the empty cell. SQL
// excludes NULL from every aggregate except COUNT(*). AVG is omitted here: exact
// decimal vs SQLite float differ in representation (a separate numeric-tolerance concern).
TEST(QueryParityAgg, AggregatesIgnoreNull)
{
	const ibMetaID KEY = 10, PARENT = 11, BONUS = 12;
	TestCol key(wxT("k"), KEY), parent(wxT("p"), PARENT), bonus(wxT("bonus"), BONUS);

	// 4 rows, one with a NULL bonus; parent left empty -> flat (all under the root).
	ibQueryRamTable ram;
	ram.AddColumn(KEY,    wxT("k"),     kNoType);
	ram.AddColumn(PARENT, wxT("p"),     kNoType);
	ram.AddColumn(BONUS,  wxT("bonus"), kNoType);
	auto row = [&](long k, long b, bool bNull) {
		const long r = ram.AppendRow();
		ram.SetCell(r, KEY, ibValue(ibNumber(k)));
		ram.SetCell(r, BONUS, bNull ? ibValue(ibValueTypes::TYPE_NULL) : ibValue(ibNumber(b)));   // SQL NULL bonus
	};
	row(1, 10, false); row(2, 0, true); row(3, 30, false); row(4, 40, false);

	ibDatabaseLayerSQLite db;
	ASSERT_TRUE(db.Open(wxT(":memory:")));
	db.RunQuery(wxT("CREATE TABLE a (bonus INTEGER)"));
	db.RunQuery(wxT("INSERT INTO a (bonus) VALUES (10)"));
	db.RunQuery(wxT("INSERT INTO a (bonus) VALUES (NULL)"));
	db.RunQuery(wxT("INSERT INTO a (bonus) VALUES (30)"));
	db.RunQuery(wxT("INSERT INTO a (bonus) VALUES (40)"));

	using Fn = ibDataQueryBuilder::AggregateFn;
	auto ramAgg = [&](Fn fn) -> long {
		ibDataQueryBuilder::AggregateItem a; a.m_fn = fn; a.m_col = &bonus; a.m_alias = wxT("agg");
		const ibSelectorTree tree = ibQueryComposer::BuildHierarchyTree(
			ram, &key, &parent, { a }, ibDimensionKind::Elements);
		return tree.Root().m_values.at(BONUS).GetInteger();   // grand aggregate rolls into the column id
	};
	auto sqlAgg = [&](const wxString& expr) -> long {
		ibDatabaseResultSet* rs = db.RunQueryWithResults(wxT("%s"), wxT("SELECT ") + expr + wxT(" AS v FROM a"));
		const long n = (rs && rs->Next()) ? rs->GetResultInt(wxT("v")) : -1;
		if (rs) db.CloseResultSet(rs);
		return n;
	};

	EXPECT_EQ(ramAgg(Fn::Sum),   sqlAgg(wxT("SUM(bonus)")));     // 80 == 80
	EXPECT_EQ(ramAgg(Fn::Count), sqlAgg(wxT("COUNT(bonus)")));   // 3 == 3 (NULL excluded)
	EXPECT_EQ(ramAgg(Fn::Min),   sqlAgg(wxT("MIN(bonus)")));     // 10 == 10
	EXPECT_EQ(ramAgg(Fn::Max),   sqlAgg(wxT("MAX(bonus)")));     // 40 == 40
}
