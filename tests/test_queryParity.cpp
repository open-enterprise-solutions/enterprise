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
// Cases that already agree are LOCKED (regression guard). The known divergence —
// `NOT (region = 'North')`: SQL three-valued logic drops the NULL-region row, the
// RAM core keeps it — is a DISABLED_ parity test that documents the gap. Enable it
// (drop the DISABLED_ prefix, run) to drive the fix: the RAM evaluator of a pushable
// predicate must adopt SQL NULL semantics. See docs/query-language-arc.md (§22) and
// the form-attribute-binding / DynamicList note: the same settings feed both paths,
// so DB≡RAM parity is load-bearing for lists AND reports.
//
// Pure + self-contained: SQLite is always embedded; no appData session bring-up.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/compiler/value.h"          // ibValue / ibNumber
#include "backend/query/queryProvider.h"     // ibQueryComposer + ibQueryRamTable
#include "backend/query/queryable.h"         // ibQueryPredicate / ibQueryCondition
#include "backend/query/queryColumn.h"       // ibBackendQueryColumn / ibTypeDescription

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
		if (!regNull) t.SetCell(r, REGION, ibValue(reg));
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
