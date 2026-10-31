// A removed type is cleared, not converted. The apply's ledger says how many stored values that
// empties, counted on the predicate the clear itself runs, and exactly those rows read empty after.
//
// Driven through DiffSnapshots — the restructuring itself — on a table the first diff creates, so
// the count and the clear meet a real engine at the moment of the apply.

#include <gtest/gtest.h>

#include <wx/filename.h>
#include <wx/init.h>
#include <wx/stdpaths.h>

#include <chrono>
#include <cstdlib>
#include <map>
#include <string>

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#ifdef OES_USE_FIREBIRD
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#endif
#ifdef OES_USE_POSTGRESQL
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#endif
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/query/columnLayout.h"
#include "backend/query/queryColumn.h"
#include "backend/query/schemaBuilder.h"
#include "backend/query/schemaSnapshot.h"
#include "backend/restructureInfo.h"
#include "core/clsid.h"

namespace {

constexpr ibMetaID kGoodsTable = 0x7100;

struct StoredColumn : ibBackendQueryColumn {
	ibString                  physical;
	mutable ibTypeDescription type;

	ibString GetName() const override { return physical; }
	ibString GetPhysicalName() const override { return physical; }
	ibTypeDescription& GetTypeDesc() const override { return type; }
	ibMetaID GetColumnId() const override { return 1; }
};

ibString FieldOf(const StoredColumn& column, ibColumnRole role)
{
	for (const ibColumnSlot& slot : DescribeColumnLayout(&column))
		if (slot.m_role == role)
			return slot.m_name;
	return ibString();
}

ibString EnvOr(const char* name, const ibString& fallback)
{
	const char* value = std::getenv(name);
	return (value != nullptr && *value != '\0') ? ibString::FromUTF8(value) : fallback;
}

std::shared_ptr<ibDatabaseLayer> OpenEngine(const std::string& engine)
{
	const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	const ibString scratch = wxFileName(wxStandardPaths::Get().GetTempDir(),
		ibString::Format(wxT("oes_ledger_%s_%lld.%s"), engine, (long long)now, engine == "firebird" ? "fdb" : "db")).GetFullPath();
	if (engine == "sqlite") {
		auto db = std::make_shared<ibDatabaseLayerSQLite>();
		return db->Open(scratch) ? db : nullptr;
	}
	if (engine == "postgres") {
#ifdef OES_USE_POSTGRESQL
		if (std::getenv("OES_PG_USER") == nullptr || *std::getenv("OES_PG_USER") == '\0')
			return nullptr;
		auto db = std::make_shared<ibDatabaseLayerPostgres>();
		if (!db->Open(EnvOr("OES_PG_HOST", wxT("127.0.0.1")), EnvOr("OES_PG_PORT", wxT("5432")), wxT("oes_ledger"),
		              EnvOr("OES_PG_USER", wxT("postgres")), EnvOr("OES_PG_PASSWORD", wxEmptyString)))
			return nullptr;
		db->RunQuery(wxT("%s"), wxT("DROP SCHEMA IF EXISTS public CASCADE"));
		db->RunQuery(wxT("%s"), wxT("CREATE SCHEMA public"));
		if (db->IsActiveTransaction())
			db->Commit();
		return db;
#else
		return nullptr;
#endif
	}
#ifdef OES_USE_FIREBIRD
	try {
		auto db = std::make_shared<ibDatabaseLayerFirebird>();
		db->SetUser(wxT("SYSDBA"));
		db->SetPassword(wxT("masterkey"));
		return db->Open(scratch) ? db : nullptr;
	}
	catch (...) {
		return nullptr;
	}
#else
	return nullptr;
#endif
}

void Settle()
{
	if (db_query->IsActiveTransaction())
		db_query->Commit();
	ibSchemaBuilder(nullptr).Flush();
	if (db_query->IsActiveTransaction())
		db_query->Commit();
}

class TypeChangeLedger : public ::testing::TestWithParam<const char*> {
protected:
	wxInitializer m_wx;

	void SetUp() override
	{
		if (!m_wx.IsOk())
			GTEST_SKIP() << "wxBase init failed";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		ASSERT_NE(pool, nullptr);
		std::shared_ptr<ibDatabaseLayer> db = OpenEngine(GetParam());
		if (!db)
			GTEST_SKIP() << GetParam() << " is not available";
		pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);
	}

	void TearDown() override
	{
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}

	// The table as `was` declares it, created by the differ itself.
	static void Create(const StoredColumn& was)
	{
		ibSchemaSnapshot before;
		before.Shared(kGoodsTable, wxT("goods")).Add(&was);
		ASSERT_EQ(DiffSnapshots(nullptr, before), 1);
		Settle();
	}

	// was -> next, the way an apply runs it. The ledger lines that say a loss.
	static std::vector<ibString> Apply(const StoredColumn& was, const StoredColumn& next)
	{
		ibSchemaSnapshot before;
		before.Shared(kGoodsTable, wxT("goods")).Add(&was);
		ibSchemaSnapshot after;
		after.Shared(kGoodsTable, wxT("goods")).Add(&next);
		ibRestructureInfo ledger;
		EXPECT_EQ(DiffSnapshots(&before, after, nullptr, &ledger), 1);
		Settle();
		std::vector<ibString> said;
		for (const ibRestructureInfo::Entry& entry : ledger)
			if (entry.descr.Find(wxT("stored values will be cleared")) != wxNOT_FOUND)
				said.push_back(entry.descr);
		return said;
	}

	static void Put(const ibString& columns, const ibString& values)
	{
		ASSERT_GE(db_query->RunQuery(wxT("%s"),
			wxT("INSERT INTO goods (") + columns + wxT(") VALUES (") + values + wxT(")")), 0);
		if (db_query->IsActiveTransaction())
			db_query->Commit();
	}

	// How many rows hold each tag (0 = cleared).
	static std::map<int, int> TagCounts(const ibString& typeCol)
	{
		std::map<int, int> counts;
		ibQueryResult rows = ibDatabaseQueryBuilder().From(wxT("goods")).Select({ typeCol }).Execute();
		while (rows.Next())
			++counts[rows.IsResultNull(typeCol) ? 0 : rows.GetResultInt(typeCol)];
		return counts;
	}
};

} // namespace

TEST_P(TypeChangeLedger, ARemovedTypeIsCountedAndExactlyThoseRowsReadEmpty)
{
	StoredColumn was;
	was.physical = wxT("Code");
	was.type.AppendMetaType(ibValueTypes::TYPE_STRING);
	was.type.AppendMetaType(ibValueTypes::TYPE_NUMBER);
	StoredColumn next = was;
	next.type.ClearMetaType(ibValue::GetIDByVT(ibValueTypes::TYPE_STRING));

	Create(was);
	const ibString typeCol = FieldOf(was, ibColumnRole::Discriminator);
	const int stringTag = ibPersistedTypeTag(ibColumnRole::String);
	const int numberTag = ibPersistedTypeTag(ibColumnRole::Number);
	Put(typeCol, ibString::Format(wxT("%d"), stringTag));
	Put(typeCol, ibString::Format(wxT("%d"), stringTag));
	Put(typeCol, ibString::Format(wxT("%d"), numberTag));

	const std::vector<ibString> said = Apply(was, next);
	ASSERT_EQ(said.size(), 1u);
	EXPECT_NE(said[0].Find(wxT(": 2 stored values will be cleared")), wxNOT_FOUND) << said[0];

	const std::map<int, int> tags = TagCounts(typeCol);
	EXPECT_EQ(tags.count(0) ? tags.at(0) : 0, 2);
	EXPECT_EQ(tags.count(numberTag) ? tags.at(numberTag) : 0, 1);
}

// _RTRef outlives the reference it named: a row that holds a number now may still carry the clsid
// of the reference it held before. Removing that reference target must not touch the number.
TEST_P(TypeChangeLedger, ARemovedTargetLeavesANumberThatStillCarriesItsClsid)
{
	const ibClassID kept = reference_to_clsid(11);
	const ibClassID gone = reference_to_clsid(12);
	StoredColumn was;
	was.physical = wxT("Item");
	was.type.AppendMetaType(kept);
	was.type.AppendMetaType(gone);
	was.type.AppendMetaType(ibValueTypes::TYPE_NUMBER);
	StoredColumn next = was;
	next.type.ClearMetaType(gone);

	Create(was);
	const ibString typeCol = FieldOf(was, ibColumnRole::Discriminator);
	const ibString targetCol = FieldOf(was, ibColumnRole::ReferenceType);
	ASSERT_FALSE(targetCol.IsEmpty());
	const int refTag = ibPersistedTypeTag(ibColumnRole::ReferenceType);
	const int numberTag = ibPersistedTypeTag(ibColumnRole::Number);
	const ibString columns = typeCol + wxT(", ") + targetCol;
	Put(columns, ibString::Format(wxT("%d, %lld"), refTag, (long long)kept));
	Put(columns, ibString::Format(wxT("%d, %lld"), refTag, (long long)gone));
	Put(columns, ibString::Format(wxT("%d, %lld"), numberTag, (long long)gone));   // a number over a stale clsid

	const std::vector<ibString> said = Apply(was, next);
	ASSERT_EQ(said.size(), 1u);
	EXPECT_NE(said[0].Find(wxT(": 1 stored values will be cleared")), wxNOT_FOUND) << said[0];

	const std::map<int, int> tags = TagCounts(typeCol);
	EXPECT_EQ(tags.count(0) ? tags.at(0) : 0, 1);
	EXPECT_EQ(tags.count(refTag) ? tags.at(refTag) : 0, 1);
	EXPECT_EQ(tags.count(numberTag) ? tags.at(numberTag) : 0, 1) << "the number lost its tag to a clsid it no longer holds";
}

// An added type changes the shape and empties nothing.
TEST_P(TypeChangeLedger, AnAddedTypeClearsNothing)
{
	StoredColumn was;
	was.physical = wxT("Code");
	was.type.AppendMetaType(ibValueTypes::TYPE_STRING);
	StoredColumn next = was;
	next.type.AppendMetaType(ibValueTypes::TYPE_NUMBER);

	Create(was);
	Put(FieldOf(was, ibColumnRole::Discriminator), ibString::Format(wxT("%d"), ibPersistedTypeTag(ibColumnRole::String)));

	EXPECT_TRUE(Apply(was, next).empty());
}

TEST_P(TypeChangeLedger, AThousandIsGrouped)
{
	StoredColumn was;
	was.physical = wxT("Code");
	was.type.AppendMetaType(ibValueTypes::TYPE_STRING);
	was.type.AppendMetaType(ibValueTypes::TYPE_NUMBER);
	StoredColumn next = was;
	next.type.ClearMetaType(ibValue::GetIDByVT(ibValueTypes::TYPE_STRING));

	Create(was);
	const ibString typeCol = FieldOf(was, ibColumnRole::Discriminator);
	const int stringTag = ibPersistedTypeTag(ibColumnRole::String);
	for (int i = 0; i < 1234; ++i)
		ASSERT_GE(db_query->RunQuery(wxT("%s"),
			ibString::Format(wxT("INSERT INTO goods (%s) VALUES (%d)"), typeCol, stringTag)), 0);
	if (db_query->IsActiveTransaction())
		db_query->Commit();

	const std::vector<ibString> said = Apply(was, next);
	ASSERT_EQ(said.size(), 1u);
	EXPECT_NE(said[0].Find(wxT(": 1 234 stored values will be cleared")), wxNOT_FOUND) << said[0];
}

INSTANTIATE_TEST_SUITE_P(Engines, TypeChangeLedger, ::testing::Values("sqlite", "postgres", "firebird"));
