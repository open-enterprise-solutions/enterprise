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

#include <memory>

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
