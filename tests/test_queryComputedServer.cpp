// =============================================================================
// Server-side push of a COMPUTED source — the temp-promote.
//
// A single computed source (register slice / balance / subquery) with >= kTempTableMinRows rows is
// MATERIALISED into a DB temp table, and its aggregate runs as SERVER-SIDE SQL (GROUP BY over a REAL
// SQLite table) instead of a RAM fold. This is the engine's rule: "push what you can to the DBMS; RAM is
// the fallback." A reference column travels as its ibReference blob (the temp stores the spread) — grouping
// / filtering by the reference needs NO decomposition. A DOT-WALK (navigate THROUGH a reference) is not
// promoted (stays the RAM path). This harness proves the SERVER path produces the correct result on a real
// embedded SQLite. (docs/temp-db.md)
//
// Needs a real connection (temp tables) so it brings up appData + the connection pool over an in-memory
// SQLite, like test_tempDbSqlite. Skips (not fails) if the headless env cannot come up.
// =============================================================================

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <wx/init.h>                                         // wxInitializer — bring up wxBase

#include "backend/appData.h"                                 // ibApplicationData
#include "backend/compiler/value.h"                          // ibValue / ibNumber / g_value*CLSID
#include "backend/typeDescription.h"                         // ibTypeDescription
#include "backend/databaseLayer/connectionPool.h"            // ibConnectionPool::ThreadHolder
#include "backend/databaseLayer/connectionHolder.h"          // ibDatabaseConnectionHolder
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/query/queryProvider.h"
#include "backend/query/queryable.h"
#include "backend/query/queryColumn.h"

namespace {

// A number / string column with a REAL type descriptor (Materialise creates the temp column by role).
class TypedCol : public ibBackendQueryColumn {
public:
	TypedCol(const wxString& name, ibMetaID id, ibTypeDescription type) : m_name(name), m_id(id), m_type(std::move(type)) {}
	wxString           GetName()         const override { return m_name; }
	wxString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }
	ibMetaID           GetColumnId()     const override { return m_id; }
private:
	wxString                  m_name;
	ibMetaID                  m_id;
	mutable ibTypeDescription m_type;
};

// A computed (RAM) queryable, rows built by a builder each call (ibQueryRamTable is move-only).
class ComputedQ : public ibBackendQueryable {
public:
	ComputedQ(const wxString& name, ibMetaID id) : m_name(name), m_id(id) {}
	void AddCol(const ibBackendQueryColumn* c) { m_cols.push_back(c); }
	void SetBuilder(std::function<ibQueryRamTable()> b) { m_build = std::move(b); }

	ibBackendQueryProvider& GetProvider() const override { return ibComputedProviderInstance(); }
	bool     IsComputedInRam() const override { return true; }
	ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const override {
		return m_build ? m_build() : ibQueryRamTable();
	}
	std::vector<const ibBackendQueryColumn*> GetColumns() const override { return m_cols; }
	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c->GetName() == name) return c;
		return nullptr;
	}
	bool OwnsColumn(const ibBackendQueryColumn* col) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c == col) return true;
		return false;
	}
	wxString GetQueryTableName() const override { return m_name; }
	ibMetaID GetQueryTableId()   const override { return m_id; }
	ibGuid   GetQueryTableGuid() const override { ibGuidImpl i{}; i.m_data1 = static_cast<unsigned long>(m_id); return ibGuid(i); }
	std::vector<ibQuerySortItem> GetIdentitySort() const override { return {}; }
	const ibMetaData* GetMetaData() const override { return nullptr; }
private:
	wxString m_name;
	ibMetaID m_id;
	std::vector<const ibBackendQueryColumn*> m_cols;
	std::function<ibQueryRamTable()> m_build;
};

const ibMetaID S_ITEM = 20, S_QTY = 21;

} // namespace

struct ComputedServerFix : ::testing::Test {
	wxInitializer                          m_wxInit;   // wxBase up before appData
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
		pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);
		ready = true;
	}
	void TearDown() override {
		if (ibApplicationData::Get() != nullptr)
			ibApplicationData::DestroyAppDataEnv();
	}
};

// SUM(qty) GROUP BY item over a 1500-row computed source (>= kTempTableMinRows = 1000) -> temp-promote ->
// server-side SQL GROUP BY on SQLite. 3 items, 500 rows each, qty = 1 -> each item sums to 500.
TEST_F(ComputedServerFix, Aggregate_PromotesToServer)
{
	if (!ready) return;

	TypedCol item(wxT("item"), S_ITEM, ibTypeDescription(g_valueStringCLSID));
	TypedCol qty(wxT("qty"),   S_QTY,  ibTypeDescription(g_valueNumberCLSID));
	ComputedQ balance(wxT("balance"), 200);
	balance.AddCol(&item);
	balance.AddCol(&qty);
	balance.SetBuilder([] {
		ibQueryRamTable t;
		t.AddColumn(S_ITEM, wxT("item"), ibTypeDescription(g_valueStringCLSID));
		t.AddColumn(S_QTY,  wxT("qty"),  ibTypeDescription(g_valueNumberCLSID));
		for (long i = 0; i < 1500; ++i) {
			const long r = t.AppendRow();
			t.SetCell(r, S_ITEM, ibValue(wxString::Format(wxT("K%ld"), i % 3)));
			t.SetCell(r, S_QTY,  ibValue(ibNumber(1)));
		}
		return t;
	});

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(&balance);
	q.GroupBy(&item);
	q.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, &qty, wxT("total"));
	ibDataQueryResult res = q.SelectAggregate();   // -> ExecuteAggregate -> PromoteSingleComputed -> temp -> SQL

	std::map<std::string, long> totals;
	while (res.Next())
		totals[res.GetValue(&item).GetString().ToStdString()] = res.GetColumn(wxT("total")).GetInteger();

	EXPECT_EQ(totals.size(), 3u);
	EXPECT_EQ(totals["K0"], 500);
	EXPECT_EQ(totals["K1"], 500);
	EXPECT_EQ(totals["K2"], 500);
}
