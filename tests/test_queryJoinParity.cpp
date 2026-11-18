// =============================================================================
// JOIN parity harness — the base in memory vs a live SQLite JOIN, SAME fixture.
//
// Companion to test_queryParity.cpp (which covers the WHERE / filter). This one
// locks the JOIN semantics: the interpreter of a base in memory (databaseQueryInterpreter.h)
// must agree, row-for-row, with what a real SQLite engine returns for the equivalent
// `A JOIN B ON a.k = b.k`.
//
// Why it matters (docs/private/query-one-road.md §7): a join whose sources the
// base cannot write into one statement — a computed one, another connection's — is
// read in a base in memory by the statement the server would have been given. Those
// two MUST mean the same thing, or a report silently changes with the plan. The
// classic offenders are encoded here:
//   * a NULL join key must NOT match (SQL: NULL = NULL is UNKNOWN);
//   * multi-match (one left row, K right rows) must fan out to K result rows;
//   * INNER drops unmatched on both sides; LEFT keeps unmatched-left.
// (The rows of a join were stitched in memory by a core of the composer's own,
// JoinRamTables, until the road it served was deleted — P5.)
//
// Pure + self-contained: SQLite is always embedded, the base in memory sits behind
// a pool and a holder of its own; SQLite is the oracle.
// =============================================================================

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include "backend/compiler/value.h"               // ibValue / ibNumber
#include "backend/databaseLayer/connectionHolder.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/databaseMemoryBase.h"

#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"

namespace {

// customers { (1,Alice), (2,Bob), (3,Carol) } and sales { (1,100), (1,300), (2,200), (9,50), (NULL,70) }:
// custid 1 -> two rows (multi-match); 9 -> no customer; NULL -> must not match; Carol -> unmatched-left.
struct MemoryJoinBase
{
	ibConnectionPool         m_pool{ ib::AppDataCtorToken{} };
	ibSingleConnectionHolder m_holder;

	MemoryJoinBase()
	{
		m_pool.Init(std::make_shared<ibDatabaseLayerMemory>(), /*maxSize=*/1, /*minIdle=*/0);
		m_holder.SetPool(&m_pool);
		const auto field = [](const ibString& name, const ibColumnType& type) {
			ibDdlColumn c;
			c.m_name = name;
			c.m_type = type;
			return c;
		};
		const auto number = [](int n) { return ibConst(ibValue(ibNumber(n))); };
		const auto text   = [](const ibString& s) { return ibConst(ibValue(s)); };

		ibDatabaseQueryBuilder(&m_holder).Execute(ibCreateTable(ibT("cust"), { field(ibT("id"), ibTypeNumber(10, 0)), field(ibT("name"), ibTypeString(20)) }));
		ibDmlStatement cust = ibInsert(ibT("cust"), { ibDmlAssign{ ibT("id"), number(1) }, ibDmlAssign{ ibT("name"), text(ibT("Alice")) } });
		cust.m_extraRows = { { number(2), text(ibT("Bob")) }, { number(3), text(ibT("Carol")) } };
		ibDatabaseQueryBuilder(&m_holder).Execute(cust);

		ibDatabaseQueryBuilder(&m_holder).Execute(ibCreateTable(ibT("sale"), { field(ibT("custid"), ibTypeNumber(10, 0)), field(ibT("amount"), ibTypeNumber(10, 0)) }));
		ibDmlStatement sale = ibInsert(ibT("sale"), { ibDmlAssign{ ibT("custid"), number(1) }, ibDmlAssign{ ibT("amount"), number(100) } });
		sale.m_extraRows = {
			{ number(1), number(300) },
			{ number(2), number(200) },
			{ number(9), number(50) },
			{ ibConst(ibValue()), number(70) },   // NULL custid — an empty value binds as NULL
		};
		ibDatabaseQueryBuilder(&m_holder).Execute(sale);
	}

	int Count(ibQueryRelPtr from)
	{
		ibDatabaseQueryBuilder q(&m_holder);
		ibQueryResult rs = q.ExecuteIR(ibQueryIR(ibAggregate(std::move(from), { ibQueryProjItem{ ibFunc(ibT("COUNT")), ibT("cnt") } }, {})));
		return rs.Next() ? rs.GetResultInt(1) : -1;
	}

	// cust c <type> JOIN sale s ON <on>
	int JoinCount(ibQueryExprPtr on, ibQueryJoinType type)
	{
		return Count(ibJoin(ibScan(ibT("cust"), ibT("c")), ibScan(ibT("sale"), ibT("s")), std::move(on), type));
	}

	// …ON c.id = s.custid
	int JoinCount(ibQueryJoinType type)
	{
		return JoinCount(ibBinOp(ibQueryBinOp::Eq, ibCol(ibT("c"), ibT("id")), ibCol(ibT("s"), ibT("custid"))), type);
	}
};

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

int SqlCount(ibDatabaseLayerSQLite& db, const ibString& fromJoin)
{
	const ibString q = wxT("SELECT COUNT(*) AS cnt FROM ") + fromJoin;
	ibDatabaseResultSet* rs = db.RunQueryWithResults(wxT("%s"), q);   // query as %s arg, not format string
	if (rs == nullptr)
		return -1;
	const int n = rs->Next() ? rs->GetResultInt(wxT("cnt")) : -1;
	db.CloseResultSet(rs);
	return n;
}

struct JoinParityFix : ::testing::Test {
	MemoryJoinBase        memory;
	ibDatabaseLayerSQLite db;
	void SetUp() override { ASSERT_TRUE(MakeSqlFixture(db)) << "SQLite in-memory fixture failed to open"; }
};

} // namespace

// Sanity: both fixtures hold the same cardinality before any join.
TEST_F(JoinParityFix, Fixtures_SameCardinality)
{
	EXPECT_EQ(memory.Count(ibScan(ibT("cust"))), SqlCount(db, wxT("cust")));   // 3
	EXPECT_EQ(memory.Count(ibScan(ibT("sale"))), SqlCount(db, wxT("sale")));   // 5
}

// INNER JOIN: multi-match fans out (customer 1 -> 2 rows), unmatched on BOTH
// sides drop (Carol, sale custid 9), and the NULL-custid sale must NOT match.
// Expect: Alice×{100,300} + Bob×{200} = 3.
TEST_F(JoinParityFix, InnerJoin_MultiMatch_DropsUnmatchedAndNullKey)
{
	const int sql = SqlCount(db, wxT("cust c JOIN sale s ON c.id = s.custid"));
	const int mem = memory.JoinCount(ibQueryJoinType::Inner);
	EXPECT_EQ(mem, sql) << "the base in memory must join as SQLite does (multi-match + NULL-key non-match).";
	EXPECT_EQ(mem, 3);
}

// The NULL join key in isolation: a sale with NULL custid must contribute ZERO
// inner-join rows (SQL: NULL = anything is UNKNOWN) — guarded directly.
TEST_F(JoinParityFix, InnerJoin_NullKeyContributesNoRow)
{
	// Same count whether or not the NULL row is present -> it never joined.
	EXPECT_EQ(memory.JoinCount(ibQueryJoinType::Inner),
	          SqlCount(db, wxT("cust c JOIN sale s ON c.id = s.custid AND s.custid IS NOT NULL")));
}

// LEFT JOIN: keeps unmatched-left (Carol, one row with NULL right cells), still
// fans out the multi-match. Expect: Alice×2 + Bob×1 + Carol×1 = 4.
TEST_F(JoinParityFix, LeftJoin_KeepsUnmatchedLeft)
{
	const int sql = SqlCount(db, wxT("cust c LEFT JOIN sale s ON c.id = s.custid"));
	const int mem = memory.JoinCount(ibQueryJoinType::Left);
	EXPECT_EQ(mem, sql) << "the base in memory must keep the unmatched left as SQLite does.";
	EXPECT_EQ(mem, 4);
}

// RIGHT keeps the unmatched sales (9, NULL); FULL keeps the unmatched of both sides.
// (SQLite before 3.39 has neither, so the oracle of RIGHT is the mirrored LEFT; of FULL, the count.)
TEST_F(JoinParityFix, RightAndFullJoin_KeepTheirUnmatched)
{
	EXPECT_EQ(memory.JoinCount(ibQueryJoinType::Right), SqlCount(db, wxT("sale s LEFT JOIN cust c ON c.id = s.custid")));   // 5
	EXPECT_EQ(memory.JoinCount(ibQueryJoinType::Full), 6);   // 3 matched + Carol + sale 9 + the NULL sale
}

// An ON with no equality of a field to a field has no key: every pair is met (1 < 2, 1 < 9, 2 < 9, 3 < 9).
TEST_F(JoinParityFix, ThetaJoin_MeetsEveryPair)
{
	const int sql = SqlCount(db, wxT("cust c JOIN sale s ON c.id < s.custid"));
	EXPECT_EQ(memory.JoinCount(ibBinOp(ibQueryBinOp::Lt, ibCol(ibT("c"), ibT("id")), ibCol(ibT("s"), ibT("custid"))),
		ibQueryJoinType::Inner), sql);
	EXPECT_EQ(sql, 4);
}

// The key is the equality, the rest of the ON is asked of each pair it meets — written either way round.
TEST_F(JoinParityFix, KeyAndACondition_TheRestAskedOfEachPair)
{
	const auto on = [] {
		return ibBinOp(ibQueryBinOp::And,
			ibBinOp(ibQueryBinOp::Eq, ibCol(ibT("s"), ibT("custid")), ibCol(ibT("c"), ibT("id"))),   // the right side first
			ibBinOp(ibQueryBinOp::Gt, ibCol(ibT("s"), ibT("amount")), ibConst(ibValue(ibNumber(150)))));
	};
	EXPECT_EQ(memory.JoinCount(on(), ibQueryJoinType::Inner),
	          SqlCount(db, wxT("cust c JOIN sale s ON s.custid = c.id AND s.amount > 150")));        // Alice 300, Bob 200
	EXPECT_EQ(memory.JoinCount(on(), ibQueryJoinType::Left),
	          SqlCount(db, wxT("cust c LEFT JOIN sale s ON s.custid = c.id AND s.amount > 150")));   // …and Carol alone
	EXPECT_EQ(memory.JoinCount(on(), ibQueryJoinType::Left), 3);
}

// ---- SQLite temp-table capability -----------------------------------------------
//
// The SQLite driver vends an ibTempTableDialect: rows can be written into a temporary
// table of the embedded base (ibTempTableManager::Materialise — a nested query's keys
// for a semi-join). Presence IS the capability (docs/private/temp-db.md). This locks the
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
