// =============================================================================
// OES Enterprise — issue #689: a tabular section and a register's record set answer Total, UnloadColumn and
// FindRows the way a value table does.
//
// All three keep their rows in the same place (ibValueModelStorage), and the value table's verbs were
// written there for that reason - Index and Total already answered on a tabular section. FindRows and
// UnloadColumn stayed on the value table alone, and a record set answered none of the three: a posting
// that totals its movements, or picks the lines of one item, had to copy them into a value table first
// (Unload) to ask.
//
// Built the way a person builds it: an application in memory with a data tool carrying a tabular section
// and an information register, the rows written through the model's own write, and every verb called by
// its name, as a script calls it.
// =============================================================================

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/value.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataApplication.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/dimension/metaDimensionObject.h"
#include "backend/metaCollection/resource/metaResourceObject.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/informationRegister.h"
#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/system/value/valueArray.h"
#include "backend/system/value/valueMap.h"

#include <utility>
#include <vector>

namespace {

struct ModelVerbs : ::testing::Test {
	wxInitializer                          m_wxInit;
	// RunDatabase sets this thread's code style to the application's; TearDown puts it back.
	const short                            m_codeStyle = ibCompileCode::GetCodeStyle();
	std::shared_ptr<ibDatabaseLayerSQLite> db;

	std::unique_ptr<ibMetaDataApplicationFile> cfg;
	ibValueMetaObjectRecordData* invoice = nullptr;
	ibValueMetaObject*           goods   = nullptr;   // tabular section: Item (string), Qty (number)
	ibValueMetaObjectAttribute*  item    = nullptr;
	ibValueMetaObjectAttribute*  qty     = nullptr;
	ibValueMetaObjectInformationRegister* prices = nullptr;   // Key (string) -> Value (number)
	ibValueMetaObjectDimension*  key     = nullptr;
	ibValueMetaObjectResource*   value   = nullptr;

	ibValuePtr<ibValueRecordDataObject>  object;
	ibValueTabularSectionDataObjectBase* lines = nullptr;
	ibValuePtr<ibValueRecordSetObject>   set;

	ibValueMetaObjectAttribute* Attribute(ibValueMetaObject* owner, const ibString& name, ibValueTypes type) {
		ibValueMetaObjectAttribute* attribute =
			dynamic_cast<ibValueMetaObjectAttribute*>(cfg->CreateMetaObject(g_metaAttributeCLSID, owner, /*runObject*/ false));
		if (attribute != nullptr) {
			attribute->SetName(name);
			attribute->GetTypeDesc().SetDefaultMetaType(type);
		}
		return attribute;
	}

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

		cfg = std::make_unique<ibMetaDataApplicationFile>();
		ibValueMetaObjectApplication* root = cfg->GetRootMetaObject();
		ASSERT_NE(root, nullptr);

		invoice = dynamic_cast<ibValueMetaObjectRecordData*>(cfg->CreateMetaObject(g_metaDataToolCLSID, root, false));
		ASSERT_NE(invoice, nullptr);
		invoice->SetName(wxT("Invoice"));
		goods = cfg->CreateMetaObject(g_metaTableCLSID, invoice, false);
		ASSERT_NE(goods, nullptr);
		goods->SetName(wxT("Goods"));
		item = Attribute(goods, wxT("Item"), ibValueTypes::TYPE_STRING);
		qty = Attribute(goods, wxT("Qty"), ibValueTypes::TYPE_NUMBER);
		ASSERT_NE(item, nullptr);
		ASSERT_NE(qty, nullptr);

		prices = dynamic_cast<ibValueMetaObjectInformationRegister*>(
			cfg->CreateMetaObject(g_metaInformationRegisterCLSID, root, false));
		ASSERT_NE(prices, nullptr);
		prices->SetName(wxT("Prices"));
		key = dynamic_cast<ibValueMetaObjectDimension*>(cfg->CreateMetaObject(g_metaDimensionCLSID, prices, false));
		value = dynamic_cast<ibValueMetaObjectResource*>(cfg->CreateMetaObject(g_metaResourceCLSID, prices, false));
		ASSERT_NE(key, nullptr);
		ASSERT_NE(value, nullptr);
		key->SetName(wxT("Key"));
		value->SetName(wxT("Value"));
		key->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_STRING);
		value->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_NUMBER);

		ASSERT_TRUE(cfg->RunDatabase()) << "the application in memory did not run";

		object = invoice->CreateRecordDataObjectValue();
		ASSERT_TRUE(object);
		lines = dynamic_cast<ibValueTabularSectionDataObjectBase*>(object->GetTableByMetaID(goods->GetMetaID()));
		ASSERT_NE(lines, nullptr);
		set = prices->CreateRecordSetObjectValue(false);
		ASSERT_NE(static_cast<ibValueRecordSetObject*>(set), nullptr);
	}

	void TearDown() override {
		set = ibValuePtr<ibValueRecordSetObject>();
		lines = nullptr;
		object = ibValuePtr<ibValueRecordDataObject>();
		if (cfg) cfg->CloseDatabase();
		cfg.reset();
		ibCompileCode::SetCodeStyle(m_codeStyle);
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}

	// One row of `model`: `name` and `number` into the two columns given, through the model's own write.
	// (A template: a tabular section and a record set each declare their own AppendRow.)
	template <class Model>
	static void AddRow(Model* model, const ibMetaID& nameColumn, const ibString& name,
		const ibMetaID& numberColumn, double number) {
		const ibDataViewItem row = model->GetItem(model->AppendRow());
		ASSERT_TRUE(row.IsOk());
		ASSERT_TRUE(static_cast<ibValueModel*>(model)->SetValueByMetaID(row, nameColumn, ibValue(name)));
		ASSERT_TRUE(static_cast<ibValueModel*>(model)->SetValueByMetaID(row, numberColumn, ibValue(number)));
	}

	// Paper 2, Ink 5, Paper 3 - into the tabular section and into the record set alike.
	void Fill() {
		AddRow(lines, item->GetMetaID(), wxT("Paper"), qty->GetMetaID(), 2);
		AddRow(lines, item->GetMetaID(), wxT("Ink"), qty->GetMetaID(), 5);
		AddRow(lines, item->GetMetaID(), wxT("Paper"), qty->GetMetaID(), 3);
		AddRow(static_cast<ibValueRecordSetObject*>(set), key->GetMetaID(), wxT("Paper"), value->GetMetaID(), 2);
		AddRow(static_cast<ibValueRecordSetObject*>(set), key->GetMetaID(), wxT("Ink"), value->GetMetaID(), 5);
		AddRow(static_cast<ibValueRecordSetObject*>(set), key->GetMetaID(), wxT("Paper"), value->GetMetaID(), 3);
	}

	// A script's call: the method by its name, the arguments as given.
	static ibValue Call(ibValue target, const ibString& method, std::vector<ibValue> args) {
		const long number = target.FindMethod(method);
		EXPECT_GE(number, 0) << method.ToStdString() << " is not a method here";
		if (number < 0)
			return ibValue();
		std::vector<ibValue*> pointers;
		for (ibValue& arg : args)
			pointers.push_back(&arg);
		ibValue result;
		EXPECT_TRUE(target.CallAsFunc(number, result, pointers.data(), static_cast<long>(pointers.size()))) << method.ToStdString();
		return result;
	}

	static ibValue Filter(const std::vector<std::pair<ibString, ibValue>>& terms) {
		ibValueStructure* filter = new ibValueStructure();
		ibValue holder;
		holder = filter;
		for (const auto& term : terms)
			filter->Insert(ibValue(term.first), term.second);
		return holder;
	}

	static std::vector<ibValue> Items(const ibValue& array) {
		std::vector<ibValue> out;
		ibValueArray* values = array.ConvertToType<ibValueArray>();
		EXPECT_NE(values, nullptr) << "an array";
		if (values == nullptr)
			return out;
		for (unsigned int i = 0; i < values->Count(); ++i) {
			ibValue at;
			values->GetAt(ibValue(static_cast<double>(i)), at);
			out.push_back(at);
		}
		return out;
	}

	static ibValue Field(ibValue row, const ibString& name) {
		ibValue cell;
		const long prop = row.FindProp(name);
		EXPECT_GE(prop, 0) << name.ToStdString();
		if (prop >= 0)
			row.GetPropVal(prop, cell);
		return cell;
	}

	ibValue Lines() const { return ibValue(static_cast<ibValue*>(lines)); }
	ibValue Set() const { return ibValue(static_cast<ibValue*>(static_cast<ibValueRecordSetObject*>(set))); }
};

} // namespace

// --------------------------------- tabular section ---------------------------------

TEST_F(ModelVerbs, TabularSection_UnloadColumn_IsTheColumnInRowOrder)
{
	Fill();
	const std::vector<ibValue> values = Items(Call(Lines(), wxT("UnloadColumn"), { ibValue(ibString(wxT("Item"))) }));
	ASSERT_EQ(values.size(), 3u);
	EXPECT_EQ(values[0].GetString(), wxT("Paper"));
	EXPECT_EQ(values[1].GetString(), wxT("Ink"));
	EXPECT_EQ(values[2].GetString(), wxT("Paper"));
}

TEST_F(ModelVerbs, TabularSection_UnloadColumn_OfAColumnThatIsNotThere_IsEmpty)
{
	Fill();
	EXPECT_TRUE(Items(Call(Lines(), wxT("UnloadColumn"), { ibValue(ibString(wxT("Colour"))) })).empty())
		<< "the value table's answer: an empty array";
}

TEST_F(ModelVerbs, TabularSection_FindRows_AreTheSectionsOwnRows)
{
	Fill();
	const std::vector<ibValue> rows =
		Items(Call(Lines(), wxT("FindRows"), { Filter({ { wxT("Item"), ibValue(ibString(wxT("Paper"))) } }) }));
	ASSERT_EQ(rows.size(), 2u);
	EXPECT_EQ(Field(rows[0], wxT("Qty")).GetNumber().ToInt(), 2);
	EXPECT_EQ(Field(rows[1], wxT("Qty")).GetNumber().ToInt(), 3);

	// Changing a row found changes the section.
	ibValue row = rows[1];
	const long prop = row.FindProp(wxT("Qty"));
	ASSERT_GE(prop, 0);
	ASSERT_TRUE(row.SetPropVal(prop, ibValue(7.0)));
	ibValue cell;
	ASSERT_TRUE(lines->GetValueByMetaID(lines->GetItem(2), qty->GetMetaID(), cell));
	EXPECT_EQ(cell.GetNumber().ToInt(), 7);
}

TEST_F(ModelVerbs, TabularSection_FindRows_NamingAColumnThatIsNotThere_Raises)
{
	Fill();
	EXPECT_THROW(Call(Lines(), wxT("FindRows"), { Filter({ { wxT("Itme"), ibValue(ibString(wxT("Paper"))) } }) }),
		ibBackendException) << "a typo in a filter must not read as \"no such rows\"";
	EXPECT_THROW(Call(Lines(), wxT("FindRows"), { ibValue(ibString(wxT("Item"))) }), ibBackendException)
		<< "a filter is a Structure";
}

// ----------------------------------- record set -----------------------------------

TEST_F(ModelVerbs, RecordSet_Total_AddsTheColumn)
{
	Fill();
	EXPECT_EQ(Call(Set(), wxT("Total"), { ibValue(ibString(wxT("Value"))) }).GetNumber().ToInt(), 10);
	EXPECT_THROW(Call(Set(), wxT("Total"), { ibValue(ibString(wxT("Price"))) }), ibBackendException)
		<< "a column that is not there raises, as on a value table";
}

TEST_F(ModelVerbs, RecordSet_UnloadColumn_IsTheColumnInRowOrder)
{
	Fill();
	const std::vector<ibValue> values = Items(Call(Set(), wxT("UnloadColumn"), { ibValue(ibString(wxT("Value"))) }));
	ASSERT_EQ(values.size(), 3u);
	EXPECT_EQ(values[0].GetNumber().ToInt(), 2);
	EXPECT_EQ(values[1].GetNumber().ToInt(), 5);
	EXPECT_EQ(values[2].GetNumber().ToInt(), 3);
}

TEST_F(ModelVerbs, RecordSet_FindRows_AreTheSetsOwnRows)
{
	Fill();
	const std::vector<ibValue> rows =
		Items(Call(Set(), wxT("FindRows"), { Filter({ { wxT("Key"), ibValue(ibString(wxT("Paper"))) } }) }));
	ASSERT_EQ(rows.size(), 2u);
	EXPECT_EQ(Field(rows[0], wxT("Value")).GetNumber().ToInt(), 2);
	EXPECT_EQ(Field(rows[1], wxT("Value")).GetNumber().ToInt(), 3);
}

// The set's own level sits between the module and the register's verbs: the register's still answer, and so does
// the set's property.
TEST_F(ModelVerbs, RecordSet_TheRegistersOwnVerbsStillAnswer)
{
	Fill();
	EXPECT_EQ(Call(Set(), wxT("Count"), {}).GetNumber().ToInt(), 3);
	EXPECT_GE(Set().FindProp(wxT("LockForUpdate")), 0);
}
