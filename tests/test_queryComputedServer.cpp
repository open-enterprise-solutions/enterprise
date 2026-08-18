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
//
// The same fixture also covers the OTHER things the door pushes server-side over a PHYSICAL source, where
// the assertion has to be real rows on a real DBMS rather than a rendered string: the LINQ-lowered WHERE,
// and the set-valued `WhereIn` (the semi-join key filter — its render branch is TU-local to
// dbTableProvider.cpp and has no unit seam, so the door IS the seam).
// =============================================================================

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <utility>                                           // std::pair — the metadata-column fixture rows

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
#include "backend/query/columnLayout.h"       // ColumnFieldNames — the metadata column's physical field spelling
#include "backend/query/queryLowering.h"      // ibQueryLowering::LowerLambdaPredicate (L4-2 lowering)
#include "backend/query/queryAst.h"           // ibQueryAstExpr (recorded lambda)
#include "backend/compiler/compileCode.h"     // ibCompileCode — script lexer feeding the LINQ recorder
#include "backend/compiler/lambdaQueryAst.h"  // ibBuildLambdaQueryAst (L4-2 recorder)

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
	const ibMetaData* GetMetaData() const override { return nullptr; }
private:
	wxString m_name;
	ibMetaID m_id;
	std::vector<const ibBackendQueryColumn*> m_cols;
	std::function<ibQueryRamTable()> m_build;
};

const ibMetaID S_ITEM = 20, S_QTY = 21;

// A PHYSICAL source over a real DB table: it does NOT override GetProvider (inherits the DB provider) and
// IsComputedInRam() is false, so the door runs it as SERVER SQL -- a LINQ-lowered WHERE on it executes on
// the DBMS (ibDbTableProvider::ExecuteRead), not a RAM fold.
class PhysicalQ : public ibBackendQueryable {
public:
	PhysicalQ(const wxString& table, ibMetaID id) : m_table(table), m_id(id) {}
	void AddCol(const ibBackendQueryColumn* c) { m_cols.push_back(c); }
	bool     IsComputedInRam()   const override { return false; }
	std::vector<const ibBackendQueryColumn*> GetColumns() const override { return m_cols; }
	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c->GetName() == name) return c;
		return nullptr;
	}
	bool OwnsColumn(const ibBackendQueryColumn* col) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c == col) return true;
		return false;
	}
	wxString GetQueryTableName() const override { return m_table; }
	ibMetaID GetQueryTableId()   const override { return m_id; }
	ibGuid   GetQueryTableGuid() const override { ibGuidImpl i{}; i.m_data1 = static_cast<unsigned long>(m_id); return ibGuid(i); }
	const ibMetaData* GetMetaData() const override { return nullptr; }
private:
	wxString m_table;
	ibMetaID m_id;
	std::vector<const ibBackendQueryColumn*> m_cols;
};

// Record a lambda BODY into the L4-2 query AST (script lexer -> recorder), as test_queryLinqExec does.
std::shared_ptr<ibQueryAstExpr> RecordLambda(const wxString& body, const wxString& rowParam = wxT("x")) {
	ibCompileCode cc;
	cc.Load(body);
	if (!cc.PrepareLexem()) return nullptr;
	const std::vector<ibLexem>& lex = cc.GetLexems();
	size_t to = lex.size();
	while (to > 0 && lex[to - 1].m_lexType == ENDPROGRAM) --to;
	return ibBuildLambdaQueryAst(lex, 0, to, rowParam);
}

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

// A LINQ lambda WHERE, lowered and run over a PHYSICAL DB source, executes as SERVER SQL on SQLite (not a
// RAM fold): x.region == "North" -> door.Where(predicate) -> ExecuteRead -> SELECT ... FROM t WHERE
// region = 'North'. Proves the LINQ front-end PUSHES to the DBMS, not just the RAM-parity test_queryLinqExec
// proves. (The predicate is scalar, so no metaData is needed -- the config-layer wall is not hit here.)
TEST_F(ComputedServerFix, Linq_WherePushesToServer)
{
	if (!ready) return;
	ibCompileCode::SetCodeStyle(CODE_CES);   // the lambda body below is CES ('{ return … ; }')

	db->RunQuery(wxT("CREATE TABLE t (region TEXT, qty INTEGER)"));
	db->RunQuery(wxT("INSERT INTO t (region, qty) VALUES ('North', 10), ('South', 5), ('North', 7), ('East', 3)"));

	ibRawDBColumn region = ibRawDBColumn::String(wxT("region"));   // RAW scalar -> single physical field, matches the table column
	ibRawDBColumn qty    = ibRawDBColumn::Number(wxT("qty"));
	PhysicalQ src(wxT("t"), 300);
	src.AddCol(&region);
	src.AddCol(&qty);

	auto expr = RecordLambda(wxT("{ return x.region == \"North\"; }"));
	ASSERT_NE(expr, nullptr);
	ibQueryPredicatePtr pred = ibQueryLowering::LowerLambdaPredicate(&src, *expr, {});
	ASSERT_NE(pred, nullptr) << "a translatable LINQ body must lower to a predicate";

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(&src);
	q.Select(&qty, wxT("qty"));
	q.Where(pred);
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);   // physical source -> ibDbTableProvider::ExecuteRead -> server SQL

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 2);   // the two North rows survived the server-side WHERE (out of four) -> LINQ pushed to the DBMS
}

// ---- WhereIn: the semi-join key filter, rendered server-side -------------------
//
// ibMetaIRBuilder::BuildConditionExpr is TU-local to dbTableProvider.cpp, so its `In` branch has no
// direct unit seam — it is covered here through the SAME door a caller uses: door.WhereIn() ->
// ExecuteRead -> BuildFilterPredicate -> BuildConditionExpr -> L2 -> real SQLite. Row counts are the
// assertion, so a wrong render (a fragment compared, an empty set degrading to "no predicate") shows up
// as the wrong rows rather than as a passing string match. The RAM half of the same operator is pinned
// by the parity cases in test_queryParity.cpp.

// A RAW column is one physical field -> the native IN branch.
TEST_F(ComputedServerFix, In_RawColumnRendersNativeIn)
{
	if (!ready) return;

	db->RunQuery(wxT("CREATE TABLE t (region TEXT, qty INTEGER)"));
	db->RunQuery(wxT("INSERT INTO t (region, qty) VALUES ('North', 10), ('South', 5), ('North', 7), ('East', 3)"));

	ibRawDBColumn region = ibRawDBColumn::String(wxT("region"));
	ibRawDBColumn qty    = ibRawDBColumn::Number(wxT("qty"));
	PhysicalQ src(wxT("t"), 310);
	src.AddCol(&region);
	src.AddCol(&qty);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(&src);
	q.Select(&qty, wxT("qty"));
	q.WhereIn(&region, { ibValue(wxString(wxT("North"))), ibValue(wxString(wxT("East"))) });
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 3);   // two North + one East, the South row filtered server-side
}

// A NULL in the probed column must NOT survive the set test — SQL says UNKNOWN, and the reduction is only
// sound if it means the same thing here as in the RAM stitch (test_queryParity.In_NullProbeIsUnknown).
TEST_F(ComputedServerFix, In_NullProbeDropped)
{
	if (!ready) return;

	db->RunQuery(wxT("CREATE TABLE t (region TEXT, qty INTEGER)"));
	db->RunQuery(wxT("INSERT INTO t (region, qty) VALUES ('North', 10), (NULL, 5), ('East', 3)"));

	ibRawDBColumn region = ibRawDBColumn::String(wxT("region"));
	ibRawDBColumn qty    = ibRawDBColumn::Number(wxT("qty"));
	PhysicalQ src(wxT("t"), 311);
	src.AddCol(&region);
	src.AddCol(&qty);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(&src);
	q.Select(&qty, wxT("qty"));
	q.WhereIn(&region, { ibValue(wxString(wxT("North"))), ibValue(wxString(wxT("East"))) });
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 2);   // the NULL-region row is UNKNOWN against the set, not a member
}

// THE load-bearing case: the driving side of a semi-join produced no keys, so the reduced read must return
// NOTHING. A filter that degraded to "no predicate" would return the whole table instead — the failure mode
// that turns an optimisation into a wrong answer.
TEST_F(ComputedServerFix, In_EmptySetReturnsNoRows)
{
	if (!ready) return;

	db->RunQuery(wxT("CREATE TABLE t (region TEXT, qty INTEGER)"));
	db->RunQuery(wxT("INSERT INTO t (region, qty) VALUES ('North', 10), ('South', 5), ('East', 3)"));

	ibRawDBColumn region = ibRawDBColumn::String(wxT("region"));
	ibRawDBColumn qty    = ibRawDBColumn::Number(wxT("qty"));
	PhysicalQ src(wxT("t"), 312);
	src.AddCol(&region);
	src.AddCol(&qty);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(&src);
	q.Select(&qty, wxT("qty"));
	q.WhereIn(&region, {});             // no keys -> L2 renders the empty IN as 1 = 0
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 0);
}

// An all-NULL key set is the same thing: WhereIn strips NULLs on the way in (a NULL key joins nothing), so
// what reaches the render is the EMPTY set — not `IN (NULL)`, which SQL would answer UNKNOWN for every row.
// Same outcome, but for a reason worth pinning separately: the stripping happens at the door, not the render.
TEST_F(ComputedServerFix, In_AllNullKeysStripToEmpty)
{
	if (!ready) return;

	db->RunQuery(wxT("CREATE TABLE t (region TEXT, qty INTEGER)"));
	db->RunQuery(wxT("INSERT INTO t (region, qty) VALUES ('North', 10), ('South', 5)"));

	ibRawDBColumn region = ibRawDBColumn::String(wxT("region"));
	ibRawDBColumn qty    = ibRawDBColumn::Number(wxT("qty"));
	PhysicalQ src(wxT("t"), 313);
	src.AddCol(&region);
	src.AddCol(&qty);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(&src);
	q.Select(&qty, wxT("qty"));
	q.WhereIn(&region, { ibValue(ibValueTypes::TYPE_NULL), ibValue(ibValueTypes::TYPE_NULL) });
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 0);
}

// A METADATA column (IsRawColumn() == false) is NEVER one field: DescribeColumnLayout always emits the
// `_TYPE` variant discriminator plus one slot per primitive the type descriptor admits (a string-only
// descriptor -> `_TYPE`, `_S`). The `In` render therefore OR-folds the same composite equality Eq uses,
// once per value, so the _TYPE tag is compared too and the value is bound through the write-spread codec
// (load-bearing for a variant — a native IN on one primitive field would match rows of the wrong variant —
// and for a reference, whose constant must be the encoded _RRRef blob, not a bare ibConst).
//
// The fixture is built FROM the layout (ColumnFieldNames, in bind order) rather than hardcoding the suffix
// table, and the discriminator is seeded through ibPersistedTypeTag — so the test asserts against the same
// contract the codec writes by, instead of duplicating it. If the _TYPE seed were wrong, this test would
// fail: the folded predicate compares it.
namespace {

struct MetaColFixture {
	std::vector<wxString> fields;   // [0] = _TYPE, [1] = _S (bind order)
	wxString              typeField;
	wxString              strField;
};

MetaColFixture MakeMetaColTable(ibDatabaseLayerSQLite& db, const wxString& table,
                                const ibBackendQueryColumn* col,
                                const std::vector<std::pair<wxString, long>>& rows)
{
	MetaColFixture f;
	f.fields = ColumnFieldNames(col);
	if (f.fields.size() != 2)
		return f;                       // caller asserts — the layout changed shape
	f.typeField = f.fields[0];
	f.strField  = f.fields[1];

	const wxString ddl = wxT("CREATE TABLE ") + table + wxT(" (") + f.typeField + wxT(" INTEGER, ")
	                   + f.strField + wxT(" TEXT, qty INTEGER)");
	db.RunQuery(wxT("%s"), ddl);   // as a %s ARGUMENT — the statement is assembled, not a format string

	const int strTag = ibPersistedTypeTag(ibColumnRole::String);
	for (const auto& r : rows) {
		const wxString ins = wxString::Format(
			wxT("INSERT INTO %s (%s, %s, qty) VALUES (%d, '%s', %ld)"),
			table, f.typeField, f.strField, strTag, r.first, r.second);
		db.RunQuery(wxT("%s"), ins);
	}
	return f;
}

} // namespace

TEST_F(ComputedServerFix, In_MetadataColumnFoldsCompositeEquality)
{
	if (!ready) return;

	TypedCol region(wxT("region"), 320, ibTypeDescription(g_valueStringCLSID));
	const MetaColFixture f = MakeMetaColTable(*db, wxT("m1"), &region,
		{ { wxT("North"), 10 }, { wxT("South"), 5 }, { wxT("East"), 3 } });
	ASSERT_EQ(f.fields.size(), 2u) << "a string-only metadata column is expected to spread as _TYPE + _S; "
	                                  "a different layout needs this fixture reworked";

	ibRawDBColumn qty = ibRawDBColumn::Number(wxT("qty"));
	PhysicalQ src(wxT("m1"), 321);
	src.AddCol(&region);
	src.AddCol(&qty);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(&src);
	q.Select(&qty, wxT("qty"));
	q.WhereIn(&region, { ibValue(wxString(wxT("North"))), ibValue(wxString(wxT("East"))) });
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 2);   // North + East via the OR-folded per-value composite equality
}

// The metadata-column fold must observe the empty set too — this is the branch where an empty OR-fold comes
// back as a NULL expression, i.e. NO predicate, i.e. the WHOLE table. Guarded explicitly in the render; this
// is the test that catches the guard being removed.
TEST_F(ComputedServerFix, In_MetadataColumnEmptySetReturnsNoRows)
{
	if (!ready) return;

	TypedCol region(wxT("region"), 330, ibTypeDescription(g_valueStringCLSID));
	const MetaColFixture f = MakeMetaColTable(*db, wxT("m2"), &region,
		{ { wxT("North"), 10 }, { wxT("South"), 5 } });
	ASSERT_EQ(f.fields.size(), 2u);

	ibRawDBColumn qty = ibRawDBColumn::Number(wxT("qty"));
	PhysicalQ src(wxT("m2"), 331);
	src.AddCol(&region);
	src.AddCol(&qty);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(&src);
	q.Select(&qty, wxT("qty"));
	q.WhereIn(&region, {});
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 0) << "an empty OR-fold must not degrade to 'no predicate' (the whole table)";
}
