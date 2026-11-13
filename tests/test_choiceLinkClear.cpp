// =============================================================================
// OES Enterprise — issue #650: a column chosen within a field of the object above it is emptied when
// that field changes, in every row.
//
// A column of a tabular section may carry a choice-parameter row whose source is a field of the
// object: `Goods.Contract`, filtered by the header's `Counterparty`, on change: Clear. The object's own
// write (ibValueRecordDataObject::SetValueByMetaID) asked ibChoiceLinkResolver::ClearLinked to empty
// what was chosen within the changed field, and that pass walked only the object's own fields — the
// header changed and every line kept a contract with the previous counterparty. Between two header
// fields, and between two cells of one row, the same link worked.
//
// Built the way a person builds it: a configuration in memory, the parameter row written through the
// attribute's own `ChoiceParameters` property (what the designer writes), and the values set through
// the object's and the section's own write — the road a form and a module both take.
// =============================================================================

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/value.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/propertyManager/propertyObject.h"
#include "core/description/choiceLinkDescription.h"
#include "core/serialize/dataBuilder.h"

namespace {

struct ChoiceLinkClear : ::testing::Test {
	wxInitializer                          m_wxInit;
	// RunDatabase sets this thread's code style to the configuration's. The binary runs VES
	// (test_compiler.cpp), so TearDown puts it back, as the other fixtures that run a configuration do.
	const short                            m_codeStyle = ibCompileCode::GetCodeStyle();
	std::shared_ptr<ibDatabaseLayerSQLite> db;

	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	ibValueMetaObject*            counterparties = nullptr;
	ibValueMetaObject*            contracts      = nullptr;
	ibValueMetaObjectRecordData*  invoice        = nullptr;
	ibValueMetaObjectAttribute*   counterparty   = nullptr;   // header
	ibValueMetaObjectAttribute*   comment        = nullptr;   // header, governs nothing in the lines
	ibValueMetaObject*            goods          = nullptr;   // tabular section
	ibValueMetaObjectAttribute*   contract       = nullptr;   // column: Owner from the header's Counterparty, Clear

	ibValuePtr<ibValueRecordDataObject> object;
	ibValueTabularSectionDataObjectBase* lines = nullptr;

	ibValueMetaObjectAttribute* Attribute(ibValueMetaObject* owner, const ibString& name) {
		ibValueMetaObjectAttribute* attribute =
			dynamic_cast<ibValueMetaObjectAttribute*>(cfg->CreateMetaObject(g_metaAttributeCLSID, owner, /*runObject*/ false));
		if (attribute != nullptr)
			attribute->SetName(name);
		return attribute;
	}
	static ibClassID ReferenceTo(const ibValueMetaObject* catalog) {
		return reference_to_clsid(catalog->GetMetaID(), clsid_metaclass(catalog->GetClassType()));
	}
	ibValue NewReference(const ibValueMetaObject* catalog) const {
		return ibValue(ibValueReferenceDataObject::Create(cfg.get(), catalog->GetMetaID(), ibGuid::newGuid()));
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

		cfg = std::make_unique<ibMetaDataConfigurationFile>();
		ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
		ASSERT_NE(root, nullptr);

		counterparties = cfg->CreateMetaObject(g_metaCatalogCLSID, root, false);
		contracts = cfg->CreateMetaObject(g_metaCatalogCLSID, root, false);
		ASSERT_NE(counterparties, nullptr);
		ASSERT_NE(contracts, nullptr);
		counterparties->SetName(wxT("Counterparties"));
		contracts->SetName(wxT("Contracts"));
		ibValueMetaObjectAttribute* contractOwner = Attribute(contracts, wxT("Counterparty"));
		ASSERT_NE(contractOwner, nullptr);
		contractOwner->GetTypeDesc().SetDefaultMetaType(ReferenceTo(counterparties));

		// The object: a data processor holds its values in memory, through the same object write a
		// document's header goes through (ibValueRecordDataObject::SetValueByMetaID).
		invoice = dynamic_cast<ibValueMetaObjectRecordData*>(cfg->CreateMetaObject(g_metaDataProcessorCLSID, root, false));
		ASSERT_NE(invoice, nullptr);
		invoice->SetName(wxT("Invoice"));

		counterparty = Attribute(invoice, wxT("Counterparty"));
		comment = Attribute(invoice, wxT("Comment"));
		ASSERT_NE(counterparty, nullptr);
		ASSERT_NE(comment, nullptr);
		counterparty->GetTypeDesc().SetDefaultMetaType(ReferenceTo(counterparties));
		comment->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_STRING);

		goods = cfg->CreateMetaObject(g_metaTableCLSID, invoice, false);
		ASSERT_NE(goods, nullptr);
		goods->SetName(wxT("Goods"));
		contract = Attribute(goods, wxT("Contract"));
		ASSERT_NE(contract, nullptr);
		contract->GetTypeDesc().SetDefaultMetaType(ReferenceTo(contracts));

		// The parameter row, written as the designer writes it — through the attribute's own property.
		ibChoiceParametersDescription params;
		ibChoiceParameterRowDescription row;
		row.m_parameter = contractOwner->GetMetaID();
		row.m_source.AppendSource(counterparty->GetMetaID());
		row.m_onChange = ibChoiceParameterOnChange::Clear;
		params.m_rows.push_back(row);
		ibDataValue node;
		ASSERT_TRUE(ibChoiceLinkDescriptionMemory::WriteNode(node, params));
		ibProperty* property = contract->GetProperty(wxT("ChoiceParameters"));
		ASSERT_NE(property, nullptr);
		ASSERT_TRUE(property->SetNodeValue(node));
		ASSERT_EQ(contract->GetChoiceParameters().m_rows.size(), 1u);

		ASSERT_TRUE(cfg->RunDatabase()) << "the configuration in memory did not run";

		object = invoice->CreateRecordDataObjectValue();
		ASSERT_TRUE(object);
		lines = dynamic_cast<ibValueTabularSectionDataObjectBase*>(object->GetTableByMetaID(goods->GetMetaID()));
		ASSERT_NE(lines, nullptr);
	}

	void TearDown() override {
		lines = nullptr;
		object = ibValuePtr<ibValueRecordDataObject>();
		if (cfg) cfg->CloseDatabase();
		cfg.reset();
		ibCompileCode::SetCodeStyle(m_codeStyle);
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}

	// The header set to A, and three lines with a contract of A each.
	void FillWithContractsOf(const ibValue& counterpartyA) {
		ASSERT_TRUE(object->SetValueByMetaID(counterparty->GetMetaID(), counterpartyA));
		for (int i = 0; i < 3; ++i) {
			const ibDataViewItem item = lines->GetItem(lines->AppendRow());
			ASSERT_TRUE(item.IsOk());
			ASSERT_TRUE(lines->SetValueByMetaID(item, contract->GetMetaID(), NewReference(contracts)));
		}
		ASSERT_EQ(lines->GetRowCount(), 3);
	}

	ibValue ContractIn(long row) const {
		ibValue value;
		EXPECT_TRUE(lines->GetValueByMetaID(lines->GetItem(row), contract->GetMetaID(), value));
		return value;
	}
};

} // namespace

TEST_F(ChoiceLinkClear, HeaderFieldChanged_ColumnChosenWithinItIsEmptiedInEveryRow)
{
	FillWithContractsOf(NewReference(counterparties));
	for (long row = 0; row < lines->GetRowCount(); ++row)
		ASSERT_FALSE(ContractIn(row).IsEmpty()) << "row " << row << " was given a contract";

	ASSERT_TRUE(object->SetValueByMetaID(counterparty->GetMetaID(), NewReference(counterparties)));

	for (long row = 0; row < lines->GetRowCount(); ++row) {
		const ibValue value = ContractIn(row);
		EXPECT_TRUE(value.IsEmpty()) << "row " << row << " kept the contract of the previous counterparty";
		EXPECT_EQ(value.GetClassType(), ReferenceTo(contracts)) << "emptied to the column's own empty value";
	}
}

TEST_F(ChoiceLinkClear, OtherHeaderFieldChanged_ColumnIsLeftAlone)
{
	FillWithContractsOf(NewReference(counterparties));
	std::vector<ibValue> before;
	for (long row = 0; row < lines->GetRowCount(); ++row)
		before.push_back(ContractIn(row));

	ASSERT_TRUE(object->SetValueByMetaID(comment->GetMetaID(), ibValue(ibString(wxT("deliver by Friday")))));

	for (long row = 0; row < lines->GetRowCount(); ++row) {
		EXPECT_FALSE(ContractIn(row).IsEmpty()) << "row " << row;
		EXPECT_TRUE(ContractIn(row) == before[static_cast<size_t>(row)]) << "row " << row;
	}
}

TEST_F(ChoiceLinkClear, SameValueWrittenAgain_ColumnIsLeftAlone)
{
	const ibValue counterpartyA = NewReference(counterparties);
	FillWithContractsOf(counterpartyA);

	ASSERT_TRUE(object->SetValueByMetaID(counterparty->GetMetaID(), counterpartyA));

	for (long row = 0; row < lines->GetRowCount(); ++row)
		EXPECT_FALSE(ContractIn(row).IsEmpty()) << "row " << row << " - nothing changed, nothing is stale";
}
