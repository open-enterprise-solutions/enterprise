// =============================================================================
// JOIN parity harness — RAM join core vs a live SQLite JOIN, SAME fixture.
//
// Companion to test_queryParity.cpp (which covers the WHERE / filter core). This
// one locks the JOIN semantics: ibQueryComposer::JoinRamTables (the pure RAM
// nested-loop join, no DB) must agree, row-for-row, with what a real SQLite
// engine returns for the equivalent `A JOIN B ON a.k = b.k`.
//
// Why it matters for the LINQ unification (docs/query-language-arc.md): a script
// `.Join()` on a Data.* queryable now lowers into the L3 door. When the door can
// co-locate, the JOIN runs server-side (SQL); when it can't, it falls back to
// JoinRamTables. Those two paths MUST mean the same thing, or a report silently
// changes with the plan. The classic offenders are encoded here:
//   * a NULL join key must NOT match (SQL: NULL = NULL is UNKNOWN) — the RAM core
//     was fixed to drop NULL keys (see project memory: JoinRamTables_NullKey…);
//   * multi-match (one left row, K right rows) must fan out to K result rows;
//   * INNER drops unmatched on both sides; LEFT keeps unmatched-left.
//
// Pure + self-contained: SQLite is always embedded; no appData / pool / holder
// bring-up. The RAM core is reached directly; SQLite is the oracle. (A full
// server-side builder.Execute() join needs metadata-backed queryables — real
// column ids + type descriptors — which is integration scope, not here.)
// =============================================================================

#include <gtest/gtest.h>

#include "backend/compiler/value.h"          // ibValue / ibNumber
#include "backend/query/queryProvider.h"     // ibQueryComposer + ibQueryRamTable
#include "backend/query/queryable.h"         // ibBackendQueryColumn
#include "backend/query/queryColumn.h"       // ibTypeDescription
#include "backend/query/dataQueryBuilder.h"  // ibQueryJoinKind

#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"

namespace {

const ibTypeDescription kNoType;

// Minimal RAM-core column — name + model id (the row read key). No metaobject.
// (Mirrors TestCol in test_queryParity.cpp / test_queryComposer.cpp.)
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

// Distinct column ids — JoinRamTables keys output cells by id, so the two sides
// must not collide (this is exactly why raw columns, all id 0, cannot drive the
// projected join read; metadata columns carry real ids).
const ibMetaID CUST_ID = 1, CUST_NAME = 2, SALE_CUSTID = 3, SALE_AMT = 4;

// left = customers { (1,Alice), (2,Bob), (3,Carol) }
ibQueryRamTable MakeCustRam()
{
	ibQueryRamTable t;
	t.AddColumn(CUST_ID,   wxT("id"),   kNoType);
	t.AddColumn(CUST_NAME, wxT("name"), kNoType);
	auto row = [&](long id, const wxString& name) {
		const long r = t.AppendRow();
		t.SetCell(r, CUST_ID,   ibValue(ibNumber(id)));
		t.SetCell(r, CUST_NAME, ibValue(name));
	};
	row(1, wxT("Alice"));
	row(2, wxT("Bob"));
	row(3, wxT("Carol"));   // no sale -> unmatched-left
	return t;
}

// right = sales { (1,100), (1,300), (2,200), (9,50), (NULL,70) }
//   custid 1 -> two rows (multi-match); 9 -> no customer; NULL -> must not match.
ibQueryRamTable MakeSaleRam()
{
	ibQueryRamTable t;
	t.AddColumn(SALE_CUSTID, wxT("custid"), kNoType);
	t.AddColumn(SALE_AMT,    wxT("amount"), kNoType);
	auto row = [&](long custid, long amount, bool custNull) {
		const long r = t.AppendRow();
		t.SetCell(r, SALE_CUSTID, custNull ? ibValue(ibValueTypes::TYPE_NULL) : ibValue(ibNumber(custid)));
		t.SetCell(r, SALE_AMT,    ibValue(ibNumber(amount)));
	};
	row(1, 100, false);
	row(1, 300, false);   // multi-match with customer 1
	row(2, 200, false);
	row(9,  50, false);   // no matching customer
	row(0,  70, true);    // NULL custid -> must NOT join to anything
	return t;
}

// The SAME fixture in a fresh in-memory SQLite.
bool MakeSqlFixture(ibDatabaseLayerSQLite& db)
{
	if (!db.Open(wxT(":memory:")))
		return false;
	db.RunQuery(wxT("CREATE TABLE cust (id INTEGER, name TEXT)"));
	db.RunQuery(wxT("INSERT INTO cust (id, name) VALUES (1, 'Alice')"));
	db.RunQuery(wxT("INSERT INTO cust (id, name) VALUES (2, 'Bob')"));
	db.RunQuery(wxT("INSERT INTO cust (id, name) VALUES (3, 'Carol')"));
	db.RunQuery(wxT("CREATE TABLE sale (custid INTEGER, amount INTEGER)"));
	db.RunQuery(wxT("INSERT INTO sale (custid, amount) VALUES (1, 100)"));
	db.RunQuery(wxT("INSERT INTO sale (custid, amount) VALUES (1, 300)"));
	db.RunQuery(wxT("INSERT INTO sale (custid, amount) VALUES (2, 200)"));
	db.RunQuery(wxT("INSERT INTO sale (custid, amount) VALUES (9, 50)"));
	db.RunQuery(wxT("INSERT INTO sale (custid, amount) VALUES (NULL, 70)"));
	return true;
}

int SqlCount(ibDatabaseLayerSQLite& db, const wxString& fromJoin)
{
	const wxString q = wxT("SELECT COUNT(*) AS cnt FROM ") + fromJoin;
	ibDatabaseResultSet* rs = db.RunQueryWithResults(wxT("%s"), q);   // query as %s arg, not format string
	if (rs == nullptr)
		return -1;
	const int n = rs->Next() ? rs->GetResultInt(wxT("cnt")) : -1;
	db.CloseResultSet(rs);
	return n;
}

// Run the RAM join core and return its row count. outCols span both sides so the
// result is the full joined row (id, name, amount); fromLeft tags each.
int RamJoinCount(const ibQueryRamTable& cust, const ibQueryRamTable& sale,
                 const TestCol& custId, const TestCol& saleCustId,
                 const TestCol& custName, const TestCol& saleAmt,
                 ibQueryJoinKind kind)
{
	const std::vector<const ibBackendQueryColumn*> outCols = { &custId, &custName, &saleAmt };
	const std::vector<bool>                        fromLeft = { true,    true,      false   };
	return (int) ibQueryComposer::JoinRamTables(cust, sale, &custId, &saleCustId, outCols, fromLeft, kind).RowCount();
}

struct JoinParityFix : ::testing::Test {
	TestCol               custId{ wxT("id"),     CUST_ID };
	TestCol               custName{ wxT("name"), CUST_NAME };
	TestCol               saleCustId{ wxT("custid"), SALE_CUSTID };
	TestCol               saleAmt{ wxT("amount"),    SALE_AMT };
	ibQueryRamTable       cust = MakeCustRam();
	ibQueryRamTable       sale = MakeSaleRam();
	ibDatabaseLayerSQLite db;
	void SetUp() override { ASSERT_TRUE(MakeSqlFixture(db)) << "SQLite in-memory fixture failed to open"; }
};

} // namespace

// Sanity: both fixtures hold the same cardinality before any join.
TEST_F(JoinParityFix, Fixtures_SameCardinality)
{
	EXPECT_EQ((int) cust.RowCount(), SqlCount(db, wxT("cust")));   // 3
	EXPECT_EQ((int) sale.RowCount(), SqlCount(db, wxT("sale")));   // 5
}

// INNER JOIN: multi-match fans out (customer 1 -> 2 rows), unmatched on BOTH
// sides drop (Carol, sale custid 9), and the NULL-custid sale must NOT match.
// Expect: Alice×{100,300} + Bob×{200} = 3.
TEST_F(JoinParityFix, InnerJoin_MultiMatch_DropsUnmatchedAndNullKey)
{
	const int sql = SqlCount(db, wxT("cust c JOIN sale s ON c.id = s.custid"));
	const int ram = RamJoinCount(cust, sale, custId, saleCustId, custName, saleAmt, ibQueryJoinKind::Inner);
	EXPECT_EQ(ram, sql) << "RAM inner join must match SQLite (multi-match + NULL-key non-match).";
	EXPECT_EQ(ram, 3);
}

// The NULL join key in isolation: a sale with NULL custid must contribute ZERO
// inner-join rows (SQL: NULL = anything is UNKNOWN). This is the divergence the
// RAM core was fixed for — guard it directly.
TEST_F(JoinParityFix, InnerJoin_NullKeyContributesNoRow)
{
	// Same count whether or not the NULL row is present -> it never joined.
	EXPECT_EQ(RamJoinCount(cust, sale, custId, saleCustId, custName, saleAmt, ibQueryJoinKind::Inner),
	          SqlCount(db, wxT("cust c JOIN sale s ON c.id = s.custid AND s.custid IS NOT NULL")));
}

// LEFT JOIN: keeps unmatched-left (Carol, one row with NULL right cells), still
// fans out the multi-match. Expect: Alice×2 + Bob×1 + Carol×1 = 4.
TEST_F(JoinParityFix, LeftJoin_KeepsUnmatchedLeft)
{
	const int sql = SqlCount(db, wxT("cust c LEFT JOIN sale s ON c.id = s.custid"));
	const int ram = RamJoinCount(cust, sale, custId, saleCustId, custName, saleAmt, ibQueryJoinKind::Left);
	EXPECT_EQ(ram, sql) << "RAM left join must match SQLite (unmatched-left kept).";
	EXPECT_EQ(ram, 4);
}

// ---- SQLite temp-table capability (the heterogeneous-join promote enabler) ---
//
// The SQLite driver now vends an ibTempTableDialect, which flips it OFF the RAM
// floor: a heterogeneous JOIN (a computed leaf ⋈ a DB leaf) can materialise the
// computed side into a server-side temp table on the embedded DB and run the
// join DB⋈DB (queryProvider.cpp PromoteComputedLeaf), instead of always
// stitching in RAM. Presence IS the capability (docs/temp-db.md). This locks the
// capability + its shape WITHOUT a pool; the full CREATE+INSERT+read round-trip
// is integration scope (it needs a holder/connection) -> oes_temp_db_sqlite_test.
TEST(SqliteTempDialect, PresentAdHocCreateExplicitDrop)
{
	ibDatabaseLayerSQLite db2;
	const ibTempTableDialect* d = db2.GetTempTableDialect();
	ASSERT_NE(d, nullptr) << "SQLite must vend a temp-table dialect (presence = capability)";
	EXPECT_EQ(d->m_strategy, ibTempTableDialect::Strategy::AdHocCreate);
	EXPECT_FALSE(d->m_autoDrops);                 // explicit DROP via the manager's pinning scope
	EXPECT_FALSE(d->m_createPrefix.IsEmpty());    // "CREATE TEMPORARY TABLE …"
}
