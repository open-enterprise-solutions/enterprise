// A removed type is cleared, not converted. The apply's ledger says how many stored values
// that empties, counted with the same predicate the clear uses, and the rows read empty after.

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/query/columnLayout.h"
#include "backend/query/queryColumn.h"
#include "backend/query/schemaBuilder.h"
#include "backend/query/structureBatch.h"
#include "backend/restructureInfo.h"

namespace {

struct StoredColumn : ibBackendQueryColumn {
	wxString           physical;
	mutable ibTypeDescription type;

	wxString GetName() const override { return physical; }
	wxString GetPhysicalName() const override { return physical; }
	ibTypeDescription& GetTypeDesc() const override { return type; }
	ibMetaID GetColumnId() const override { return 1; }
};

struct LedgerBase : ::testing::Test {
	wxInitializer                          wx;
	std::shared_ptr<ibDatabaseLayerSQLite> db;

	void SetUp() override
	{
		if (!wx.IsOk())
			GTEST_SKIP() << "wxBase init failed";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(db, 1, 0);
	}

	void TearDown() override
	{
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}

	static wxString FieldOf(const StoredColumn& column, ibColumnRole role)
	{
		for (const ibColumnSlot& slot : column.DescribeLayout())
			if (slot.m_role == role)
				return slot.m_name;
		return wxString();
	}
};

} // namespace

TEST_F(LedgerBase, ARemovedStringIsCountedAndThenReadsEmpty)
{
	StoredColumn was;
	was.physical = wxT("Code");
	was.type.AppendMetaType(ibValueTypes::TYPE_STRING);
	was.type.AppendMetaType(ibValueTypes::TYPE_NUMBER);
	StoredColumn next = was;
	next.type.ClearMetaType(ibValue::GetIDByVT(ibValueTypes::TYPE_STRING));

	const wxString typeCol = FieldOf(was, ibColumnRole::Discriminator);
	const wxString stringCol = FieldOf(was, ibColumnRole::String);
	const wxString numberCol = FieldOf(was, ibColumnRole::Number);
	ASSERT_FALSE(typeCol.IsEmpty());
	ASSERT_FALSE(stringCol.IsEmpty());
	ASSERT_FALSE(numberCol.IsEmpty());

	const int stringTag = ibPersistedTypeTag(ibColumnRole::String);
	const int numberTag = ibPersistedTypeTag(ibColumnRole::Number);
	wxString fields;
	for (const ibColumnSlot& slot : was.DescribeLayout())
		fields << (fields.IsEmpty() ? wxT("") : wxT(", ")) << slot.m_name << wxT(" INTEGER");
	ASSERT_GE(db->RunQuery(wxT("%s"), wxT("CREATE TABLE goods (") + fields + wxT(")")), 0);
	auto put = [&](int tag) {
		ASSERT_GE(db->RunQuery(wxT("%s"),
			wxString::Format(wxT("INSERT INTO goods (%s) VALUES (%d)"), typeCol, tag)), 0);
	};
	put(stringTag);
	put(stringTag);
	put(numberTag);

	ibStructureBatch batch(wxT("goods"));
	ibRestructureInfo ledger;
	ASSERT_EQ(DiffColumnInto(batch, &next, &was, &ledger, wxT("Catalog.Goods"), wxT("Code")), 1);

	bool said = false;
	for (const ibRestructureInfo::Entry& entry : ledger) {
		if (entry.descr.Find(wxT("Catalog.Goods / Code: 2 stored values will be cleared")) != wxNOT_FOUND)
			said = true;
	}
	EXPECT_TRUE(said);

	ibSchemaBuilder schema;
	ASSERT_EQ(batch.Flush(schema), 1);

	ibDatabaseQueryBuilder read;
	ibQueryResult rows = read.From(wxT("goods")).Select({ typeCol }).Execute();
	int empty = 0;
	int numbers = 0;
	while (rows.Next()) {
		const int tag = rows.GetResultInt(typeCol);
		if (tag == 0)
			++empty;
		if (tag == numberTag)
			++numbers;
	}
	EXPECT_EQ(empty, 2);
	EXPECT_EQ(numbers, 1);
}

TEST_F(LedgerBase, AWiderStringClearsNothing)
{
	StoredColumn was;
	was.physical = wxT("Code");
	was.type.AppendMetaType(ibValueTypes::TYPE_STRING);
	was.type.SetString(10);
	StoredColumn next = was;
	next.type.SetString(20);

	const wxString typeCol = FieldOf(was, ibColumnRole::Discriminator);
	ASSERT_FALSE(typeCol.IsEmpty());
	wxString fields;
	for (const ibColumnSlot& slot : was.DescribeLayout())
		fields << (fields.IsEmpty() ? wxT("") : wxT(", ")) << slot.m_name;
	ASSERT_GE(db->RunQuery(wxT("%s"), wxT("CREATE TABLE goods (") + fields + wxT(")")), 0);
	ASSERT_GE(db->RunQuery(wxT("%s"),
		wxString::Format(wxT("INSERT INTO goods (%s) VALUES (%d)"), typeCol, ibPersistedTypeTag(ibColumnRole::String))), 0);

	ibStructureBatch batch(wxT("goods"));
	ibRestructureInfo ledger;
	ASSERT_EQ(DiffColumnInto(batch, &next, &was, &ledger, wxT("Catalog.Goods"), wxT("Code")), 1);
	for (const ibRestructureInfo::Entry& entry : ledger)
		EXPECT_EQ(entry.descr.Find(wxT("stored values will be cleared")), wxNOT_FOUND);
}

TEST_F(LedgerBase, AThousandIsGrouped)
{
	StoredColumn was;
	was.physical = wxT("Code");
	was.type.AppendMetaType(ibValueTypes::TYPE_STRING);
	StoredColumn next;
	next.physical = wxT("Code");
	next.type.AppendMetaType(ibValueTypes::TYPE_NUMBER);

	const wxString typeCol = FieldOf(was, ibColumnRole::Discriminator);
	ASSERT_FALSE(typeCol.IsEmpty());
	wxString fields;
	for (const ibColumnSlot& slot : was.DescribeLayout())
		fields << (fields.IsEmpty() ? wxT("") : wxT(", ")) << slot.m_name;
	ASSERT_GE(db->RunQuery(wxT("%s"), wxT("CREATE TABLE goods (") + fields + wxT(")")), 0);
	const int stringTag = ibPersistedTypeTag(ibColumnRole::String);
	for (int i = 0; i < 1234; ++i)
		ASSERT_GE(db->RunQuery(wxT("%s"),
			wxString::Format(wxT("INSERT INTO goods (%s) VALUES (%d)"), typeCol, stringTag)), 0);

	ibStructureBatch batch(wxT("goods"));
	ibRestructureInfo ledger;
	ASSERT_EQ(DiffColumnInto(batch, &next, &was, &ledger, wxT("Catalog.Goods"), wxT("Code")), 1);
	bool said = false;
	for (const ibRestructureInfo::Entry& entry : ledger) {
		if (entry.descr.Find(wxT("Catalog.Goods / Code: 1 234 stored values will be cleared")) != wxNOT_FOUND)
			said = true;
	}
	EXPECT_TRUE(said);
}
