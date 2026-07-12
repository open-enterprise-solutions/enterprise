// =============================================================================
// Temp-DB round-trip on the embedded SQLite driver (integration scope).
//
// The SQLite driver now vends an ibTempTableDialect (ad-hoc CREATE TEMPORARY
// TABLE), so ibTempTableManager::Materialise can serialise a RAM intermediate
// into a server-side temp table on the embedded DB. That is the capability that
// lets a heterogeneous JOIN (a computed leaf ⋈ a DB leaf) promote OFF the RAM
// floor and run DB⋈DB server-side (queryProvider.cpp PromoteComputedLeaf) — the
// optimisation the LINQ-join unification leans on once a side is computed. This
// proves the machinery end-to-end: CREATE TEMP + chunked INSERT + read-back
// through the L3 door.
//
// Unlike the pure parity tests, this one DOES bring up the global appData env
// (the way codeRunner does — ibApplicationData::CreateAppDataEnv) plus a
// SQLite-only pool, because the temp manager pins a HOLDER's connection and
// every later statement (fill / read / drop) must land on it. maxSize=1 so the
// pool always hands out the SINGLE master connection (a :memory: clone would be
// a separate empty DB — the temp table must stay on one connection). If the
// headless env can't come up, the test SKIPS (it does not fail the suite).
//
// Isolated target (oes_temp_db_sqlite_test) so the appData bring-up cannot
// affect the main oes_tests run.
// =============================================================================

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <wx/init.h>                                       // wxInitializer — bring up wxBase (locale / std paths appData touches)

#include "backend/appData.h"
#include "backend/compiler/value.h"                       // ibValue / ibNumber / g_value*CLSID
#include "backend/typeDescription.h"                      // ibTypeDescription
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/connectionHolder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"       // ibDatabaseResultSet — ANALYZE-ran assertion
#include "backend/query/queryProvider.h"                  // ibQueryRamTable
#include "backend/query/queryable.h"                      // ibBackendQueryable
#include "backend/query/tempTableManager.h"               // ibTempTableManager
#include "backend/query/dataQueryBuilder.h"               // ibDataQueryBuilder / ibReadPageRequest

namespace {

// Brings up appData (like codeRunner) + a SQLite-only pool on the master
// connection. Skips (not fails) if the headless env / SQLite cannot come up.
struct TempDbSqliteFix : ::testing::Test {
	wxInitializer                          m_wxInit;   // wxBase up before appData (constructed pre-SetUp)
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	bool ready = false;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationData::CreateAppDataEnv(ibRunMode::eRUNTIME_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationData::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		// maxSize=1, minIdle=0 -> the pool only ever hands out the master `db`.
		pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);
		ready = true;
	}
	void TearDown() override {
		if (ibApplicationData::Get() != nullptr)
			ibApplicationData::DestroyAppDataEnv();
	}
};

} // namespace

// A scalar RAM table materialises into a SQLite temp table and reads back
// identically through the L3 door — temp-promote on the embedded DB.
TEST_F(TempDbSqliteFix, MaterialiseScalarRoundTrip)
{
	if (!ready) GTEST_SKIP();

	const ibMetaID ID = 101, NAME = 102;
	ibQueryRamTable ram;
	ram.AddColumn(ID,   wxT("id"),   ibTypeDescription(g_valueNumberCLSID));
	ram.AddColumn(NAME, wxT("name"), ibTypeDescription(g_valueStringCLSID));
	auto row = [&](long id, const wxString& name) {
		const long r = ram.AppendRow();
		ram.SetCell(r, ID,   ibValue(ibNumber(id)));
		ram.SetCell(r, NAME, ibValue(name));
	};
	row(1, wxT("Alice"));
	row(2, wxT("Bob"));
	row(3, wxT("Carol"));

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	std::unique_ptr<ibTempTableManager> mgr =
		ibTempTableManager::Materialise(holder, ram, /*metaData*/ nullptr);
	ASSERT_NE(mgr, nullptr)
		<< "SQLite temp dialect should let Materialise create a server-side temp table";

	const ibBackendQueryable* temp = mgr->Queryable();
	ASSERT_NE(temp, nullptr);

	// Read the temp table back through the L3 door (a server-side SELECT on the
	// pinned connection) and count the rows that round-tripped.
	ibDataQueryBuilder q(holder);
	q.From(temp);
	ibReadPageRequest page;                 // count 0 -> all rows
	ibDataQueryResult sel = q.Execute(page);
	int n = 0;
	while (sel.Next()) ++n;
	EXPECT_EQ(n, 3) << "every row must round-trip through the SQLite temp table";

	// Materialise runs ANALYZE on the filled temp so the planner has real cardinality. Proof it
	// EXECUTED (not silently swallowed): ANALYZE creates an sqlite_stat1 table (in the temp schema
	// for a temp table, or main). The actual plan/perf benefit is DB-specific and measured on PG —
	// SQLite test data is too small for timing to mean anything; this only asserts the lever fired.
	ibDatabaseResultSet* stat = db->RunQueryWithResults(wxT("%s"), wxString(wxT(
		"SELECT (SELECT count(*) FROM sqlite_temp_master WHERE type='table' AND name='sqlite_stat1') + "
		"       (SELECT count(*) FROM sqlite_master      WHERE type='table' AND name='sqlite_stat1') AS c")));
	const int statTables = (stat != nullptr && stat->Next()) ? stat->GetResultInt(wxT("c")) : 0;
	if (stat != nullptr) db->CloseResultSet(stat);
	EXPECT_GE(statTables, 1) << "ANALYZE should have created sqlite_stat1 (stats refresh executed)";
}

// Empty input -> no temp table (the manager returns null; the caller stays on
// the RAM floor). Guards the documented "Columns().empty() -> nullptr" contract.
TEST_F(TempDbSqliteFix, MaterialiseEmptyReturnsNull)
{
	if (!ready) GTEST_SKIP();
	ibQueryRamTable empty;
	EXPECT_EQ(ibTempTableManager::Materialise(ibConnectionPool::ThreadHolder(), empty, nullptr), nullptr);
}

// Value fidelity through the L3 READ door: the EXACT cell values written into the
// source must come back through Execute() / GetValue(), not just the row count.
// This is the read half of the value-codec round-trip that test_columnCodec
// explicitly defers to "a CI integration target". Integer amounts (cents) keep
// the number columns exact, independent of the temp column's SQL storage type.
TEST_F(TempDbSqliteFix, DoorReadsBackExactCellValues)
{
	if (!ready) GTEST_SKIP();

	const ibMetaID ID = 201, NAME = 202, AMT = 203;
	ibQueryRamTable ram;
	ram.AddColumn(ID,   wxT("id"),   ibTypeDescription(g_valueNumberCLSID));
	ram.AddColumn(NAME, wxT("name"), ibTypeDescription(g_valueStringCLSID));
	ram.AddColumn(AMT,  wxT("amt"),  ibTypeDescription(g_valueNumberCLSID));
	auto add = [&](long id, const wxString& nm, long amt) {
		const long r = ram.AppendRow();
		ram.SetCell(r, ID,   ibValue(ibNumber(id)));
		ram.SetCell(r, NAME, ibValue(nm));
		ram.SetCell(r, AMT,  ibValue(ibNumber(amt)));
	};
	add(1, wxT("Alice"),  1050);
	add(2, wxT("Bob"),    -325);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);
	std::unique_ptr<ibTempTableManager> mgr =
		ibTempTableManager::Materialise(holder, ram, /*metaData*/ nullptr);
	ASSERT_NE(mgr, nullptr);
	const ibBackendQueryable* temp = mgr->Queryable();
	ASSERT_NE(temp, nullptr);

	auto colById = [&](ibMetaID id) -> const ibBackendQueryColumn* {
		for (const ibBackendQueryColumn* c : temp->GetColumns())
			if (c->GetColumnId() == id) return c;
		return nullptr;
	};
	const ibBackendQueryColumn* cId   = colById(ID);
	const ibBackendQueryColumn* cName = colById(NAME);
	const ibBackendQueryColumn* cAmt  = colById(AMT);
	ASSERT_NE(cId,   nullptr);
	ASSERT_NE(cName, nullptr);
	ASSERT_NE(cAmt,  nullptr);

	ibDataQueryBuilder q(holder);
	q.From(temp);
	ibReadPageRequest page;
	ibDataQueryResult sel = q.Execute(page);

	int rows = 0;
	bool sawAlice = false, sawBob = false;
	while (sel.Next()) {
		++rows;
		const int      id   = sel.GetValue(cId).GetInteger();
		const wxString name = sel.GetValue(cName).GetString();
		const int      amt  = sel.GetValue(cAmt).GetInteger();
		if (id == 1) { EXPECT_EQ(name, wxT("Alice")); EXPECT_EQ(amt, 1050); sawAlice = true; }
		if (id == 2) { EXPECT_EQ(name, wxT("Bob"));   EXPECT_EQ(amt, -325); sawBob   = true; }
	}
	EXPECT_EQ(rows, 2);
	EXPECT_TRUE(sawAlice) << "row 1 must round-trip through the door with exact values";
	EXPECT_TRUE(sawBob)   << "row 2 (negative amount) must round-trip exactly";
}

// L3 WRITE door round-trip on a real (temp) SQLite table. The temp queryable
// inherits the ordinary DB provider, so From(temp).SetValue().Insert()/.Delete()
// renders a real INSERT / DELETE against the temp table — the same write surface
// the record descriptors sit on (commonObjectRefQuery SaveData / DeleteData).
// Insert a row through the door, confirm read-your-writes through the door, then
// delete it and confirm it is gone. This is the write half that test_columnCodec
// defers to "a CI integration target".
TEST_F(TempDbSqliteFix, WriteDoorInsertReadDeleteRoundTrip)
{
	if (!ready) GTEST_SKIP();

	const ibMetaID ID = 301, NAME = 302;
	ibQueryRamTable ram;
	ram.AddColumn(ID,   wxT("id"),   ibTypeDescription(g_valueNumberCLSID));
	ram.AddColumn(NAME, wxT("name"), ibTypeDescription(g_valueStringCLSID));
	{	// one seed row so Materialise creates the table with both columns
		const long r = ram.AppendRow();
		ram.SetCell(r, ID,   ibValue(ibNumber(1)));
		ram.SetCell(r, NAME, ibValue(wxString(wxT("Seed"))));
	}

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);
	std::unique_ptr<ibTempTableManager> mgr =
		ibTempTableManager::Materialise(holder, ram, /*metaData*/ nullptr);
	ASSERT_NE(mgr, nullptr);
	const ibBackendQueryable* temp = mgr->Queryable();
	ASSERT_NE(temp, nullptr);

	auto colById = [&](ibMetaID id) -> const ibBackendQueryColumn* {
		for (const ibBackendQueryColumn* c : temp->GetColumns())
			if (c->GetColumnId() == id) return c;
		return nullptr;
	};
	const ibBackendQueryColumn* cId   = colById(ID);
	const ibBackendQueryColumn* cName = colById(NAME);
	ASSERT_NE(cId,   nullptr);
	ASSERT_NE(cName, nullptr);

	auto countRows = [&]() -> int {
		ibDataQueryBuilder q(holder);
		q.From(temp);
		ibReadPageRequest page;
		ibDataQueryResult sel = q.Execute(page);
		int n = 0;
		while (sel.Next()) ++n;
		return n;
	};

	ASSERT_EQ(countRows(), 1) << "seed row present after Materialise";

	// INSERT a second row through the WRITE door.
	const bool inserted = ibDataQueryBuilder(holder)
		.From(temp)
		.SetValue(cId,   ibValue(ibNumber(2)))
		.SetValue(cName, ibValue(wxString(wxT("Added"))))
		.Insert();
	EXPECT_TRUE(inserted) << "door INSERT should render and execute against the temp table";
	EXPECT_EQ(countRows(), 2) << "read-your-writes: the inserted row is visible on the same holder";

	// DELETE the inserted row through the door (WHERE id = 2).
	ibDataQueryBuilder(holder)
		.From(temp)
		.Where(cId, ibValue(ibNumber(2)))
		.Delete();
	EXPECT_EQ(countRows(), 1) << "door DELETE removed exactly the inserted row";
}

// ---------------------------------------------------------------------------
// Read-door FEATURES on a real (temp) SQLite table: WhereLike filter, OrderBy
// sort and the SelectAggregate Sum/Min/Max/Count path — all server-side.
// ---------------------------------------------------------------------------

namespace {
// Build a 3-row temp { id, name, amt } and resolve its columns. Returns false
// (skip) if the driver vends no temp dialect.
struct DoorRows {
	const ibMetaID ID = 401, NAME = 402, AMT = 403;
	std::unique_ptr<ibTempTableManager> mgr;
	const ibBackendQueryable*   temp  = nullptr;
	const ibBackendQueryColumn* cId   = nullptr;
	const ibBackendQueryColumn* cName = nullptr;
	const ibBackendQueryColumn* cAmt  = nullptr;

	bool Build(ibDatabaseConnectionHolder* holder) {
		ibQueryRamTable ram;
		ram.AddColumn(ID,   wxT("id"),   ibTypeDescription(g_valueNumberCLSID));
		ram.AddColumn(NAME, wxT("name"), ibTypeDescription(g_valueStringCLSID));
		ram.AddColumn(AMT,  wxT("amt"),  ibTypeDescription(g_valueNumberCLSID));
		auto add = [&](long id, const wxString& nm, long amt) {
			const long r = ram.AppendRow();
			ram.SetCell(r, ID,   ibValue(ibNumber(id)));
			ram.SetCell(r, NAME, ibValue(nm));
			ram.SetCell(r, AMT,  ibValue(ibNumber(amt)));
		};
		add(3, wxT("Apple"),   30);
		add(1, wxT("Banana"),  10);
		add(2, wxT("Avocado"), 20);
		mgr = ibTempTableManager::Materialise(holder, ram, nullptr);
		if (!mgr) return false;
		temp = mgr->Queryable();
		if (!temp) return false;
		for (const ibBackendQueryColumn* c : temp->GetColumns()) {
			if      (c->GetColumnId() == ID)   cId   = c;
			else if (c->GetColumnId() == NAME) cName = c;
			else if (c->GetColumnId() == AMT)  cAmt  = c;
		}
		return cId && cName && cAmt;
	}
};
} // namespace

TEST_F(TempDbSqliteFix, DoorWhereLikeFiltersRows)
{
	if (!ready) GTEST_SKIP();
	DoorRows d;
	ASSERT_TRUE(d.Build(ibConnectionPool::ThreadHolder()));

	// name LIKE 'A%' -> Apple, Avocado (2 of 3).
	ibDataQueryBuilder q(ibConnectionPool::ThreadHolder());
	q.From(d.temp).WhereLike(d.cName, ibValue(wxString(wxT("A%"))));
	ibReadPageRequest page;
	ibDataQueryResult sel = q.Execute(page);
	int n = 0;
	while (sel.Next()) ++n;
	EXPECT_EQ(n, 2) << "WhereLike 'A%' should match Apple and Avocado";
}

// NOTE: OrderBy through the door is intentionally NOT exercised on a bare temp
// table — a sorted + paged read uses a keyset cursor that needs the queryable's
// identity / primary-key sort, which a temp table does not provide (it throws).
// That path belongs in the metaobject integration fixture (alongside Upsert +
// reference-as-key), not on this minimal harness.

TEST_F(TempDbSqliteFix, DoorAggregatesThroughSelectAggregate)
{
	if (!ready) GTEST_SKIP();
	DoorRows d;
	ASSERT_TRUE(d.Build(ibConnectionPool::ThreadHolder()));
	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();

	{	ibDataQueryBuilder q(holder); q.From(d.temp).Sum(d.cAmt, wxT("v"));
		ibDataQueryResult s = q.SelectAggregate();
		ASSERT_TRUE(s.Next());
		EXPECT_EQ(s.GetColumn(wxT("v")).GetInteger(), 60); }   // 30+10+20
	{	ibDataQueryBuilder q(holder); q.From(d.temp).Min(d.cAmt, wxT("v"));
		ibDataQueryResult s = q.SelectAggregate();
		ASSERT_TRUE(s.Next());
		EXPECT_EQ(s.GetColumn(wxT("v")).GetInteger(), 10); }
	{	ibDataQueryBuilder q(holder); q.From(d.temp).Max(d.cAmt, wxT("v"));
		ibDataQueryResult s = q.SelectAggregate();
		ASSERT_TRUE(s.Next());
		EXPECT_EQ(s.GetColumn(wxT("v")).GetInteger(), 30); }
	{	ibDataQueryBuilder q(holder); q.From(d.temp).Count(wxT("v"));
		ibDataQueryResult s = q.SelectAggregate();
		ASSERT_TRUE(s.Next());
		EXPECT_EQ(s.GetColumn(wxT("v")).GetInteger(), 3); }
}

TEST_F(TempDbSqliteFix, DoorWhereEqualityFilters)
{
	if (!ready) GTEST_SKIP();
	DoorRows d;
	ASSERT_TRUE(d.Build(ibConnectionPool::ThreadHolder()));

	ibDataQueryBuilder q(ibConnectionPool::ThreadHolder());
	q.From(d.temp).Where(d.cId, ibValue(ibNumber(2)));     // id = 2
	ibReadPageRequest page;
	ibDataQueryResult sel = q.Execute(page);
	int n = 0, seenId = -1;
	while (sel.Next()) { ++n; seenId = sel.GetValue(d.cId).GetInteger(); }
	EXPECT_EQ(n, 1);
	EXPECT_EQ(seenId, 2);
}

TEST_F(TempDbSqliteFix, DoorWhereCompareGreater)
{
	if (!ready) GTEST_SKIP();
	DoorRows d;
	ASSERT_TRUE(d.Build(ibConnectionPool::ThreadHolder()));

	// amt > 15 -> 20, 30 (two of {30, 10, 20})
	ibDataQueryBuilder q(ibConnectionPool::ThreadHolder());
	q.From(d.temp).WhereCompare(d.cAmt, ibQueryFilterOp::Greater, ibValue(ibNumber(15)));
	ibReadPageRequest page;
	ibDataQueryResult sel = q.Execute(page);
	int n = 0;
	while (sel.Next()) ++n;
	EXPECT_EQ(n, 2);
}

TEST_F(TempDbSqliteFix, DoorMultipleAggregatesInOneRow)
{
	if (!ready) GTEST_SKIP();
	DoorRows d;
	ASSERT_TRUE(d.Build(ibConnectionPool::ThreadHolder()));

	ibDataQueryBuilder q(ibConnectionPool::ThreadHolder());
	q.From(d.temp).Sum(d.cAmt, wxT("s")).Count(wxT("c"));
	ibDataQueryResult sel = q.SelectAggregate();
	ASSERT_TRUE(sel.Next());
	EXPECT_EQ(sel.GetColumn(wxT("s")).GetInteger(), 60);    // 30+10+20
	EXPECT_EQ(sel.GetColumn(wxT("c")).GetInteger(), 3);
}

// ===========================================================================
// RLS semi-join (⋉) as a correlated EXISTS — the behavioural proof on real SQLite.
//
// The access-policy decorator folds `restrict s join perm on s.k = perm.k` into the
// query's WHERE predicate as an ibSemiJoinExists (ibDataQueryBuilder::AddSemiJoin);
// the provider renders it EXISTS(SELECT * FROM perm sj WHERE sj.k = s.k) — the SAME
// render the register-direct path AND the temp-promoted value-table / multi-source path
// both funnel through (BuildSemiJoinExists). RLS lives or dies on two properties, both
// asserted here by RUNNING the render against SQLite:
//   * it FILTERS — a row passes iff a permitting row EXISTS (no permission → not visible);
//   * it NEVER MULTIPLIES — a row permitted by N rows appears ONCE, not N times (an INNER
//     JOIN would duplicate; the filter-not-JOIN principle, docs/access-policy-rls.md).
// Both sources are SQLite temp tables (the vehicle), so the correlation runs server-side
// exactly like a temp-promoted inner — the outer key qualifies by the source's own table
// name (BuildConditionExpr's empty-mainQual fallback), so it never binds inside the EXISTS.
// ===========================================================================
namespace {
// Materialise a one-column NUMBER-"k" temp table from a key list (DUPLICATES allowed — a
// permission source can grant the same key more than once). false (skip) if no temp dialect.
struct KeyTable {
	std::unique_ptr<ibTempTableManager> mgr;
	const ibBackendQueryable*   temp = nullptr;
	const ibBackendQueryColumn* k    = nullptr;
	bool Build(ibDatabaseConnectionHolder* holder, ibMetaID keyId, const std::vector<long>& keys) {
		ibQueryRamTable ram;
		ram.AddColumn(keyId, wxT("k"), ibTypeDescription(g_valueNumberCLSID));
		for (long v : keys) { const long r = ram.AppendRow(); ram.SetCell(r, keyId, ibValue(ibNumber(v))); }
		mgr = ibTempTableManager::Materialise(holder, ram, /*metaData*/ nullptr);
		if (!mgr) return false;
		temp = mgr->Queryable();
		if (temp == nullptr) return false;
		for (const ibBackendQueryColumn* c : temp->GetColumns())
			if (c->GetColumnId() == keyId) k = c;
		return k != nullptr;
	}
};

// `SELECT * FROM outer WHERE [NOT] EXISTS(perm correlated on k)` — the semi-join is the ONLY
// restriction. Returns the surviving outer keys (dups in the result would prove multiplication).
std::vector<int> SemiJoinSurvivors(ibDatabaseConnectionHolder* holder,
                                   const KeyTable& outer, const KeyTable& perm, bool negated = false) {
	ibSemiJoinExists sj;
	sj.m_inner    = perm.temp;    // the permission source — EXISTS scans it
	sj.m_outerKey = outer.k;      // s.k
	sj.m_innerKey = perm.k;       // a.k
	sj.m_op       = ibQueryFilterOp::Equal;
	sj.m_negated  = negated;      // NOT EXISTS — the anti-join / deny-if-present rule

	ibDataQueryBuilder q(holder);
	q.From(outer.temp);
	q.AddSemiJoin(sj);
	ibReadPageRequest page;                 // count 0 -> all rows
	ibDataQueryResult sel = q.Execute(page);
	std::vector<int> keys;
	while (sel.Next()) keys.push_back(sel.GetValue(outer.k).GetInteger());
	return keys;
}
} // namespace

TEST_F(TempDbSqliteFix, RlsSemiJoin_FiltersByExistence)
{
	if (!ready) GTEST_SKIP();
	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	KeyTable sales, perm;
	ASSERT_TRUE(sales.Build(holder, 601, {1, 2, 3, 4}));
	ASSERT_TRUE(perm .Build(holder, 602, {2, 4}));

	std::vector<int> got = SemiJoinSurvivors(holder, sales, perm);
	std::sort(got.begin(), got.end());
	EXPECT_EQ(got, (std::vector<int>{2, 4})) << "only rows whose key EXISTS in the permission set survive";
}

TEST_F(TempDbSqliteFix, RlsSemiJoin_DoesNotMultiply)
{
	if (!ready) GTEST_SKIP();
	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	KeyTable sales, perm;
	ASSERT_TRUE(sales.Build(holder, 611, {1, 2}));
	ASSERT_TRUE(perm .Build(holder, 612, {2, 2, 2}));   // key 2 permitted THREE times

	// An INNER JOIN would yield key 2 THREE times (one per permission match). The semi-join
	// FILTERS: key 2 appears exactly ONCE. This is the whole point — a restriction must not
	// duplicate rows (docs/access-policy-rls.md — RLS join = FILTER, not JOIN).
	EXPECT_EQ(SemiJoinSurvivors(holder, sales, perm), (std::vector<int>{2}))
		<< "a row permitted by N rows appears ONCE, not N times";
}

TEST_F(TempDbSqliteFix, RlsSemiJoin_NoMatchDeniesAll)
{
	if (!ready) GTEST_SKIP();
	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	KeyTable sales, perm;
	ASSERT_TRUE(sales.Build(holder, 621, {1, 2, 3}));
	ASSERT_TRUE(perm .Build(holder, 622, {99}));        // grants nothing that matches

	EXPECT_TRUE(SemiJoinSurvivors(holder, sales, perm).empty())
		<< "no permitting row -> zero visible rows (fail-closed)";
}

TEST_F(TempDbSqliteFix, RlsSemiJoin_NegatedIsAntiJoin)
{
	if (!ready) GTEST_SKIP();
	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	KeyTable sales, perm;
	ASSERT_TRUE(sales.Build(holder, 631, {1, 2, 3, 4}));
	ASSERT_TRUE(perm .Build(holder, 632, {2, 4}));

	std::vector<int> got = SemiJoinSurvivors(holder, sales, perm, /*negated*/ true);
	std::sort(got.begin(), got.end());
	EXPECT_EQ(got, (std::vector<int>{1, 3})) << "NOT EXISTS: only the rows with NO permitting row (anti-join)";
}

// The WRITE half: an RLS semi-join rides m_predicate, and the DELETE path lowers m_predicate
// through BuildWhere (the earlier flat loop read only m_conditions and left writes UNENFORCED —
// a silent leak). So a blanket DELETE under the restriction scopes to PERMITTED rows only; an
// unpermitted row cannot be deleted. (docs/access-policy-rls.md — Write path)
TEST_F(TempDbSqliteFix, RlsSemiJoin_RestrictsDelete)
{
	if (!ready) GTEST_SKIP();
	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	KeyTable data, perm;
	ASSERT_TRUE(data.Build(holder, 641, {1, 2, 3}));
	ASSERT_TRUE(perm.Build(holder, 642, {2}));          // only key 2 is permitted

	ibSemiJoinExists sj;
	sj.m_inner = perm.temp; sj.m_outerKey = data.k; sj.m_innerKey = perm.k; sj.m_op = ibQueryFilterOp::Equal;
	ibDataQueryBuilder del(holder);       // AddSemiJoin returns void — not chainable, so build in steps
	del.From(data.temp);
	del.AddSemiJoin(sj);
	del.Delete();

	ibDataQueryBuilder q(holder);
	q.From(data.temp);
	ibReadPageRequest page;
	ibDataQueryResult sel = q.Execute(page);
	std::vector<int> left;
	while (sel.Next()) left.push_back(sel.GetValue(data.k).GetInteger());
	std::sort(left.begin(), left.end());
	EXPECT_EQ(left, (std::vector<int>{1, 3}))
		<< "the RLS semi-join scoped the DELETE to permitted rows — unpermitted rows survive (write-enforced)";
}

// Several joins on one restriction (Max: "а несколько джоинов переваривать будет?"). Each AddSemiJoin
// AND-folds its EXISTS into m_predicate, so a row must satisfy EVERY semi-join — and each is still a
// FILTER, so the intersection never multiplies. (docs/access-policy-rls.md — the tree rides every path)
TEST_F(TempDbSqliteFix, RlsSemiJoin_MultipleJoinsCompose)
{
	if (!ready) GTEST_SKIP();
	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	KeyTable sales, permA, permB;
	ASSERT_TRUE(sales.Build(holder, 651, {1, 2, 3, 4}));
	ASSERT_TRUE(permA.Build(holder, 652, {2, 3, 4}));
	ASSERT_TRUE(permB.Build(holder, 653, {3, 4, 5}));

	auto mk = [](const KeyTable& out, const KeyTable& perm) {
		ibSemiJoinExists sj;
		sj.m_inner = perm.temp; sj.m_outerKey = out.k; sj.m_innerKey = perm.k; sj.m_op = ibQueryFilterOp::Equal;
		return sj;
	};
	ibDataQueryBuilder q(holder);
	q.From(sales.temp);
	q.AddSemiJoin(mk(sales, permA));    // EXISTS(permA) AND …
	q.AddSemiJoin(mk(sales, permB));    // … EXISTS(permB)
	ibReadPageRequest page;
	ibDataQueryResult sel = q.Execute(page);
	std::vector<int> got;
	while (sel.Next()) got.push_back(sel.GetValue(sales.k).GetInteger());
	std::sort(got.begin(), got.end());
	EXPECT_EQ(got, (std::vector<int>{3, 4})) << "two semi-joins AND-compose: only keys present in BOTH permission sets";
}
