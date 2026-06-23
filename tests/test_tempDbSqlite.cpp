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
