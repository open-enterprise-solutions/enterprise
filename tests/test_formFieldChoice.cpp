// =============================================================================
// OES Enterprise — what a FORM FIELD says about its choice, over what its attribute says (issue #259).
//
// A Textctrl and a table column carry three properties of their own: QuickChoice (Auto / Use / DontUse),
// ChoiceFoldersAndItems (Auto / Items / Folders / FoldersAndItems) and ChoiceParameters — the same table an
// attribute holds, read against the same holder. These pin the three rules the field adds:
//
//   * its own rows stand OVER the attribute's — a row of the same parameter replaces the attribute's, the
//     others are added (ibChoiceLinkResolver::Resolve with `own`);
//   * a field chosen under a row of its own is emptied when that row's source changes, unless the row says
//     Keep (ibChoiceLinkResolver::ClearLinked with `rows`);
//   * QuickChoice decides only for a reference — a number keeps its calculator, whatever the field says.
//
// And that the three survive the form's own save and load (ibValueTextCtrl::WriteData / ReadData).
//
// DB-FREE AND UI-FREE: a fresh ibMetaDataConfigurationFile that is never run, a source that answers hops from
// a map (the shape test_sourceHopChain uses), and a control made with no form around it.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/init.h>

#include <map>

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/choiceLinkResolver.h"
#include "backend/srcDataObject.h"
#include "backend/propertyManager/property/propertyChoiceLink.h"
#include "core/serialize/dataBuilder.h"

#include "frmserver/visualView/ctrl/widgets.h"   // ibValueTextCtrl
#include "frmserver/visualView/ctrl/typeControl.h"

namespace {

// The form's object: answers a hop from a map, and writes one back into it — the holder a form field stands on.
class MockSource : public ibValue, public ibSourceDataObject {
public:
	std::map<ibSourceId, ibValue> m_fields;
	const ibMetaData* m_metaData = nullptr;
	const ibValueMetaObjectGenericData* m_metaObject = nullptr;

	MockSource() : ibValue(ibValueTypes::TYPE_VALUE) {}

	bool GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const override {
		const auto it = m_fields.find(hop.m_id);
		if (it == m_fields.end())
			return false;
		out = it->second;
		return true;
	}
	bool SetValueBySourceHop(const ibSourceHop& hop, const ibValue& value) override {
		m_fields[hop.m_id] = value;
		return true;
	}

	void SourceIncrRef() override {}
	void SourceDecrRef() override {}
	bool IsEmpty() const override { return m_fields.empty(); }
	const ibUniqueKey& GetGuid() const override { static const ibUniqueKey none; return none; }
	const ibValueMetaObjectGenericData* GetSourceMetaObject() const override { return m_metaObject; }
	const ibSourceExplorer* GetSourceExplorer() const override { return nullptr; }
	const ibMetaData* GetSourceMetaData() const override { return m_metaData; }
	ibClassID GetSourceClassType() const override { return 0; }
	ibString GetSourceCaption() const override { return wxEmptyString; }
};

// A row: `parameter` (a field of what is chosen) takes its value from `source` (a field beside this one).
ibChoiceParameterRowDescription Row(const ibMetaID& parameter, const ibMetaID& source,
	ibChoiceParameterOnChange onChange = ibChoiceParameterOnChange::Clear)
{
	ibChoiceParameterRowDescription row;
	row.m_parameter = parameter;
	row.m_source.AppendSource(source);
	row.m_onChange = onChange;
	return row;
}

// A sale document with a counterparty, an organisation, a second counterparty and a contract — and the contracts
// catalog whose Counterparty and Organisation the contract's choice is narrowed by.
struct FormFieldChoiceFix : ::testing::Test {
	wxInitializer m_wxInit;
	ibMetaDataConfigurationFile cfg;

	ibValueMetaObjectAttributeBase* contractCounterparty = nullptr;   // parameters: fields of the chosen contract
	ibValueMetaObjectAttributeBase* contractOrganisation = nullptr;
	ibValueMetaObjectAttributeBase* counterparty = nullptr;           // the sale's fields: sources and the field
	ibValueMetaObjectAttributeBase* payer = nullptr;
	ibValueMetaObjectAttributeBase* organisation = nullptr;
	ibValueMetaObjectAttributeBase* contract = nullptr;

	MockSource sale;

	ibValueMetaObjectAttributeBase* Attribute(ibValueMetaObject* owner, const wxString& name) {
		auto* attribute = dynamic_cast<ibValueMetaObjectAttributeBase*>(cfg.CreateMetaObject(g_metaAttributeCLSID, owner, false));
		if (attribute != nullptr)
			attribute->SetName(name);
		return attribute;
	}

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";

		ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
		ASSERT_NE(root, nullptr);
		ibValueMetaObject* contracts = cfg.CreateMetaObject(g_metaCatalogCLSID, root, /*runObject*/ false);
		ibValueMetaObject* sales = cfg.CreateMetaObject(g_metaDocumentCLSID, root, /*runObject*/ false);
		ASSERT_NE(contracts, nullptr);
		ASSERT_NE(sales, nullptr);
		contracts->SetName(wxT("Contracts"));
		sales->SetName(wxT("Sale"));

		contractCounterparty = Attribute(contracts, wxT("Counterparty"));
		contractOrganisation = Attribute(contracts, wxT("Organisation"));
		counterparty = Attribute(sales, wxT("Counterparty"));
		payer = Attribute(sales, wxT("Payer"));
		organisation = Attribute(sales, wxT("Organisation"));
		contract = Attribute(sales, wxT("Contract"));
		ASSERT_TRUE(contractCounterparty && contractOrganisation && counterparty && payer && organisation && contract);

		sale.m_metaData = &cfg;
		sale.m_metaObject = dynamic_cast<const ibValueMetaObjectGenericData*>(sales);
		sale.m_fields[counterparty->GetMetaID()] = ibValue(ibString(wxT("Acme")));
		sale.m_fields[payer->GetMetaID()] = ibValue(ibString(wxT("Globex")));
		sale.m_fields[organisation->GetMetaID()] = ibValue(ibString(wxT("Our firm")));
		sale.m_fields[contract->GetMetaID()] = ibValue(ibString(wxT("No. 7")));
	}

	// The attribute's own table, set as the designer sets it.
	void SetAttributeRows(const ibChoiceParametersDescription& rows) {
		auto* property = dynamic_cast<ibPropertyChoiceParameters*>(contract->GetProperty(wxT("ChoiceParameters")));
		ASSERT_NE(property, nullptr);
		property->SetValue(rows);
	}
};

} // namespace

// No rows of its own: the field's answer is exactly its attribute's.
TEST_F(FormFieldChoiceFix, Resolve_NoOwnRows_IsTheAttributesAnswer)
{
	ibChoiceParametersDescription attributeRows;
	attributeRows.SetRow(Row(contractCounterparty->GetMetaID(), counterparty->GetMetaID()));
	SetAttributeRows(attributeRows);

	const ibChoiceHolder holder(&sale);
	const ibChoiceCondition condition = ibChoiceLinkResolver::Resolve(holder, contract, ibChoiceParametersDescription());

	ASSERT_EQ(condition.m_parameters.size(), 1u);
	EXPECT_TRUE(condition.m_parameters.at(wxT("Counterparty")).GetString() == wxT("Acme"));
}

// A row of the same parameter replaces the attribute's; a row of another parameter is added to it.
TEST_F(FormFieldChoiceFix, Resolve_OwnRows_StandOverTheAttributes)
{
	ibChoiceParametersDescription attributeRows;
	attributeRows.SetRow(Row(contractCounterparty->GetMetaID(), counterparty->GetMetaID()));
	SetAttributeRows(attributeRows);

	ibChoiceParametersDescription own;
	own.SetRow(Row(contractCounterparty->GetMetaID(), payer->GetMetaID()));
	own.SetRow(Row(contractOrganisation->GetMetaID(), organisation->GetMetaID()));

	const ibChoiceHolder holder(&sale);
	const ibChoiceCondition condition = ibChoiceLinkResolver::Resolve(holder, contract, own);

	ASSERT_EQ(condition.m_parameters.size(), 2u);
	EXPECT_TRUE(condition.m_parameters.at(wxT("Counterparty")).GetString() == wxT("Globex"));
	EXPECT_TRUE(condition.m_parameters.at(wxT("Organisation")).GetString() == wxT("Our firm"));

	// …and the attribute's own table is left as it was: the field's rows are the form's.
	EXPECT_EQ(contract->GetChoiceParameters().m_rows.size(), 1u);
}

// A field bound to no attribute still narrows by its own rows.
TEST_F(FormFieldChoiceFix, Resolve_OwnRows_WithoutAnAttribute)
{
	ibChoiceParametersDescription own;
	own.SetRow(Row(contractOrganisation->GetMetaID(), organisation->GetMetaID()));

	const ibChoiceHolder holder(&sale);
	const ibChoiceCondition condition = ibChoiceLinkResolver::Resolve(holder, nullptr, own);

	ASSERT_EQ(condition.m_parameters.size(), 1u);
	EXPECT_TRUE(condition.m_parameters.at(wxT("Organisation")).GetString() == wxT("Our firm"));
}

// The counterparty changed: the contract chosen under the field's own row is emptied.
TEST_F(FormFieldChoiceFix, ClearLinked_OwnRow_EmptiesTheField)
{
	ibChoiceParametersDescription own;
	own.SetRow(Row(contractCounterparty->GetMetaID(), counterparty->GetMetaID()));

	ibChoiceHolder holder(&sale);
	ibChoiceLinkResolver::ClearLinked(holder, counterparty->GetMetaID(), contract, own);

	EXPECT_TRUE(sale.m_fields.at(contract->GetMetaID()).GetString().IsEmpty());
}

// …unless the row says Keep, and a change of a field no row names empties nothing.
TEST_F(FormFieldChoiceFix, ClearLinked_KeepOrUnrelated_LeavesTheField)
{
	ibChoiceParametersDescription own;
	own.SetRow(Row(contractCounterparty->GetMetaID(), counterparty->GetMetaID(), ibChoiceParameterOnChange::Keep));

	ibChoiceHolder holder(&sale);
	ibChoiceLinkResolver::ClearLinked(holder, counterparty->GetMetaID(), contract, own);
	EXPECT_TRUE(sale.m_fields.at(contract->GetMetaID()).GetString() == wxT("No. 7"));

	ibChoiceParametersDescription cleared;
	cleared.SetRow(Row(contractCounterparty->GetMetaID(), counterparty->GetMetaID()));
	ibChoiceLinkResolver::ClearLinked(holder, organisation->GetMetaID(), contract, cleared);
	EXPECT_TRUE(sale.m_fields.at(contract->GetMetaID()).GetString() == wxT("No. 7"));
}

// QuickChoice decides for a reference only; Auto is the type's own answer.
TEST(FormFieldQuickChoice, DecidesForAReferenceOnly)
{
	const ibClassID reference = reference_to_clsid(1005, clsid_metaclass(g_metaCatalogCLSID));
	const ibClassID number = g_valueNumberCLSID;

	EXPECT_TRUE(ibTypeControlFactory::HasQuickChoice(ibQuickChoiceMode_Auto, true, reference));
	EXPECT_FALSE(ibTypeControlFactory::HasQuickChoice(ibQuickChoiceMode_Auto, false, reference));
	EXPECT_TRUE(ibTypeControlFactory::HasQuickChoice(ibQuickChoiceMode_Use, false, reference));
	EXPECT_FALSE(ibTypeControlFactory::HasQuickChoice(ibQuickChoiceMode_DontUse, true, reference));

	// A number keeps its calculator whatever the field says.
	EXPECT_TRUE(ibTypeControlFactory::HasQuickChoice(ibQuickChoiceMode_DontUse, true, number));
	EXPECT_FALSE(ibTypeControlFactory::HasQuickChoice(ibQuickChoiceMode_Use, false, number));
}

// The three properties go out with the form and come back with it.
TEST_F(FormFieldChoiceFix, TextctrlProperties_SurviveSaveAndLoad)
{
	ibValuePtr<ibValueTextCtrl> written(new ibValueTextCtrl());

	auto* quickChoice = dynamic_cast<ibPropertyEnumBase*>(written->GetProperty(wxT("QuickChoice")));
	auto* foldersAndItems = dynamic_cast<ibPropertyEnumBase*>(written->GetProperty(wxT("ChoiceFoldersAndItems")));
	auto* parameters = dynamic_cast<ibPropertyChoiceParameters*>(written->GetProperty(wxT("ChoiceParameters")));
	ASSERT_NE(quickChoice, nullptr);
	ASSERT_NE(foldersAndItems, nullptr);
	ASSERT_NE(parameters, nullptr);

	// Nothing said: the attribute's answer stands.
	ibSelectMode mode = ibSelectMode::ibSelectMode_Items;
	EXPECT_FALSE(written->GetOwnSelectMode(mode));

	quickChoice->SetValue(static_cast<int>(ibQuickChoiceMode_DontUse));
	foldersAndItems->SetValue(static_cast<int>(ibChoiceFoldersAndItems_FoldersAndItems));
	ibChoiceParametersDescription rows;
	rows.SetRow(Row(contractCounterparty->GetMetaID(), counterparty->GetMetaID(), ibChoiceParameterOnChange::Keep));
	parameters->SetValue(rows);

	ibDataNode node;
	ASSERT_TRUE(written->WriteData(node));

	ibValuePtr<ibValueTextCtrl> read(new ibValueTextCtrl());
	ASSERT_TRUE(read->ReadData(node));

	EXPECT_EQ(dynamic_cast<ibPropertyEnumBase*>(read->GetProperty(wxT("QuickChoice")))->GetValueAsInteger(),
		static_cast<int>(ibQuickChoiceMode_DontUse));
	ASSERT_TRUE(read->GetOwnSelectMode(mode));
	EXPECT_EQ(mode, ibSelectMode::ibSelectMode_FoldersAndItems);
	const ibChoiceParametersDescription* own = read->GetOwnChoiceParameters();
	ASSERT_NE(own, nullptr);
	EXPECT_TRUE(*own == rows);
}
