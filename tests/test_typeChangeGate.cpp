// A type change is judged from the stored values before any structure is written.
//
// The predicate is the whole of the decision: a non-empty value that becomes
// empty is lost, a parse that leaves text behind is lost, a narrowing is lost,
// and an empty that stays empty is not. The apply that would lose something
// returns before it emits DDL, so the table and its values are still the ones
// that were read.

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/partial/catalog.h"
#include "backend/query/columnLayout.h"
#include "backend/query/schemaSnapshot.h"
#include "backend/query/typeChangeReport.h"
#include "backend/system/systemManager.h"

namespace {

ibTypeDescription TypeOf(ibValueTypes type)
{
	ibTypeDescription description;
	description.AppendMetaType(type);
	return description;
}

ibTypeDescription NumberOf(int precision, int scale)
{
	ibTypeDescription description = TypeOf(ibValueTypes::TYPE_NUMBER);
	description.SetNumber(precision, scale);
	return description;
}

ibTypeChangeFate Judge(const ibValue& stored, const ibTypeDescription& next)
{
	return ibAssessTypeChange(stored, next, nullptr, nullptr);
}

} // namespace

TEST(TypeChangePredicate, EmptyToEmptyIsNotLoss)
{
	EXPECT_EQ(Judge(ibValue(wxT("")), NumberOf(10, 0)).state, ibTypeChangeState::Absent);
	EXPECT_EQ(Judge(ibValue(wxT("   ")), TypeOf(ibValueTypes::TYPE_DATE)).state, ibTypeChangeState::Absent);
	EXPECT_EQ(Judge(ibValue(), NumberOf(10, 2)).state, ibTypeChangeState::Absent);
}

TEST(TypeChangePredicate, AStringThatIsNotTheNewTypeIsLoss)
{
	const ibTypeChangeFate date = Judge(ibValue(wxT("abc")), TypeOf(ibValueTypes::TYPE_DATE));
	EXPECT_EQ(date.state, ibTypeChangeState::Lost);
	EXPECT_TRUE(date.wipe);

	const ibTypeChangeFate boolean = Judge(ibValue(wxT("yes")), TypeOf(ibValueTypes::TYPE_BOOLEAN));
	EXPECT_EQ(boolean.state, ibTypeChangeState::Lost);
	EXPECT_TRUE(boolean.wipe);

	EXPECT_EQ(Judge(ibValue(wxT("True")), TypeOf(ibValueTypes::TYPE_BOOLEAN)).state, ibTypeChangeState::Kept);
	EXPECT_EQ(Judge(ibValue(wxT("False")), TypeOf(ibValueTypes::TYPE_BOOLEAN)).state, ibTypeChangeState::Kept);
}

TEST(TypeChangePredicate, APartialNumberIsLoss)
{
	for (const wchar_t* text : { wxT("10 pcs"), wxT("12x"), wxT("12,50"), wxT("123-45"), wxT("1.2.3") }) {
		const ibTypeChangeFate fate = Judge(ibValue(wxString(text)), NumberOf(12, 2));
		EXPECT_EQ(fate.state, ibTypeChangeState::Lost) << text;
		EXPECT_TRUE(fate.wipe) << text;
	}
	EXPECT_EQ(Judge(ibValue(wxT("10")), NumberOf(12, 0)).state, ibTypeChangeState::Kept);
	EXPECT_EQ(Judge(ibValue(wxT("12.50")), NumberOf(12, 2)).state, ibTypeChangeState::Kept);
}

TEST(TypeChangePredicate, NarrowingIsLossAndUsesTheSameRoundingTheWriteWill)
{
	const ibNumber original(wxT("12.345"));
	const ibTypeChangeFate scale = Judge(ibValue(original), NumberOf(10, 2));
	EXPECT_EQ(scale.state, ibTypeChangeState::Lost);
	EXPECT_TRUE(scale.narrowed);
	const ibNumber rounded = ibValueSystemFunction::Round(ibValue(original), 2);
	EXPECT_EQ(scale.value.GetNumber(), rounded);
	EXPECT_NE(rounded, original);

	const ibTypeChangeFate precision = Judge(ibValue(ibNumber(1234567)), NumberOf(5, 0));
	EXPECT_EQ(precision.state, ibTypeChangeState::Lost);
	EXPECT_TRUE(precision.wipe);

	ibTypeDescription shortText = TypeOf(ibValueTypes::TYPE_STRING);
	shortText.SetString(3);
	const ibTypeChangeFate length = Judge(ibValue(wxT("12345")), shortText);
	EXPECT_EQ(length.state, ibTypeChangeState::Lost);
	EXPECT_TRUE(length.narrowed);
	EXPECT_EQ(length.value.GetString(), wxT("123"));

	ibTypeDescription time = TypeOf(ibValueTypes::TYPE_DATE);
	time.SetDate(ibDateFractions::ibDateFractions_Time);
	const ibTypeChangeFate date = Judge(ibValue(2020, 5, 1, 10, 30, 0), time);
	EXPECT_EQ(date.state, ibTypeChangeState::Lost);
	EXPECT_TRUE(date.narrowed);
}

TEST(TypeChangePredicate, AWiderQualifierIsNotLoss)
{
	ibTypeDescription wide = TypeOf(ibValueTypes::TYPE_STRING);
	wide.SetString(20);
	ibValue stored(wxT("abc"));
	// The stored string has no qualifier of its own; a limit it already fits is kept.
	EXPECT_EQ(Judge(stored, wide).state, ibTypeChangeState::Kept);
	EXPECT_EQ(Judge(ibValue(ibNumber(12)), NumberOf(10, 0)).state, ibTypeChangeState::Kept);
}

namespace {

struct ApplyGate : ::testing::Test {
	wxInitializer wx;
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

	static ibValueMetaObjectAttribute* Attribute(ibMetaDataConfigurationFile& cfg, ibValueTypes type, int length)
	{
		ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
		ibValueMetaObject* catalog = cfg.CreateMetaObject(g_metaCatalogCLSID, root, false);
		catalog->SetName(wxT("Goods"));
		catalog->SetMetaID(100);
		ibValueMetaObject* created = cfg.CreateMetaObject(g_metaAttributeCLSID, catalog, false);
		created->SetName(wxT("Code"));
		created->SetMetaID(200);
		auto* attribute = dynamic_cast<ibValueMetaObjectAttribute*>(created);
		EXPECT_NE(attribute, nullptr);
		ibTypeDescription& description = attribute->GetTypeDesc();
		while (description.GetClsidCount() > 0)
			description.ClearMetaType(description.GetFirstClsid());
		description.AppendMetaType(type);
		if (type == ibValueTypes::TYPE_STRING)
			description.SetString(length);
		return attribute;
	}
};

const ibSchemaTable* MainTable(const ibSchemaSnapshot& snapshot)
{
	for (const ibSchemaTable& table : snapshot.Tables())
		if (!table.m_derived && !table.m_external && !table.m_columns.empty())
			return &table;
	return nullptr;
}

wxString Field(const ibBackendQueryColumn* column, ibColumnRole role)
{
	for (const ibColumnSlot& slot : DescribeColumnLayout(column))
		if (slot.m_role == role)
			return slot.m_name;
	return wxString();
}

} // namespace

TEST_F(ApplyGate, ALossyChangeLeavesTheTableAndItsValues)
{
	ibMetaDataConfigurationFile wasCfg;
	ibValueMetaObjectAttribute* wasAttr = Attribute(wasCfg, ibValueTypes::TYPE_STRING, 50);
	ASSERT_NE(wasAttr, nullptr);
	const ibSchemaSnapshot was = wasCfg.BuildSchemaSnapshot();
	const ibSchemaTable* table = MainTable(was);
	ASSERT_NE(table, nullptr);

	ibRestructureInfo ledger;
	ASSERT_EQ(DiffSnapshots(nullptr, was, nullptr, &ledger), 1);

	const wxString typeCol = Field(wasAttr->GetQueryColumn(), ibColumnRole::Discriminator);
	const wxString textCol = Field(wasAttr->GetQueryColumn(), ibColumnRole::String);
	ASSERT_FALSE(typeCol.IsEmpty());
	ASSERT_FALSE(textCol.IsEmpty());
	ASSERT_GE(db->RunQuery(wxT("%s"),
		wxString::Format(wxT("INSERT INTO %s (%s, %s) VALUES (%d, 'abc')"),
			table->m_name, typeCol, textCol, ibPersistedTypeTag(ibColumnRole::String))), 0);

	ibMetaDataConfigurationFile nextCfg;
	ASSERT_NE(Attribute(nextCfg, ibValueTypes::TYPE_DATE, 0), nullptr);
	const ibSchemaSnapshot next = nextCfg.BuildSchemaSnapshot();

	wxString refusal;
	try {
		DiffSnapshots(&was, next, nullptr, &ledger);
		FAIL() << "a lossy type change ran the restructuring";
	}
	catch (const ibBackendException& err) {
		refusal = err.GetErrorDescription();
	}
	EXPECT_NE(refusal.Find(wxT("would lose stored values")), wxNOT_FOUND) << refusal;

	ibDatabaseQueryBuilder read;
	ibQueryResult rows = read.From(table->m_name).Select({ textCol }).Execute();
	ASSERT_TRUE(rows.Next());
	EXPECT_EQ(rows.GetResultString(textCol), wxT("abc"));
	EXPECT_FALSE(rows.Next());
}

TEST_F(ApplyGate, AnEmptyValueDoesNotStopTheApply)
{
	ibMetaDataConfigurationFile wasCfg;
	ibValueMetaObjectAttribute* wasAttr = Attribute(wasCfg, ibValueTypes::TYPE_STRING, 50);
	ASSERT_NE(wasAttr, nullptr);
	const ibSchemaSnapshot was = wasCfg.BuildSchemaSnapshot();
	const ibSchemaTable* table = MainTable(was);
	ASSERT_NE(table, nullptr);
	ASSERT_EQ(DiffSnapshots(nullptr, was, nullptr, nullptr), 1);

	const wxString typeCol = Field(wasAttr->GetQueryColumn(), ibColumnRole::Discriminator);
	const wxString textCol = Field(wasAttr->GetQueryColumn(), ibColumnRole::String);
	ASSERT_GE(db->RunQuery(wxT("%s"),
		wxString::Format(wxT("INSERT INTO %s (%s, %s) VALUES (%d, '')"),
			table->m_name, typeCol, textCol, ibPersistedTypeTag(ibColumnRole::String))), 0);

	ibMetaDataConfigurationFile nextCfg;
	ASSERT_NE(Attribute(nextCfg, ibValueTypes::TYPE_NUMBER, 0), nullptr);
	const ibSchemaSnapshot next = nextCfg.BuildSchemaSnapshot();

	ibRestructureInfo ledger;
	EXPECT_EQ(DiffSnapshots(&was, next, nullptr, &ledger), 1);
	bool read = false;
	for (const ibRestructureInfo::Entry& entry : ledger)
		if (entry.descr.Find(wxT("rows read")) != wxNOT_FOUND)
			read = true;
	EXPECT_TRUE(read);
}
