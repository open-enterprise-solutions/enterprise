// =============================================================================
// OES Enterprise — what is chosen into a form's field (issue #259).
//
// A form is a SOURCE over its own attributes (ibValueForm : ibSourceDataObject): its explorer is made of them, and a
// hop is an attribute's id. A form's own attribute carries no link — one reached through the dot could not be given
// any — so a link on a form lives on its CONTROL, a text field or a table column, and the form is the source its paths
// are read in. These pin:
//
//   * a control's own rows are read off the form (ibChoiceLinkResolver::ResolveParameters, the form as the holder);
//   * they are offered what the form reaches — never the field the control is bound to;
//   * a value written through a control empties the controls whose own rows name its field, unless the row says
//     Keep, on a change only (ibTypeControlFactory::ClearLinked);
//   * a Textctrl's QuickChoice and ChoiceFoldersAndItems answer its doors HasQuickChoice and GetSelectMode, and
//     survive the form's save and load;
//   * a Textctrl's own TypeLink and ChoiceParameters (#616) survive the save and load, a path through the dot whole.
//
// DB-FREE AND UI-FREE: a fresh ibMetaDataConfigurationFile that is never run, a form made in the test, and a control
// made by its own ctor.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/init.h>
#include <wx/log.h>                                  // wxLogStderr — a warning must not become a modal box

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/choiceLinkResolver.h"
#include "backend/propertyManager/property/propertyChoiceLink.h"
#include "backend/propertyManager/property/variant/variantChoiceLink.h"   // ibChoiceHolderFields — what a row may read
#include "backend/appData.h"                      // the environment a form is made in
#include "backend/objCtor.h"                         // ibCtorAbstractType — a control made by its own ctor
#include "core/serialize/dataBuilder.h"

#include "frmserver/visualView/ctrl/frame.h"         // ibValueFrame — what a control saves and loads through
#include "frmserver/visualView/ctrl/typeControl.h"
#include "frmserver/visualView/ctrl/widgets.h"       // g_controlTextCtrlCLSID
#include "frmserver/visualView/ctrl/form.h"
#include "frmserver/visualView/ctrl/formAttribute.h"

namespace {

// A row: `parameter` (a field of what is chosen) takes its value from `source` (an attribute of the form).
ibChoiceParameterRowDescription Row(const ibMetaID& parameter, const ibMetaID& source,
	ibChoiceParameterOnChange onChange = ibChoiceParameterOnChange::Clear)
{
	ibChoiceParameterRowDescription row;
	row.m_parameter = parameter;
	row.m_source.AppendSource(source);
	row.m_onChange = onChange;
	return row;
}

// A form made in the test answers with the test's configuration, as a form opened from one does.
struct ChoiceForm : ibValueForm {
	const ibMetaData* m_metaData = nullptr;
	const ibMetaData* GetMetaData() const override { return m_metaData; }
};

// The contracts catalog, whose Counterparty and Organisation a contract's choice is narrowed by — and a form with a
// counterparty, an organisation and a contract of its own.
struct FormAttributeChoice : ::testing::Test {
	wxInitializer m_wxInit;
	ibMetaDataConfigurationFile cfg;

	ibValueMetaObjectAttributeBase* contractCounterparty = nullptr;   // parameters: fields of the chosen contract
	ibValueMetaObjectAttributeBase* contractOrganisation = nullptr;

	ChoiceForm* form = nullptr;
	ibValue formValue;   // holds the form
	ibFormAttributeValue* counterparty = nullptr;
	ibFormAttributeValue* organisation = nullptr;
	ibFormAttributeValue* contract = nullptr;

	ibValueMetaObjectAttributeBase* Attribute(ibValueMetaObject* owner, const wxString& name) {
		auto* attribute = dynamic_cast<ibValueMetaObjectAttributeBase*>(cfg.CreateMetaObject(g_metaAttributeCLSID, owner, false));
		if (attribute != nullptr)
			attribute->SetName(name);
		return attribute;
	}

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (wxLog::GetActiveTarget() != nullptr)
			delete wxLog::SetActiveTarget(new wxLogStderr());
		if (ibApplicationInstance::Get() == nullptr && !ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";

		ibValueMetaObject* contracts = cfg.CreateMetaObject(g_metaCatalogCLSID, cfg.GetCommonMetaObject(), /*runObject*/ false);
		ASSERT_NE(contracts, nullptr);
		contracts->SetName(wxT("Contracts"));
		contractCounterparty = Attribute(contracts, wxT("Counterparty"));
		contractOrganisation = Attribute(contracts, wxT("Organisation"));
		ASSERT_TRUE(contractCounterparty && contractOrganisation);

		form = new ChoiceForm();
		formValue = ibValue(static_cast<ibValue*>(form));
		form->m_metaData = &cfg;
		counterparty = form->AddAttribute(wxT("Counterparty"), g_valueStringCLSID, ibValue(ibString(wxT("Acme"))));
		organisation = form->AddAttribute(wxT("Organisation"), g_valueStringCLSID, ibValue(ibString(wxT("Our firm"))));
		contract = form->AddAttribute(wxT("Contract"), g_valueStringCLSID, ibValue(ibString(wxT("No. 7"))));
		ASSERT_TRUE(counterparty && organisation && contract);
	}

	void TearDown() override {
		formValue = ibValue();
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}

	ibString ValueOf(const ibFormAttributeValue* attribute) const {
		ibValue value;
		attribute->GetValueByPath({}, value);
		return value.GetString();
	}
};

} // namespace

// The form lists its attributes, and a control's own rows naming them are read off it — the form is their source.
TEST_F(FormAttributeChoice, TheFormIsTheSourceAControlsRowsAreReadIn)
{
	const ibSourceDataObject::ibSourceExplorer* fields = form->GetSourceExplorer();
	ASSERT_NE(fields, nullptr);
	EXPECT_EQ(fields->GetHelperCount(), 3u);
	ASSERT_NE(fields->FindById(contract->GetId()), nullptr);
	EXPECT_TRUE(fields->FindById(contract->GetId())->GetSourceName() == wxT("Contract"));

	ibChoiceParametersDescription rows;
	rows.SetRow(Row(contractCounterparty->GetMetaID(), counterparty->GetId()));
	rows.SetRow(Row(contractOrganisation->GetMetaID(), organisation->GetId()));

	ibChoiceCondition condition;
	ibChoiceLinkResolver::ResolveParameters(rows, ibChoiceHolder(form), condition);

	ASSERT_EQ(condition.m_parameters.size(), 2u);
	EXPECT_TRUE(condition.m_parameters.at(wxT("Counterparty")).GetString() == wxT("Acme"));
	EXPECT_TRUE(condition.m_parameters.at(wxT("Organisation")).GetString() == wxT("Our firm"));
}

namespace {

// A Textctrl made by its class's own ctor, the way the registry makes one, with no form around it. Asked through the
// exported classes it derives — the frame saves, loads and answers the quick choice, the factory the select mode.
ibValue MakeTextctrl()
{
	ibCtorAbstractType* ctor = ibValue::GetAvailableCtor(g_controlTextCtrlCLSID);
	return ctor != nullptr ? ctor->CreateObject() : ibValue();
}

struct TextctrlUnderTest {
	ibValue m_value = MakeTextctrl();
	ibValueFrame* m_frame = dynamic_cast<ibValueFrame*>(m_value.GetRef());
	ibTypeControlFactory* m_factory = dynamic_cast<ibTypeControlFactory*>(m_value.GetRef());

	bool IsOk() const { return m_frame != nullptr && m_factory != nullptr; }
	ibPropertyEnumBase* Property(const wxString& name) const { return dynamic_cast<ibPropertyEnumBase*>(m_frame->GetProperty(name)); }
};

} // namespace

namespace {

// A Textctrl on the form, bound to `attribute`, as the designer's add makes one.
ibTypeControlFactory* BoundTextctrl(ibValueForm* form, const ibFormAttributeValue* attribute)
{
	ibTypeControlFactory* control = dynamic_cast<ibTypeControlFactory*>(form->NewObject(g_controlTextCtrlCLSID, form));
	if (control != nullptr)
		control->SetDefaultSourceType(ibSourceDescription(attribute->GetId()));
	return control;
}

ibPropertyChoiceParameters* OwnRows(ibTypeControlFactory* control)
{
	ibPropertyObject* frame = dynamic_cast<ibPropertyObject*>(control);
	return frame != nullptr ? dynamic_cast<ibPropertyChoiceParameters*>(frame->GetProperty(wxT("ChoiceParameters"))) : nullptr;
}

} // namespace

// A control's own rows are offered what the form reaches — never the field it is bound to.
TEST_F(FormAttributeChoice, AControlIsOfferedWhatTheFormReaches)
{
	ibTypeControlFactory* control = BoundTextctrl(form, contract);
	ASSERT_NE(control, nullptr);
	ASSERT_NE(OwnRows(control), nullptr);

	const std::vector<ibChoiceHolderField> sources = ibChoiceHolderFields(OwnRows(control)->GetPropertyObject());
	ASSERT_EQ(sources.size(), 2u);
	EXPECT_TRUE(sources[0].m_spelled == wxT("Counterparty"));
	EXPECT_TRUE(sources[1].m_spelled == wxT("Organisation"));
	EXPECT_EQ(sources[1].m_path.GetHopCount(), 1u);
}

// Another organisation is typed into ITS field: the contract field, whose OWN row reads the organisation, is emptied
// through its own door — the attribute itself declares nothing. The same organisation again, and a row that says Keep,
// empty nothing.
TEST_F(FormAttributeChoice, AControlsOwnRowEmptiesItsFieldWhenTheFieldItNamesChanges)
{
	ibTypeControlFactory* contractField = BoundTextctrl(form, contract);
	ibControlFrame* organisationField = dynamic_cast<ibControlFrame*>(BoundTextctrl(form, organisation));
	ASSERT_NE(contractField, nullptr);
	ASSERT_NE(organisationField, nullptr);
	ASSERT_NE(OwnRows(contractField), nullptr);

	ibChoiceParametersDescription clear;
	clear.SetRow(Row(contractOrganisation->GetMetaID(), organisation->GetId()));
	OwnRows(contractField)->SetValue(clear);

	organisationField->SetControlValue(ibValue(ibString(wxT("Another firm"))));
	EXPECT_TRUE(ValueOf(contract).IsEmpty());

	contract->SetValueByPath({}, ibValue(ibString(wxT("No. 9"))));
	organisationField->SetControlValue(ibValue(ibString(wxT("Another firm"))));
	EXPECT_TRUE(ValueOf(contract) == wxT("No. 9"));

	ibChoiceParametersDescription keep;
	keep.SetRow(Row(contractOrganisation->GetMetaID(), organisation->GetId(), ibChoiceParameterOnChange::Keep));
	OwnRows(contractField)->SetValue(keep);
	organisationField->SetControlValue(ibValue(ibString(wxT("Our firm"))));
	EXPECT_TRUE(ValueOf(contract) == wxT("No. 9"));
}

// The field's word over its type's, through its own doors; Auto leaves the type's answer standing — and the two
// properties go out with the form and come back with it.
TEST(FormFieldChoice, TextctrlSaysItsWordInItsDoors)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	TextctrlUnderTest written;
	ASSERT_TRUE(written.IsOk());
	ibPropertyEnumBase* quickChoice = written.Property(wxT("QuickChoice"));
	ibPropertyEnumBase* foldersAndItems = written.Property(wxT("ChoiceFoldersAndItems"));
	ASSERT_NE(quickChoice, nullptr);
	ASSERT_NE(foldersAndItems, nullptr);

	// Nothing said: the type's answer (no configuration here: none) and the holder's (items).
	EXPECT_FALSE(written.m_frame->HasQuickChoice());
	EXPECT_EQ(written.m_factory->GetSelectMode(), ibSelectMode::ibSelectMode_Items);

	quickChoice->SetValue(static_cast<int>(ibQuickChoiceMode_Use));
	EXPECT_TRUE(written.m_frame->HasQuickChoice());
	quickChoice->SetValue(static_cast<int>(ibQuickChoiceMode_DontUse));
	EXPECT_FALSE(written.m_frame->HasQuickChoice());
	foldersAndItems->SetValue(static_cast<int>(ibChoiceFoldersAndItems_FoldersAndItems));
	EXPECT_EQ(written.m_factory->GetSelectMode(), ibSelectMode::ibSelectMode_FoldersAndItems);

	ibDataNode node;
	ASSERT_TRUE(written.m_frame->WriteData(node));

	TextctrlUnderTest read;
	ASSERT_TRUE(read.IsOk());
	ASSERT_TRUE(read.m_frame->ReadData(node));
	EXPECT_FALSE(read.m_frame->HasQuickChoice());
	EXPECT_EQ(read.m_factory->GetSelectMode(), ibSelectMode::ibSelectMode_FoldersAndItems);
}

// A field's OWN links (#616) go out with the form and come back with it — a path through the dot whole.
TEST(FormFieldChoice, TextctrlKeepsItsOwnLinks)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	TextctrlUnderTest written;
	ASSERT_TRUE(written.IsOk());
	ibPropertyChoiceLink* link = dynamic_cast<ibPropertyChoiceLink*>(written.m_frame->GetProperty(wxT("TypeLink")));
	ibPropertyChoiceParameters* rows = dynamic_cast<ibPropertyChoiceParameters*>(written.m_frame->GetProperty(wxT("ChoiceParameters")));
	ASSERT_NE(link, nullptr);
	ASSERT_NE(rows, nullptr);

	ibChoiceTypeLinkDescription linkDesc;
	linkDesc.m_source.AppendSource(3);
	linkDesc.m_source.AppendSource(17);   // Object.Kind — through the dot
	link->SetValue(linkDesc);
	ibChoiceParametersDescription paramsDesc;
	paramsDesc.SetRow(Row(41, 3));
	rows->SetValue(paramsDesc);
	EXPECT_TRUE(written.m_factory->GetTypeLink() == linkDesc);
	EXPECT_TRUE(written.m_factory->GetChoiceParameters() == paramsDesc);

	ibDataNode node;
	ASSERT_TRUE(written.m_frame->WriteData(node));

	TextctrlUnderTest read;
	ASSERT_TRUE(read.IsOk());
	ASSERT_TRUE(read.m_frame->ReadData(node));
	EXPECT_TRUE(read.m_factory->GetTypeLink() == linkDesc);
	EXPECT_TRUE(read.m_factory->GetChoiceParameters() == paramsDesc);
}
