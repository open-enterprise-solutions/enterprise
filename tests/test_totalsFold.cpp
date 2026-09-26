// =============================================================================
// THE SHARD FOLD — ibDerivedState::Collapse, run against a live engine (in-memory SQLite).
//
// A split totals table keeps one key in several rows, one per writing connection, and the fold moves them
// back into one. It moves figures between rows; it must never change one. On 2026-09-26 it did: a ledger
// whose currency was added after its first postings held one July key in four rows - the empty currency
// stored untagged in two of them and as an empty reference in the other two - and one pass of the totals
// job turned its 5 600 into 11 200. The fold read the key as VALUES (both spellings are "not filled", and
// read the same) and aimed its writes by value, so an add meant for one row landed on a row of the other
// spelling.
//
// The table keeps the two spellings apart - the trigger matches a key field by field - and the fold now
// works where the trigger works: on the physical fields, one row named by all of them. The register below
// is declared by ContributeTables exactly as a configuration declares one; only the split is set here.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/init.h>

#include <vector>

#include "backend/appData.h"
#include "backend/clsid.h"
#include "backend/compiler/value.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/connectionHolder.h"
#include "backend/databaseLayer/databaseMaterializeBuilder.h"   // ShardColumnName
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/dimension/metaDimensionObject.h"
#include "backend/metaCollection/partial/accumulationRegister.h"
#include "backend/query/columnLayout.h"
#include "backend/query/derivedStateBuilder.h"
#include "backend/query/queryColumn.h"
#include "backend/query/queryable.h"
#include "backend/query/schemaSnapshot.h"

namespace {

struct TotalsFoldFix : ::testing::Test {
	wxInitializer                          m_wxInit;   // wxBase up before appData
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	bool ready = false;

	// An accumulation register with one dimension - a reference to a catalog, the shape the ledger's currency
	// has - and one resource, its totals split in four.
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObject*          catalog = nullptr;
	ibSchemaSnapshot            snapshot;
	ibSchemaTable               totals;
	std::vector<ibColumnSlot>   keySlots;   // the dimension's physical fields: tag, table, key

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

		ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
		ASSERT_NE(root, nullptr);
		catalog = cfg.CreateMetaObject(g_metaCatalogCLSID, root, /*runObject*/ false);
		auto* reg = dynamic_cast<ibValueMetaObjectAccumulationRegister*>(
			cfg.CreateMetaObject(g_metaAccumulationRegisterCLSID, root, /*runObject*/ false));
		ASSERT_NE(catalog, nullptr);
		ASSERT_NE(reg, nullptr);
		auto* dimension = dynamic_cast<ibValueMetaObjectDimension*>(cfg.CreateMetaObject(g_metaDimensionCLSID, reg, false));
		ASSERT_NE(dimension, nullptr);
		dimension->GetTypeDesc().SetDefaultMetaType(reference_to_clsid(catalog->GetMetaID()));
		cfg.CreateMetaObject(g_metaResourceCLSID, reg, false);

		reg->ContributeTables(snapshot);
		for (const ibSchemaTable& t : snapshot.Tables())
			if (t.m_derived && !t.m_materialize.m_views.empty())
				totals = t;
		ASSERT_TRUE(totals.m_derived) << "the register declared no totals table";
		ASSERT_NE(totals.m_queryable, nullptr);
		ASSERT_EQ(totals.m_materialize.m_keys.size(), 1u) << "one dimension, one key column";
		totals.m_materialize.Split(4);

		keySlots = DescribeColumnLayout(totals.m_materialize.m_keys.front());
		ASSERT_EQ(keySlots.size(), 3u) << "a single-target reference spreads as _TYPE + _RTRef + _RRRef";

		// The table itself, from the declaration's own fields.
		std::vector<ibDdlColumn> columns;
		const auto add = [&columns](const ibBackendQueryColumn* col) {
			for (const ibColumnSlot& slot : DescribeColumnLayout(col))
				columns.push_back(ibDdlColumn{ slot.m_name, slot.m_type });
		};
		add(totals.m_queryable->ResolveColumnByName(totals.m_materialize.m_periodColumn));
		add(totals.m_materialize.m_keys.front());
		columns.push_back(ibDdlColumn{ ShardColumnName(), ibTypeInteger() });
		for (const ibSchemaDelta& d : totals.m_materialize.m_deltas)
			add(d.m_column);
		ASSERT_GE(Holder().Execute(ibCreateTable(totals.m_name, columns)), 0);
		ready = true;
	}
	void TearDown() override {
		if (ibApplicationData::Get() != nullptr)
			ibApplicationData::DestroyAppDataEnv();
	}

	static ibDatabaseQueryBuilder Holder() { return ibDatabaseQueryBuilder(ibConnectionPool::ThreadHolder()); }

	wxString Field(ibColumnRole role) const {
		for (const ibColumnSlot& slot : keySlots)
			if (slot.m_role == role)
				return slot.m_name;
		return wxString();
	}

	// One stored row of the key: the dimension empty, spelled `typed` (an empty reference: the reference tag,
	// the catalog's table, sixteen zero bytes) or untagged (tag 0, nothing beside it) - the two ways the
	// ledger held it. The figure goes into the first accumulation.
	void Put(bool typed, long long shard, long figure) {
		static const unsigned char zeroKey[16] = {};
		std::vector<ibDmlAssign> row;
		row.push_back({ totals.m_materialize.m_periodColumn, ibConst(ibValue(wxDateTime(1, wxDateTime::Jan, 2020))) });
		row.push_back({ Field(ibColumnRole::Discriminator),
			ibConst(ibValue(static_cast<int>(typed ? ibFieldTypes_Reference : ibFieldTypes_Empty))) });
		row.push_back({ Field(ibColumnRole::ReferenceType),
			typed ? ibConst(ibValue(ibNumber(static_cast<long long>(reference_to_clsid(catalog->GetMetaID()))))) : ibConst(ibValue()) });
		row.push_back({ Field(ibColumnRole::ReferenceId), typed ? ibConstBlob(zeroKey, sizeof(zeroKey)) : ibConst(ibValue()) });
		row.push_back({ ShardColumnName(), ibConst(ibValue(ibNumber(shard))) });
		for (size_t n = 0; n < totals.m_materialize.m_deltas.size(); n++)
			row.push_back({ totals.m_materialize.m_deltas[n].m_column->GetPhysicalName(), ibConst(ibValue(ibNumber(n == 0 ? figure : 0L))) });
		ASSERT_EQ(Holder().Execute(ibInsert(totals.m_name, row)), 1);
	}

	// How many rows, and what figure, the table holds for one spelling (by its tag) - or for all of them.
	struct Held { long long m_rows = 0; ibNumber m_figure; };
	Held Holds(const ibFieldTypes* tag) const {
		ibDatabaseQueryBuilder q = Holder();
		q.From(totals.m_name);
		q.Project({ ibQueryProjItem{ ibFunc(wxT("COUNT"), { ibCol(ShardColumnName()) }), wxT("n_") },
		            ibQueryProjItem{ ibFunc(wxT("SUM"), { ibCol(totals.m_materialize.m_deltas.front().m_column->GetPhysicalName()) }), wxT("s_") } });
		if (tag != nullptr)
			q.Where(ibBinOp(ibQueryBinOp::Eq, ibCol(Field(ibColumnRole::Discriminator)), ibConst(ibValue(static_cast<int>(*tag)))));
		ibQueryResult r = q.Execute();
		Held held;
		if (r.Next()) {
			held.m_rows = r.GetResultLong(wxT("n_"));
			held.m_figure = r.IsResultNull(wxT("s_")) ? ibNumber() : r.GetResultNumber(wxT("s_"));
		}
		return held;
	}
};

const ibFieldTypes kTyped    = ibFieldTypes_Reference;
const ibFieldTypes kUntagged = ibFieldTypes_Empty;

} // namespace

// The key of the 2026-09-26 journal, row for row: 5 600 in all, 0 under the untagged spelling and 5 600
// under the empty reference. Folded, each spelling is ONE row holding its own figure, and the key still
// holds 5 600.
TEST_F(TotalsFoldFix, AnEmptyKeyStoredTwoWaysFoldsEachWayIntoOneRowAndKeepsItsFigure)
{
	if (!ready) return;
	Put(/*typed*/ false, 0, 5600);
	Put(/*typed*/ true,  1, 0);
	Put(/*typed*/ true,  2, 5600);
	Put(/*typed*/ false, 2, -5600);

	ASSERT_TRUE(ibDerivedState::Collapse(totals, ibConnectionPool::ThreadHolder()));

	EXPECT_EQ(Holds(nullptr).m_figure, ibNumber(5600L)) << "a fold moves figures; it never changes one";
	const Held typed = Holds(&kTyped), untagged = Holds(&kUntagged);
	EXPECT_EQ(typed.m_rows, 1) << "the empty references are one key, folded into one row";
	EXPECT_EQ(typed.m_figure, ibNumber(5600L));
	EXPECT_EQ(untagged.m_rows, 1) << "…and so are the untagged cells";
	EXPECT_EQ(untagged.m_figure, ibNumber(0L));
}

// Two physical rows under one key AND one shard - the trigger never makes that, but a NULL in the key is a row
// a unique index does not guard - and no WHERE can name one of them. The key is left as it is: a split key
// reads exactly right, and the next pass costs nothing more.
TEST_F(TotalsFoldFix, TwoRowsUnderOneShardLeaveTheKeyAsItIs)
{
	if (!ready) return;
	Put(/*typed*/ false, 1, 100);
	Put(/*typed*/ false, 1, 50);
	Put(/*typed*/ false, 2, 10);

	ASSERT_TRUE(ibDerivedState::Collapse(totals, ibConnectionPool::ThreadHolder()));

	const Held untagged = Holds(&kUntagged);
	EXPECT_EQ(untagged.m_rows, 3);
	EXPECT_EQ(untagged.m_figure, ibNumber(160L));
}
