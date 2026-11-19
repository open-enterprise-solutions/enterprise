// =============================================================================
// A COMPUTED source read by a STATEMENT — its rows copied into a base in memory, its aggregate the statement's
// GROUP BY there, written by the same translation as over a table (ibQueryComposer::OverMemorySpace,
// docs/private/query-one-road.md §7). A reference column travels as its spread (the copy stores it as a table
// does), so grouping / filtering by the reference needs NO decomposition.
//
// Needs a real connection (the physical cases below) so it brings up appData + the connection pool over an
// in-memory SQLite, like test_tempDbSqlite. Skips (not fails) if the headless env cannot come up.
//
// The same fixture also covers the OTHER things the door pushes server-side over a PHYSICAL source, where
// the assertion has to be real rows on a real DBMS rather than a rendered string: the LINQ-lowered WHERE,
// and the set-valued `WhereIn` (the semi-join key filter — its render branch is TU-local to
// dbTableProvider.cpp and has no unit seam, so the door IS the seam).
// =============================================================================

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <utility>                                           // std::pair — the metadata-column fixture rows

#include <wx/init.h>                                         // wxInitializer — bring up wxBase

#include "backend/appData.h"                                 // ibApplicationInstance
#include "backend/compiler/value.h"                          // ibValue / ibNumber / g_value*CLSID
#include "core/description/typeDescription.h"                         // ibTypeDescription
#include "backend/databaseLayer/connectionPool.h"            // ibConnectionPool::ThreadHolder
#include "backend/databaseLayer/connectionHolder.h"          // ibDatabaseConnectionHolder
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/query/queryProvider.h"
#include "backend/query/queryable.h"
#include "backend/query/queryColumn.h"
#include "backend/query/columnLayout.h"       // ColumnFieldNames — the metadata column's physical field spelling
#include "backend/query/provider/database/columnCodec.h" // ibColumnCodec — a cell written and read back as the codec spreads it
#include "backend/databaseLayer/databaseQueryBuilder.h"   // ibScan / ibFunc — a statement written by hand (the codec test)
#include "backend/query/queryLowering.h"      // ibQueryLowering::LowerLambdaPredicate (L4-2 lowering)
#include "core/query/queryAST.h"           // ibQueryAstExpr (recorded lambda)
#include "backend/compiler/compileCode.h"     // ibCompileCode — script lexer feeding the LINQ recorder
#include "backend/compiler/lambdaQueryAST.h"  // ibBuildLambdaQueryAstFromCode (L4-2 recorder)

#include "core/clsid.h"                                   // reference_to_clsid — a reference column's type
#include "backend/metadataApplication.h"                     // ibMetaDataApplicationFile — a catalog to point references at
#include "backend/metaCollection/metaObject.h"               // g_metaCatalogCLSID
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject::Create
#include "backend/query/provider/database/dbTableProvider.h" // ibDbTableProvider::BuildPredicateIR — the door that writes every WHERE
#include "backend/databaseLayer/databaseQueryBuilder.h"      // ibQueryRenderer — the SQL a set becomes
#include "lambdaRecordFix.h"                  // ibTestRecordLambda — body -> compiled lambda -> AST
#include "queryRoad.h"                        // MeasureSent — the road a read took

namespace {

// A number / string column with a REAL type descriptor (Materialise creates the temp column by role).
class TypedCol : public ibBackendQueryColumn {
public:
	TypedCol(const ibString& name, ibMetaID id, ibTypeDescription type) : m_name(name), m_id(id), m_type(std::move(type)) {}
	ibString           GetName()         const override { return m_name; }
	ibString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }
	ibMetaID           GetColumnId()     const override { return m_id; }
	const ibMetaData*  GetMetaData()     const override { return nullptr; }
private:
	ibString                  m_name;
	ibMetaID                  m_id;
	mutable ibTypeDescription m_type;
};

// A computed queryable, rows built by a builder each call (ibQueryRamTable is move-only).
class ComputedQ : public ibBackendQueryable {
public:
	ComputedQ(const ibString& name, ibMetaID id) : m_name(name), m_id(id) {
		ibGuidImpl i{}; i.m_data1 = static_cast<unsigned long>(id); m_key = ibGuid(i);
	}
	void AddCol(const std::shared_ptr<const ibBackendQueryColumn>& c) { m_cols.push_back(c); }
	void SetBuilder(std::function<ibQueryRamTable()> b) { m_build = std::move(b); }

	ibBackendQueryProvider& GetProvider() const override { return ibComputedProviderInstance(); }
	ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const override {
		return m_build ? m_build() : ibQueryRamTable();
	}
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetQueryColumns() const override { return m_cols; }
	bool ContainColumn(const std::shared_ptr<const ibBackendQueryColumn>& col) const override {
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : m_cols) if (c == col) return true;
		return false;
	}
	ibString GetQueryTableName() const override { return m_name; }
	ibMetaID GetQueryTableId()   const override { return m_id; }
	const ibUniqueKey& GetQueryTableGuid() const override { return m_key; }
	const ibMetaData* GetMetaData() const override { return nullptr; }
private:
	ibString m_name;
	ibMetaID m_id;
	ibUniqueKey m_key;   // its table guid, the id in the first word
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_cols;
	std::function<ibQueryRamTable()> m_build;
};

const ibMetaID S_ITEM = 20, S_QTY = 21;

// A PHYSICAL source over a real DB table: it does NOT override GetProvider (inherits the DB provider, whose
// IsComputedInRam() is false), so the door runs it as SERVER SQL -- a LINQ-lowered WHERE on it executes on
// the DBMS (ibDbTableProvider::ExecuteRead), not a RAM fold.
class PhysicalQ : public ibBackendQueryable {
public:
	PhysicalQ(const ibString& table, ibMetaID id) : m_table(table), m_id(id) {
		ibGuidImpl i{}; i.m_data1 = static_cast<unsigned long>(id); m_key = ibGuid(i);
	}
	void AddCol(const std::shared_ptr<const ibBackendQueryColumn>& c) { m_cols.push_back(c); }
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetQueryColumns() const override { return m_cols; }
	bool ContainColumn(const std::shared_ptr<const ibBackendQueryColumn>& col) const override {
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : m_cols) if (c == col) return true;
		return false;
	}
	ibString GetQueryTableName() const override { return m_table; }
	ibMetaID GetQueryTableId()   const override { return m_id; }
	const ibUniqueKey& GetQueryTableGuid() const override { return m_key; }
	const ibMetaData* GetMetaData() const override { return nullptr; }
private:
	ibString m_table;
	ibMetaID m_id;
	ibUniqueKey m_key;   // its table guid, the id in the first word
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_cols;
};

// Record a lambda BODY into the L4-2 query AST (compile -> recorder), as test_queryLINQExec does.
std::shared_ptr<ibQueryAstExpr> RecordLambda(const ibString& body, const ibString& rowParam = wxT("x")) {
	return ibTestRecordLambda(body, rowParam);
}

} // namespace

struct ComputedServerFix : ::testing::Test {
	wxInitializer                          m_wxInit;   // wxBase up before appData
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	bool ready = false;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);
		ready = true;
	}
	void TearDown() override {
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}
};

// SUM(qty) GROUP BY item over a 1500-row computed source -> its rows copied into a base in memory -> the statement's
// GROUP BY there. 3 items, 500 rows each, qty = 1 -> each item sums to 500.
TEST_F(ComputedServerFix, Aggregate_IsTheStatementsGroupBy)
{
	if (!ready) return;

	const auto item = std::make_shared<TypedCol>(wxT("item"), S_ITEM, ibTypeDescription(g_valueStringCLSID));
	const auto qty  = std::make_shared<TypedCol>(wxT("qty"),  S_QTY,  ibTypeDescription(g_valueNumberCLSID));
	const auto balance = std::make_shared<ComputedQ>(wxT("balance"), 200);
	balance->AddCol(item);
	balance->AddCol(qty);
	balance->SetBuilder([] {
		ibQueryRamTable t;
		t.AddColumn(S_ITEM, wxT("item"), ibTypeDescription(g_valueStringCLSID));
		t.AddColumn(S_QTY,  wxT("qty"),  ibTypeDescription(g_valueNumberCLSID));
		for (long i = 0; i < 1500; ++i) {
			const long r = t.AppendRow();
			t.SetCell(r, S_ITEM, ibValue(ibString::Format(wxT("K%ld"), i % 3)));
			t.SetCell(r, S_QTY,  ibValue(ibNumber(1)));
		}
		return t;
	});

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(balance);
	q.GroupBy(item);
	q.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, qty, wxT("total"));

	std::map<std::string, long> totals;
	const SentToDatabase sent = MeasureSent([&] {
		ibDataQueryResult res = q.SelectAggregate();   // -> OverMemorySpace -> the copy -> the statement
		while (res.Next())
			totals[res.GetValue(item).GetString().ToStdString()] = res.GetColumn(wxT("total")).GetInteger();
	});
	// Folded by a fold of the engine's own, no statement would have grouped them.
	EXPECT_TRUE(std::any_of(sent.m_texts.begin(), sent.m_texts.end(),
		[](const ibString& text) { return text.Contains(wxT("GROUP BY")); }))
		<< "the rows went into a base in memory and the statement grouped them\n" << sent;

	EXPECT_EQ(totals.size(), 3u);
	EXPECT_EQ(totals["K0"], 500);
	EXPECT_EQ(totals["K1"], 500);
	EXPECT_EQ(totals["K2"], 500);
}

// A LINQ lambda WHERE, lowered and run over a PHYSICAL DB source, executes as SERVER SQL on SQLite (not a
// RAM fold): x.region == "North" -> door.Where(predicate) -> ExecuteRead -> SELECT ... FROM t WHERE
// region = 'North'. Proves the LINQ front-end PUSHES to the DBMS, not just the memory parity test_queryLINQExec
// proves. (The predicate is scalar, so no metaData is needed -- the application-layer wall is not hit here.)
TEST_F(ComputedServerFix, Linq_WherePushesToServer)
{
	if (!ready) return;
	ibTestCodeStyle style(CODE_CES);   // the lambda body below is CES ('{ return … ; }')

	db->RunQuery(wxT("CREATE TABLE t (region TEXT, qty INTEGER)"));
	db->RunQuery(wxT("INSERT INTO t (region, qty) VALUES ('North', 10), ('South', 5), ('North', 7), ('East', 3)"));

	const std::shared_ptr<const ibBackendColumnRawDB> region = ibBackendColumnRawDB::String(wxT("region"));   // RAW scalar -> single physical field, matches the table column
	const std::shared_ptr<const ibBackendColumnRawDB> qty    = ibBackendColumnRawDB::Number(wxT("qty"));
	const auto src = std::make_shared<PhysicalQ>(wxT("t"), 300);
	src->AddCol(region);
	src->AddCol(qty);

	// CES compares with `==` - see the note on Record in test_lambdaRecorder.cpp.
	auto expr = RecordLambda(wxT("{ return x.region == \"North\"; }"));
	ASSERT_NE(expr, nullptr);
	ibQueryPredicatePtr pred = ibQueryLowering::LowerLambdaPredicate(src, *expr, {});
	ASSERT_NE(pred, nullptr) << "a translatable LINQ body must lower to a predicate";

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(src);
	q.Select(qty, wxT("qty"));
	q.Where(pred);
	ibReadPageRequest page;
	int rows = 0;
	const SentToDatabase sent = MeasureSent([&] {
		ibDataQueryResult res = q.Execute(page);   // physical source -> ibDbTableProvider::ExecuteRead -> server SQL
		while (res.Next()) ++rows;
	});
	EXPECT_EQ(rows, 2);   // the two North rows survived the WHERE (out of four)…
	EXPECT_EQ(sent.m_rows, 2u) << "…and only they came back: the WHERE ran on the DBMS, not over four rows read\n" << sent;
}

// ---- WhereIn: the semi-join key filter, rendered server-side -------------------
//
// ibMetaIRBuilder::BuildConditionExpr is TU-local to dbTableProvider.cpp, so its `In` branch has no
// direct unit seam — it is covered here through the SAME door a caller uses: door.WhereIn() ->
// ExecuteRead -> BuildFilterPredicate -> BuildConditionExpr -> L2 -> real SQLite. Row counts are the
// assertion, so a wrong render (a fragment compared, an empty set degrading to "no predicate") shows up
// as the wrong rows rather than as a passing string match. The base-in-memory half of the same operator is
// pinned by the parity cases in test_queryParity.cpp.

// A RAW column is one physical field -> the native IN branch.
TEST_F(ComputedServerFix, In_RawColumnRendersNativeIn)
{
	if (!ready) return;

	db->RunQuery(wxT("CREATE TABLE t (region TEXT, qty INTEGER)"));
	db->RunQuery(wxT("INSERT INTO t (region, qty) VALUES ('North', 10), ('South', 5), ('North', 7), ('East', 3)"));

	const std::shared_ptr<const ibBackendColumnRawDB> region = ibBackendColumnRawDB::String(wxT("region"));
	const std::shared_ptr<const ibBackendColumnRawDB> qty    = ibBackendColumnRawDB::Number(wxT("qty"));
	const auto src = std::make_shared<PhysicalQ>(wxT("t"), 310);
	src->AddCol(region);
	src->AddCol(qty);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(src);
	q.Select(qty, wxT("qty"));
	q.WhereIn(region, { ibValue(ibString(wxT("North"))), ibValue(ibString(wxT("East"))) });
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 3);   // two North + one East, the South row filtered server-side
}

// ⭐ A QUESTION PUT TO A VALUE IN A CONDITION is answered over a table's rows as over a source computed in memory: no
// statement has SQL for it, so the read goes through a base in memory (ReadsInMemorySpace). Over a table it was refused,
// over a computed source answered — one query, two answers by where its rows came from.
TEST_F(ComputedServerFix, AQuestionPutToTheValueOfATableIsAnswered)
{
	if (!ready) return;

	db->RunQuery(wxT("CREATE TABLE t (region TEXT, qty INTEGER)"));
	db->RunQuery(wxT("INSERT INTO t (region, qty) VALUES ('North', 10), ('South', 5), ('North', 7), ('East', 3)"));

	// Ids of their own, as a column a query publishes has: rows read into memory are kept by column id (GetColumnId, unique
	// per column), and two columns of id 0 — a scaffold's — are one there.
	const std::shared_ptr<const ibBackendColumnRawDB> region = ibBackendColumnRawDB::String(wxT("region"), 1);
	const std::shared_ptr<const ibBackendColumnRawDB> qty    = ibBackendColumnRawDB::Number(wxT("qty"), 2);
	const auto src = std::make_shared<PhysicalQ>(wxT("t"), 314);
	src->AddCol(region);
	src->AddCol(qty);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(src);
	q.Select(qty, wxT("qty"));
	q.Where(region, ibValue(ibString(wxT("North"))));   // a plain condition still narrows the table's own read
	q.WhereExpr(ibQueryColumnExpr::ValueAsk(ibQueryColumnExpr::Col(qty), ibQueryValueAsk::Presentation),
		ibQueryFilterOp::Equal, ibValue(ibString(wxT("7"))));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	std::vector<long> seen;
	while (res.Next())
		seen.push_back(res.GetValue(qty).GetInteger());
	EXPECT_EQ(seen, std::vector<long>({ 7 }));
}

// A NULL in the probed column must NOT survive the set test — SQL says UNKNOWN, and the reduction is only
// sound if it means the same thing here as in a base in memory (test_queryParity.In_NullProbeIsUnknown).
TEST_F(ComputedServerFix, In_NullProbeDropped)
{
	if (!ready) return;

	db->RunQuery(wxT("CREATE TABLE t (region TEXT, qty INTEGER)"));
	db->RunQuery(wxT("INSERT INTO t (region, qty) VALUES ('North', 10), (NULL, 5), ('East', 3)"));

	const std::shared_ptr<const ibBackendColumnRawDB> region = ibBackendColumnRawDB::String(wxT("region"));
	const std::shared_ptr<const ibBackendColumnRawDB> qty    = ibBackendColumnRawDB::Number(wxT("qty"));
	const auto src = std::make_shared<PhysicalQ>(wxT("t"), 311);
	src->AddCol(region);
	src->AddCol(qty);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(src);
	q.Select(qty, wxT("qty"));
	q.WhereIn(region, { ibValue(ibString(wxT("North"))), ibValue(ibString(wxT("East"))) });
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

	const std::shared_ptr<const ibBackendColumnRawDB> region = ibBackendColumnRawDB::String(wxT("region"));
	const std::shared_ptr<const ibBackendColumnRawDB> qty    = ibBackendColumnRawDB::Number(wxT("qty"));
	const auto src = std::make_shared<PhysicalQ>(wxT("t"), 312);
	src->AddCol(region);
	src->AddCol(qty);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(src);
	q.Select(qty, wxT("qty"));
	q.WhereIn(region, {});             // no keys -> L2 renders the empty IN as 1 = 0
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

	const std::shared_ptr<const ibBackendColumnRawDB> region = ibBackendColumnRawDB::String(wxT("region"));
	const std::shared_ptr<const ibBackendColumnRawDB> qty    = ibBackendColumnRawDB::Number(wxT("qty"));
	const auto src = std::make_shared<PhysicalQ>(wxT("t"), 313);
	src->AddCol(region);
	src->AddCol(qty);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(src);
	q.Select(qty, wxT("qty"));
	q.WhereIn(region, { ibValue(ibValueTypes::TYPE_NULL), ibValue(ibValueTypes::TYPE_NULL) });
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 0);
}

// A METADATA column (IsRawColumn() == false) is NEVER one field: DescribeColumnLayout always emits the
// `_TYPE` variant discriminator plus one slot per primitive the type descriptor admits (a string-only
// descriptor -> `_TYPE`, `_S`). The `In` render therefore binds every value through the write-spread codec,
// so the _TYPE tag is compared too (load-bearing for a variant — a native IN on one primitive field alone
// would match rows of the wrong variant — and for a reference, whose constant must be the encoded _RRRef
// blob, not a bare ibConst). Values of one kind on a single-primitive column spell the tag alike: it is said
// once and the values go into one native IN (DecomposeIn); anything else is OR-folded, pair by pair.
//
// The fixture is built FROM the layout (ColumnFieldNames, in bind order) rather than hardcoding the suffix
// table, and the discriminator is seeded through ibPersistedTypeTag — so the test asserts against the same
// contract the codec writes by, instead of duplicating it. If the _TYPE seed were wrong, this test would
// fail: the folded predicate compares it.
namespace {

struct MetaColFixture {
	std::vector<ibString> fields;   // [0] = _TYPE, [1] = _S (bind order)
	ibString              typeField;
	ibString              strField;
};

MetaColFixture MakeMetaColTable(ibDatabaseLayerSQLite& db, const ibString& table,
                                const std::shared_ptr<const ibBackendQueryColumn>& col,
                                const std::vector<std::pair<ibString, long>>& rows)
{
	MetaColFixture f;
	f.fields = ColumnFieldNames(*col);
	if (f.fields.size() != 2)
		return f;                       // caller asserts — the layout changed shape
	f.typeField = f.fields[0];
	f.strField  = f.fields[1];

	const ibString ddl = wxT("CREATE TABLE ") + table + wxT(" (") + f.typeField + wxT(" INTEGER, ")
	                   + f.strField + wxT(" TEXT, qty INTEGER)");
	db.RunQuery(wxT("%s"), ddl);   // as a %s ARGUMENT — the statement is assembled, not a format string

	const int strTag = ibPersistedTypeTag(ibColumnRole::String);
	for (const auto& r : rows) {
		const ibString ins = ibString::Format(
			wxT("INSERT INTO %s (%s, %s, qty) VALUES (%d, '%s', %ld)"),
			table, f.typeField, f.strField, strTag, r.first, r.second);
		db.RunQuery(wxT("%s"), ins);
	}
	return f;
}

} // namespace

// =============================================================================
// A CELL WHOSE TAG THE RESULT DOES NOT CARRY reads as the column's TYPED EMPTY value, and `false`.
//
// That is ibColumnCodec::ReadValue's documented answer to "field not in the result set": a caller that
// projected one field of a metadata column (a MAX over its date, say) and read it back through the column.
//
// 🛑 It was an ACCESS VIOLATION in a process with no active application. The codec built the empty value
// with AdjustValue(type) and left the metadata it had been handed behind, so AdjustValue went to
// `activeMetaData->` - which a headless tool before it opens a base, and every test here, does not have.
// Where an application IS active the same line quietly used THAT one instead of the caller's - wrong for a
// second application held beside it (compare / merge, an external data tool). Found through a sequence's
// border, whose retreat read a lone MAX this way: on a file base it lost the border, under a test it crashed.
// =============================================================================
TEST_F(ComputedServerFix, Codec_ACellWhoseTagIsNotInTheResultReadsAsTheTypedEmptyValue)
{
	if (!ready) return;

	const auto region = std::make_shared<TypedCol>(wxT("region"), 370, ibTypeDescription(g_valueStringCLSID));
	const MetaColFixture f = MakeMetaColTable(*db, wxT("m7"), region, { { wxT("North"), 10 } });
	ASSERT_EQ(f.fields.size(), 2u);

	// One field of the column's two: the value, with no `_TYPE` beside it.
	ibDatabaseQueryBuilder q(ibConnectionPool::ThreadHolder());
	q.From(ibScan(wxT("m7"), wxT("r")));
	q.Project({ ibQueryProjItem{ ibFunc(wxT("MAX"), { ibCol(wxT("r"), f.strField) }), f.strField } });
	ibQueryResult rs = q.Execute();
	ASSERT_TRUE(rs.Next());

	ibValue value(ibNumber(7));   // anything but what it must become
	bool read = true;
	ASSERT_NO_THROW(read = ibColumnCodec::ReadValue(region->GetPhysicalName(), *region, /*metaData*/ nullptr, value, rs));
	EXPECT_FALSE(read) << "the cell was not read - the caller is told so";
	EXPECT_EQ(value.GetType(), ibValueTypes::TYPE_STRING) << "the column's typed empty value, not what was there before";
	EXPECT_TRUE(value.GetString().IsEmpty());
}

TEST_F(ComputedServerFix, In_MetadataColumnFoldsCompositeEquality)
{
	if (!ready) return;

	const auto region = std::make_shared<TypedCol>(wxT("region"), 320, ibTypeDescription(g_valueStringCLSID));
	const MetaColFixture f = MakeMetaColTable(*db, wxT("m1"), region,
		{ { wxT("North"), 10 }, { wxT("South"), 5 }, { wxT("East"), 3 } });
	ASSERT_EQ(f.fields.size(), 2u) << "a string-only metadata column is expected to spread as _TYPE + _S; "
	                                  "a different layout needs this fixture reworked";

	const std::shared_ptr<const ibBackendColumnRawDB> qty = ibBackendColumnRawDB::Number(wxT("qty"));
	const auto src = std::make_shared<PhysicalQ>(wxT("m1"), 321);
	src->AddCol(region);
	src->AddCol(qty);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(src);
	q.Select(qty, wxT("qty"));
	q.WhereIn(region, { ibValue(ibString(wxT("North"))), ibValue(ibString(wxT("East"))) });
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 2);   // North + East: the tag once, the two values in one IN
}

// Values of two kinds do not share a tag, so they go pair by pair — and a number matches no string row.
TEST_F(ComputedServerFix, In_MetadataColumnValuesOfTwoKindsFoldPairByPair)
{
	if (!ready) return;

	const auto region = std::make_shared<TypedCol>(wxT("region"), 322, ibTypeDescription(g_valueStringCLSID));
	const MetaColFixture f = MakeMetaColTable(*db, wxT("m3"), region,
		{ { wxT("North"), 10 }, { wxT("South"), 5 } });
	ASSERT_EQ(f.fields.size(), 2u);

	const std::shared_ptr<const ibBackendColumnRawDB> qty = ibBackendColumnRawDB::Number(wxT("qty"));
	const auto src = std::make_shared<PhysicalQ>(wxT("m3"), 323);
	src->AddCol(region);
	src->AddCol(qty);

	ibDataQueryBuilder q(ibConnectionPool::ThreadHolder());
	q.From(src);
	q.Select(qty, wxT("qty"));
	q.WhereIn(region, { ibValue(ibString(wxT("North"))), ibValue(ibNumber(10)) });
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 1);
}

// The metadata-column fold must observe the empty set too — this is the branch where an empty OR-fold comes
// back as a NULL expression, i.e. NO predicate, i.e. the WHOLE table. Guarded explicitly in the render; this
// is the test that catches the guard being removed.
TEST_F(ComputedServerFix, In_MetadataColumnEmptySetReturnsNoRows)
{
	if (!ready) return;

	const auto region = std::make_shared<TypedCol>(wxT("region"), 330, ibTypeDescription(g_valueStringCLSID));
	const MetaColFixture f = MakeMetaColTable(*db, wxT("m2"), region,
		{ { wxT("North"), 10 }, { wxT("South"), 5 } });
	ASSERT_EQ(f.fields.size(), 2u);

	const std::shared_ptr<const ibBackendColumnRawDB> qty = ibBackendColumnRawDB::Number(wxT("qty"));
	const auto src = std::make_shared<PhysicalQ>(wxT("m2"), 331);
	src->AddCol(region);
	src->AddCol(qty);

	ibDatabaseConnectionHolder* holder = ibConnectionPool::ThreadHolder();
	ASSERT_NE(holder, nullptr);

	ibDataQueryBuilder q(holder);
	q.From(src);
	q.Select(qty, wxT("qty"));
	q.WhereIn(region, {});
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 0) << "an empty OR-fold must not degrade to 'no predicate' (the whole table)";
}

// =============================================================================
// A SET OF REFERENCES (DecomposeIn, through the door that writes every WHERE).
//
// A reference is three fields — its type tag, its table, its id — and a list of items of ONE catalog spells
// the first two alike. Said pair by pair, a hundred items were a hundred `(tag = ? AND table = ? AND id = ?)`
// branches weighed one at a time against every row, folded as a chain as deep as the list is long (which the
// renderer walks recursively: some 155 references ran a checked build out of stack). No database is needed:
// a catalog in an application in memory is enough to mint references of it.
// =============================================================================
namespace {

class MetaPhysicalQ : public PhysicalQ {
public:
	MetaPhysicalQ(const ibString& table, ibMetaID id, const ibMetaData* metaData) : PhysicalQ(table, id), m_metaData(metaData) {}
	const ibMetaData* GetMetaData() const override { return m_metaData; }
private:
	const ibMetaData* m_metaData;
};

struct ReferenceSetFix {
	ibMetaDataApplicationFile cfg;
	ibValueMetaObject* items = nullptr;
	ibValueMetaObject* units = nullptr;

	ReferenceSetFix() {
		ibValueMetaObjectApplication* root = cfg.GetRootMetaObject();
		if (root == nullptr) return;
		items = cfg.CreateMetaObject(g_metaCatalogCLSID, root, /*runObject*/ false);
		units = cfg.CreateMetaObject(g_metaCatalogCLSID, root, /*runObject*/ false);
	}
	bool Ready() const { return items != nullptr && units != nullptr; }

	ibValue RefTo(const ibValueMetaObject* catalog) const {
		return ibValue(ibValueReferenceDataObject::Create(&cfg, catalog->GetMetaID(), ibGuid(ibGuid::newGuid())));
	}
	// `column IN (values)` over a table of this application, as the SQL the door writes for it.
	ibString SqlOfSet(const std::shared_ptr<const ibBackendQueryColumn>& column, ibMetaID tableId, const std::vector<ibValue>& values) const {
		const auto table = std::make_shared<MetaPhysicalQ>(wxT("T"), tableId, &cfg);
		table->AddCol(column);
		ibQueryCondition set;
		set.m_col    = column;
		set.m_op     = ibQueryFilterOp::In;
		set.m_values = values;
		const ibQueryExprPtr predicate = ibDbTableProvider::BuildPredicateIR(table, ibQueryPredicate::Leaf(set));
		if (!predicate) return ibString();
		ibDatabaseQueryBuilder q;
		q.From(wxT("T")).Where(predicate);
		return ibQueryRenderer(ibDatabaseLayerSQLite::Dialect()).Render(q.Build()).m_sql;
	}
	static int CountOf(const ibString& haystack, const ibString& needle) {
		int n = 0;
		for (size_t at = 0; (at = haystack.find(needle, at)) != ibString::npos; at += needle.length()) ++n;
		return n;
	}
};

} // namespace

TEST_F(ComputedServerFix, In_ReferencesOfOneTableSayTheTableOnce)
{
	if (!ready) return;
	ReferenceSetFix f;
	ASSERT_TRUE(f.Ready());

	const auto item = std::make_shared<TypedCol>(wxT("item"), 340, ibTypeDescription(reference_to_clsid(f.items->GetMetaID(), clsid_metaclass(f.items->GetClassType()))));
	const std::vector<ibString> fields = ColumnFieldNames(*item);
	ASSERT_EQ(fields.size(), 3u) << "a single-target reference is expected to spread as _TYPE + _RTRef + _RRRef";

	std::vector<ibValue> values;
	for (int i = 0; i < 100; ++i)
		values.push_back(f.RefTo(f.items));

	const ibString sql = f.SqlOfSet(item, 343, values);
	ASSERT_FALSE(sql.IsEmpty());
	EXPECT_EQ(ReferenceSetFix::CountOf(sql, wxT(" IN (")), 1) << sql;
	EXPECT_EQ(ReferenceSetFix::CountOf(sql, wxT(" OR ")), 0) << sql;
	EXPECT_EQ(ReferenceSetFix::CountOf(sql, fields[0] + wxT(" = ")), 1) << "the type tag, once";
	EXPECT_EQ(ReferenceSetFix::CountOf(sql, fields[1] + wxT(" = ")), 1) << "the table, once";
	EXPECT_TRUE(sql.Contains(fields[2] + wxT(" IN (")));
}

// References of TWO tables do not agree on the table — so each table says itself once over its own ids, and the
// tables are ORed: one IN per table (DecomposeIn, 2026-10-05). It was pair by pair, a thousand ORs for a thousand
// references, where a walk filter's set of a few thousand documents is the ordinary case.
TEST_F(ComputedServerFix, In_ReferencesOfTwoTablesSayEachTableOnce)
{
	if (!ready) return;
	ReferenceSetFix f;
	ASSERT_TRUE(f.Ready());

	ibTypeDescription both(reference_to_clsid(f.items->GetMetaID(), clsid_metaclass(f.items->GetClassType())));
	both.AppendMetaType(reference_to_clsid(f.units->GetMetaID(), clsid_metaclass(f.units->GetClassType())));
	const auto subject = std::make_shared<TypedCol>(wxT("subject"), 341, both);

	std::vector<ibValue> values;
	for (int i = 0; i < 1000; ++i)
		values.push_back(f.RefTo(i % 2 ? f.items : f.units));

	const ibString sql = f.SqlOfSet(subject, 344, values);
	ASSERT_FALSE(sql.IsEmpty());
	EXPECT_EQ(ReferenceSetFix::CountOf(sql, wxT(" IN (")), 2) << "one IN per table: " << sql.Left(400);
	EXPECT_EQ(ReferenceSetFix::CountOf(sql, wxT(" OR ")), 1) << "the two tables, ORed";
}

// 🛑 AN EMPTY VALUE IN THE LIST, AND THE WHOLE LIST GOES PAIR BY PAIR. Its equality is its own
// (DecomposeEquality): UNDEFINED is the tag alone, written as the row holds it — `_TYPE = 0`, no NULL beside it
// since a field holds its DEFAULT (2026-11-18) — which no IN of one table's ids can say.
TEST_F(ComputedServerFix, In_AnEmptyReferenceAmongTheValuesGoesPairByPair)
{
	if (!ready) return;
	ReferenceSetFix f;
	ASSERT_TRUE(f.Ready());

	const auto item = std::make_shared<TypedCol>(wxT("item"), 342, ibTypeDescription(reference_to_clsid(f.items->GetMetaID(), clsid_metaclass(f.items->GetClassType()))));
	const std::vector<ibValue> values{ f.RefTo(f.items), f.RefTo(f.items), ibValue(), f.RefTo(f.items) };

	const ibString sql = f.SqlOfSet(item, 345, values);
	ASSERT_FALSE(sql.IsEmpty());
	EXPECT_EQ(ReferenceSetFix::CountOf(sql, wxT(" IN (")), 0) << sql;
	EXPECT_EQ(ReferenceSetFix::CountOf(sql, wxT(" IS NULL")), 0) << sql;
}

// ⭐ AN EQUALITY WITH AN EMPTY VALUE IS THE SPELLING IT IS WRITTEN IN (2026-11-18). A value has one spelling in a
// row — the codec's write, a field's DEFAULT, a type change writing the empty value — so `= <empty reference>` is the
// zero key and `= False` the tag and the false, exactly as the GROUP BY of a level tells the rows apart: no NULL
// beside them, no OR. (A drill into a list's heading scopes its rows by this equality.)
TEST_F(ComputedServerFix, Equal_AnEmptyValueIsTheSpellingItIsWritten)
{
	if (!ready) return;
	ReferenceSetFix f;
	ASSERT_TRUE(f.Ready());

	const auto item = std::make_shared<TypedCol>(wxT("item"), 346, ibTypeDescription(reference_to_clsid(f.items->GetMetaID(), clsid_metaclass(f.items->GetClassType()))));
	const auto flag = std::make_shared<TypedCol>(wxT("flag"), 347, ibTypeDescription(g_valueBooleanCLSID));
	const auto sqlOf = [&f](const std::shared_ptr<const ibBackendQueryColumn>& column, const ibValue& value) {
		const auto table = std::make_shared<MetaPhysicalQ>(wxT("T"), 348, &f.cfg);
		table->AddCol(column);
		ibQueryCondition equal;
		equal.m_col   = column;
		equal.m_value = value;
		const ibQueryExprPtr predicate = ibDbTableProvider::BuildPredicateIR(table, ibQueryPredicate::Leaf(equal));
		if (!predicate) return ibString();
		ibDatabaseQueryBuilder q;
		q.From(wxT("T")).Where(predicate);
		return ibQueryRenderer(ibDatabaseLayerSQLite::Dialect()).Render(q.Build()).m_sql;
	};

	const ibString emptyRef = sqlOf(item, ibValue(ibValueReferenceDataObject::Create(&f.cfg, f.items->GetMetaID(), ibGuid())));
	ASSERT_FALSE(emptyRef.IsEmpty());
	EXPECT_EQ(ReferenceSetFix::CountOf(emptyRef, wxT(" IS NULL")), 0) << emptyRef;
	EXPECT_EQ(ReferenceSetFix::CountOf(emptyRef, wxT(" OR ")), 0) << emptyRef;

	const ibString isFalse = sqlOf(flag, ibValue(false));
	ASSERT_FALSE(isFalse.IsEmpty());
	EXPECT_EQ(ReferenceSetFix::CountOf(isFalse, wxT(" IS NULL")), 0) << isFalse;
	EXPECT_EQ(ReferenceSetFix::CountOf(isFalse, wxT(" OR ")), 0) << isFalse;

	// …and UNDEFINED in a composite is the tag 0 it is written as (beside a NULL key), which the column reads back as
	// Undefined: asked of the key, a ledger's unused dimension opened at its heading found nothing (2026-11-19).
	ibTypeDescription both(reference_to_clsid(f.items->GetMetaID(), clsid_metaclass(f.items->GetClassType())));
	both.AppendMetaType(reference_to_clsid(f.units->GetMetaID(), clsid_metaclass(f.units->GetClassType())));
	const auto subject = std::make_shared<TypedCol>(wxT("subject"), 349, both);
	const ibString undefined = sqlOf(subject, ibValue());
	ASSERT_FALSE(undefined.IsEmpty());
	EXPECT_EQ(ReferenceSetFix::CountOf(undefined, wxT("_TYPE")), 1) << undefined;
	EXPECT_EQ(ReferenceSetFix::CountOf(undefined, wxT("_RRRef")), 0) << undefined;
}
