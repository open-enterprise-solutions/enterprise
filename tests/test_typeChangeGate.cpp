// A type change is judged from the stored values before any structure is written.
//
// The predicate is the whole of the decision: a non-empty value that becomes
// empty is lost, a parse that leaves text behind is lost, a narrowing is lost,
// and an empty that stays empty is not. The apply that would lose something
// returns before it emits DDL, so the table and its values are still the ones
// that were read.

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <tuple>
#include <vector>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/partial/accumulationRegister.h"
#include "backend/metaCollection/partial/catalog.h"
#include "backend/metaCollection/partial/reference/reference.h"
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
		if (auto* refObj = dynamic_cast<ibValueMetaObjectRecordDataRef*>(catalog))
			if (ibValueMetaObjectAttributeBase* dataRef = refObj->GetDataReference())
				dataRef->GetTypeDesc().SetDefaultMetaType(
					reference_to_clsid(catalog->GetMetaID(), clsid_metaclass(catalog->GetClassType())));
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

void InsertRow(const wxString& table, const std::vector<std::pair<const ibBackendQueryColumn*, ibValue>>& cells)
{
	std::vector<wxString> names;
	for (const auto& cell : cells)
		for (const wxString& field : ColumnFieldNames(cell.first))
			names.push_back(field);
	ibQueryStatement statement(ibQueryStatement::Kind::Insert, table, names, {}, db_query->GetHolder());
	int position = 1;
	for (const auto& cell : cells)
		BindWriteValue(statement, cell.first, nullptr, cell.second, position);
	statement.RunQuery();
}

ibGuid GuidOf(unsigned data)
{
	ibGuidImpl impl{};
	impl.m_data1 = data;
	return ibGuid(impl);
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

	const wxString textCol = Field(wasAttr->GetQueryColumn(), ibColumnRole::String);
	ASSERT_FALSE(textCol.IsEmpty());
	ASSERT_NE(table->m_queryable, nullptr);
	const auto key = table->m_queryable->GetPrimaryKeyColumns();
	ASSERT_EQ(key.size(), 1u);
	const ibValue ref(ibValueReferenceDataObject::Create(&wasCfg, table->m_id, GuidOf(1)));
	InsertRow(table->m_name, { { key.front(), ref }, { wasAttr->GetQueryColumn(), ibValue(wxT("abc")) } });

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

namespace {

void Stamp(ibValueMetaObject* obj, const wxString& path, std::map<wxString, ibMetaID>& ids, ibMetaID& next)
{
	const auto inserted = ids.emplace(path, next);
	if (inserted.second)
		++next;
	obj->SetMetaID(inserted.first->second);
	for (unsigned i = 0; i < obj->GetChildCount(); ++i) {
		auto* child = dynamic_cast<ibValueMetaObject*>(obj->GetChild(i));
		if (child != nullptr)
			Stamp(child, path + wxString::Format(wxT("/%u"), i), ids, next);
	}
}

const ibTypeChangeAccept kAccept = [](const ibTypeChangeReport&) { return true; };

} // namespace

TEST_F(ApplyGate, ACatalogKeepsEachRowWhenTheReferenceKeyHasTwoSlots)
{
	std::map<wxString, ibMetaID> ids;
	ibMetaID nextId = 1000;

	auto build = [&](ibValueTypes type, int length) {
		auto cfg = std::make_unique<ibMetaDataConfigurationFile>();
		ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
		ibValueMetaObject* catalog = cfg->CreateMetaObject(g_metaCatalogCLSID, root, false);
		catalog->SetName(wxT("Goods"));
		ibValueMetaObject* created = cfg->CreateMetaObject(g_metaAttributeCLSID, catalog, false);
		created->SetName(wxT("Code"));
		auto* attribute = dynamic_cast<ibValueMetaObjectAttribute*>(created);
		ibTypeDescription& description = attribute->GetTypeDesc();
		while (description.GetClsidCount() > 0)
			description.ClearMetaType(description.GetFirstClsid());
		description.AppendMetaType(type);
		if (type == ibValueTypes::TYPE_STRING)
			description.SetString(length);
		if (type == ibValueTypes::TYPE_NUMBER)
			description.SetNumber(12, 0);
		Stamp(catalog, wxT("Goods"), ids, nextId);
		if (auto* refObj = dynamic_cast<ibValueMetaObjectRecordDataRef*>(catalog))
			if (ibValueMetaObjectAttributeBase* dataRef = refObj->GetDataReference())
				dataRef->GetTypeDesc().SetDefaultMetaType(
					reference_to_clsid(catalog->GetMetaID(), clsid_metaclass(catalog->GetClassType())));
		return std::make_pair(std::move(cfg), attribute);
	};

	auto wasBuilt = build(ibValueTypes::TYPE_STRING, 50);
	const ibSchemaSnapshot was = wasBuilt.first->BuildSchemaSnapshot();
	const ibSchemaTable* table = MainTable(was);
	ASSERT_NE(table, nullptr);
	ASSERT_EQ(DiffSnapshots(nullptr, was, nullptr, nullptr), 1);

	const std::vector<const ibBackendQueryColumn*> key = table->m_queryable->GetPrimaryKeyColumns();
	ASSERT_EQ(key.size(), 1u);
	int identitySlots = 0;
	for (const ibColumnSlot& slot : DescribeColumnLayout(key.front()))
		if (slot.m_role != ibColumnRole::Discriminator)
			++identitySlots;
	ASSERT_GE(identitySlots, 2);

	const ibValue refA(ibValueReferenceDataObject::Create(wasBuilt.first.get(), table->m_id, GuidOf(1)));
	const ibValue refB(ibValueReferenceDataObject::Create(wasBuilt.first.get(), table->m_id, GuidOf(2)));
	InsertRow(table->m_name, { { key.front(), refA }, { wasBuilt.second->GetQueryColumn(), ibValue(wxT("10")) } });
	InsertRow(table->m_name, { { key.front(), refB }, { wasBuilt.second->GetQueryColumn(), ibValue(wxT("20")) } });

	auto nextBuilt = build(ibValueTypes::TYPE_NUMBER, 0);
	const ibSchemaSnapshot next = nextBuilt.first->BuildSchemaSnapshot();
	ibRestructureInfo ledger;
	EXPECT_EQ(DiffSnapshots(&was, next, nullptr, &ledger, kAccept), 1);

	const wxString numberField = Field(nextBuilt.second->GetQueryColumn(), ibColumnRole::Number);
	ASSERT_FALSE(numberField.IsEmpty());
	ibDatabaseQueryBuilder read;
	ibQueryResult rows = read.From(table->m_name).Select({ numberField }).Execute();
	std::vector<ibNumber> values;
	while (rows.Next())
		values.push_back(rows.GetResultNumber(numberField));
	ASSERT_EQ(values.size(), 2u);
	const bool firstIsTen = values[0] == ibNumber(10) || values[1] == ibNumber(10);
	const bool otherIsTwenty = values[0] == ibNumber(20) || values[1] == ibNumber(20);
	EXPECT_TRUE(firstIsTen);
	EXPECT_TRUE(otherIsTwenty);
	EXPECT_FALSE(values[0] == values[1]);
}

TEST_F(ApplyGate, ARegisterKeepsEachLineOfOneRecorder)
{
	std::map<wxString, ibMetaID> ids;
	ibMetaID nextId = 2000;

	struct Built {
		std::unique_ptr<ibMetaDataConfigurationFile> cfg;
		ibValueMetaObjectAccumulationRegister* reg = nullptr;
		ibValueMetaObjectAttribute* resource = nullptr;
	};
	auto build = [&](ibValueTypes type) {
		Built built;
		built.cfg = std::make_unique<ibMetaDataConfigurationFile>();
		ibValueMetaObjectConfiguration* root = built.cfg->GetCommonMetaObject();
		ibValueMetaObject* document = built.cfg->CreateMetaObject(g_metaDocumentCLSID, root, false);
		document->SetName(wxT("Sale"));
		document->SetMetaID(50);
		built.reg = dynamic_cast<ibValueMetaObjectAccumulationRegister*>(
			built.cfg->CreateMetaObject(g_metaAccumulationRegisterCLSID, root, false));
		built.reg->SetName(wxT("Stock"));
		built.reg->GetRegisterRecorder()->GetTypeDesc().AppendMetaType(
			reference_to_clsid(document->GetMetaID(), clsid_metaclass(document->GetClassType())));
		ibValueMetaObject* created = built.cfg->CreateMetaObject(g_metaResourceCLSID, built.reg, false);
		created->SetName(wxT("Qty"));
		built.resource = dynamic_cast<ibValueMetaObjectAttribute*>(created);
		ibTypeDescription& description = built.resource->GetTypeDesc();
		while (description.GetClsidCount() > 0)
			description.ClearMetaType(description.GetFirstClsid());
		description.AppendMetaType(type);
		if (type == ibValueTypes::TYPE_STRING)
			description.SetString(20);
		else
			description.SetNumber(12, 2);
		Stamp(built.reg, wxT("Stock"), ids, nextId);
		return built;
	};

	Built was = build(ibValueTypes::TYPE_STRING);
	const ibSchemaSnapshot wasSnap = was.cfg->BuildSchemaSnapshot();
	const ibSchemaTable* table = wasSnap.Find(was.reg->GetMetaID());
	ASSERT_NE(table, nullptr);
	ASSERT_FALSE(table->m_derived);
	try {
		ASSERT_EQ(DiffSnapshots(nullptr, wasSnap, nullptr, nullptr), 1);
	}
	catch (const ibBackendException& err) {
		FAIL() << "create: " << err.GetErrorDescription();
	}

	const std::vector<const ibBackendQueryColumn*> key = table->m_queryable->GetPrimaryKeyColumns();
	ASSERT_GE(key.size(), 3u);
	const ibValue recorder(ibValueReferenceDataObject::Create(was.cfg.get(), 50, GuidOf(7)));
	const ibValue period(2020, 6, 1, 0, 0, 0);
	for (int line = 1; line <= 2; ++line) {
		InsertRow(table->m_name, {
			{ key[0], recorder },
			{ key[1], ibValue(ibNumber(line)) },
			{ key[2], period },
			{ was.resource->GetQueryColumn(), ibValue(line == 1 ? wxT("10") : wxT("20")) }
		});
	}

	Built next = build(ibValueTypes::TYPE_NUMBER);
	const ibSchemaSnapshot nextSnap = next.cfg->BuildSchemaSnapshot();
	ibRestructureInfo ledger;
	EXPECT_EQ(DiffSnapshots(&wasSnap, nextSnap, nullptr, &ledger, kAccept), 1);

	const wxString lineField = Field(key[1], ibColumnRole::Number);
	const wxString qtyField = Field(next.resource->GetQueryColumn(), ibColumnRole::Number);
	ASSERT_FALSE(lineField.IsEmpty());
	ASSERT_FALSE(qtyField.IsEmpty());
	ibDatabaseQueryBuilder read;
	ibQueryResult rows = read.From(table->m_name).Select({ lineField, qtyField }).Execute();
	ibNumber line1;
	ibNumber line2;
	int count = 0;
	while (rows.Next()) {
		++count;
		const ibNumber lineNo = rows.GetResultNumber(lineField);
		const ibNumber qty = rows.GetResultNumber(qtyField);
		if (lineNo == ibNumber(1))
			line1 = qty;
		if (lineNo == ibNumber(2))
			line2 = qty;
	}
	EXPECT_EQ(count, 2);
	EXPECT_EQ(line1, ibNumber(10));
	EXPECT_EQ(line2, ibNumber(20));
}

TEST_F(ApplyGate, ATabularSectionWithNoKeyRefusesTheConversion)
{
	std::map<wxString, ibMetaID> ids;
	ibMetaID nextId = 3000;

	auto build = [&](ibValueTypes type) {
		auto cfg = std::make_unique<ibMetaDataConfigurationFile>();
		ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
		ibValueMetaObject* catalog = cfg->CreateMetaObject(g_metaCatalogCLSID, root, false);
		catalog->SetName(wxT("Goods"));
		ibValueMetaObject* section = cfg->CreateMetaObject(g_metaTableRefCLSID, catalog, false);
		section->SetName(wxT("Lines"));
		ibValueMetaObject* created = cfg->CreateMetaObject(g_metaAttributeCLSID, section, false);
		created->SetName(wxT("Note"));
		auto* attribute = dynamic_cast<ibValueMetaObjectAttribute*>(created);
		ibTypeDescription& description = attribute->GetTypeDesc();
		while (description.GetClsidCount() > 0)
			description.ClearMetaType(description.GetFirstClsid());
		description.AppendMetaType(type);
		if (type == ibValueTypes::TYPE_STRING)
			description.SetString(20);
		else
			description.SetNumber(12, 0);
		Stamp(catalog, wxT("Goods"), ids, nextId);
		if (auto* refObj = dynamic_cast<ibValueMetaObjectRecordDataRef*>(catalog))
			if (ibValueMetaObjectAttributeBase* dataRef = refObj->GetDataReference())
				dataRef->GetTypeDesc().SetDefaultMetaType(
					reference_to_clsid(catalog->GetMetaID(), clsid_metaclass(catalog->GetClassType())));
		return std::make_tuple(std::move(cfg), section, attribute);
	};

	auto wasBuilt = build(ibValueTypes::TYPE_STRING);
	const ibSchemaSnapshot was = std::get<0>(wasBuilt)->BuildSchemaSnapshot();
	const ibSchemaTable* table = was.Find(std::get<1>(wasBuilt)->GetMetaID());
	ASSERT_NE(table, nullptr);
	ASSERT_TRUE(table->m_queryable->GetPrimaryKeyColumns().empty());
	ASSERT_EQ(DiffSnapshots(nullptr, was, nullptr, nullptr), 1);

	const wxString textCol = Field(std::get<2>(wasBuilt)->GetQueryColumn(), ibColumnRole::String);
	InsertRow(table->m_name, { { std::get<2>(wasBuilt)->GetQueryColumn(), ibValue(wxT("10")) } });

	auto nextBuilt = build(ibValueTypes::TYPE_NUMBER);
	const ibSchemaSnapshot next = std::get<0>(nextBuilt)->BuildSchemaSnapshot();
	wxString refusal;
	try {
		DiffSnapshots(&was, next, nullptr, nullptr, kAccept);
		FAIL() << "a section with no key was converted";
	}
	catch (const ibBackendException& err) {
		refusal = err.GetErrorDescription();
	}
	EXPECT_NE(refusal.Find(wxT("no complete key")), wxNOT_FOUND) << refusal;

	ibDatabaseQueryBuilder read;
	ibQueryResult rows = read.From(table->m_name).Select({ textCol }).Execute();
	ASSERT_TRUE(rows.Next());
	EXPECT_EQ(rows.GetResultString(textCol), wxT("10"));
}

TEST_F(ApplyGate, AnAcceptedNarrowingStoresTheRoundedValue)
{
	std::map<wxString, ibMetaID> ids;
	ibMetaID nextId = 4000;
	const ibNumber original(wxT("12.345"));

	auto build = [&](int scale) {
		auto cfg = std::make_unique<ibMetaDataConfigurationFile>();
		ibValueMetaObject* catalog = cfg->CreateMetaObject(g_metaCatalogCLSID, cfg->GetCommonMetaObject(), false);
		catalog->SetName(wxT("Goods"));
		ibValueMetaObject* created = cfg->CreateMetaObject(g_metaAttributeCLSID, catalog, false);
		created->SetName(wxT("Qty"));
		auto* attribute = dynamic_cast<ibValueMetaObjectAttribute*>(created);
		ibTypeDescription& description = attribute->GetTypeDesc();
		while (description.GetClsidCount() > 0)
			description.ClearMetaType(description.GetFirstClsid());
		description.AppendMetaType(ibValueTypes::TYPE_NUMBER);
		description.SetNumber(10, scale);
		Stamp(catalog, wxT("Goods"), ids, nextId);
		if (auto* refObj = dynamic_cast<ibValueMetaObjectRecordDataRef*>(catalog))
			if (ibValueMetaObjectAttributeBase* dataRef = refObj->GetDataReference())
				dataRef->GetTypeDesc().SetDefaultMetaType(
					reference_to_clsid(catalog->GetMetaID(), clsid_metaclass(catalog->GetClassType())));
		return std::make_pair(std::move(cfg), attribute);
	};

	auto wasBuilt = build(3);
	const ibSchemaSnapshot was = wasBuilt.first->BuildSchemaSnapshot();
	const ibSchemaTable* table = MainTable(was);
	ASSERT_NE(table, nullptr);
	ASSERT_EQ(DiffSnapshots(nullptr, was, nullptr, nullptr), 1);
	const auto key = table->m_queryable->GetPrimaryKeyColumns();
	ASSERT_FALSE(key.empty());
	const ibValue ref(ibValueReferenceDataObject::Create(wasBuilt.first.get(), table->m_id, GuidOf(3)));
	InsertRow(table->m_name, { { key.front(), ref }, { wasBuilt.second->GetQueryColumn(), ibValue(original) } });

	auto nextBuilt = build(2);
	const ibSchemaSnapshot next = nextBuilt.first->BuildSchemaSnapshot();
	EXPECT_EQ(DiffSnapshots(&was, next, nullptr, nullptr, kAccept), 1);

	const wxString numberField = Field(nextBuilt.second->GetQueryColumn(), ibColumnRole::Number);
	ibDatabaseQueryBuilder read;
	ibQueryResult rows = read.From(table->m_name).Select({ numberField }).Execute();
	ASSERT_TRUE(rows.Next());
	const ibNumber rounded = ibValueSystemFunction::Round(ibValue(original), 2);
	EXPECT_EQ(rows.GetResultNumber(numberField), rounded);
	EXPECT_NE(rounded, original);
}
