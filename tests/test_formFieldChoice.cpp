// =============================================================================
// OES Enterprise — what is chosen into a form's field (issue #259).
//
// A form is a SOURCE over its own attributes (ibValueForm : ibSourceDataObject): its explorer is made of them, and a
// hop is an attribute's id. A form's own attribute carries no link — one reached through the dot could not be given
// any — so a link on a form lives on its CONTROL, a field or a table column, and the form is the source its paths are
// read in. These pin:
//
//   * a control's own rows are read off the form (ibChoiceLinkResolver::ResolveParameters, the form as the holder);
//   * they are offered what the form reaches — never the field the control is bound to;
//   * a value written through a control empties the controls whose own rows name its field, unless the row says
//     Keep, on a change only (ibTypeControlFactory::ClearLinked).
//
// An Input field's own word and own links, saved and loaded: test_formField.cpp.
//
// DB-FREE AND UI-FREE: a fresh ibMetaDataApplicationFile that is never run, a form made in the test on a session with
// a frame (framedSession.h).
// =============================================================================

#include <gtest/gtest.h>

#include <wx/init.h>
#include <wx/log.h>                                  // wxLogStderr — a warning must not become a modal box

#include "backend/metadataApplication.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/choiceLinkResolver.h"
#include "backend/propertyManager/property/propertyChoiceLink.h"
#include "backend/propertyManager/property/variant/variantChoiceLink.h"   // ibChoiceHolderFields — what a row may read
#include "backend/appData.h"                      // the environment a form is made in
#include "core/serialize/dataBuilder.h"

#include "frmserver/visualView/ctrl/frame.h"         // ibControlFrame — what a value is written through
#include "frmserver/visualView/ctrl/typeControl.h"
#include "frmserver/visualView/ctrl/field.h"         // g_controlFieldCLSID
#include "frmserver/visualView/ctrl/form.h"
#include "frmserver/visualView/ctrl/formAttribute.h"
#include "framedSession.h"                           // KeyFrame, FramedSession — the frame a form is born against

#include <memory>

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

// A form made in the test answers with the test's application, as a form opened from one does.
struct ChoiceForm : ibValueForm {
	const ibMetaData* m_metaData = nullptr;
	const ibMetaData* GetMetaData() const override { return m_metaData; }
};

// The contracts catalog, whose Counterparty and Organisation a contract's choice is narrowed by — and a form with a
// counterparty, an organisation and a contract of its own.
struct FormAttributeChoice : ::testing::Test {
	wxInitializer m_wxInit;
	ibMetaDataApplicationFile cfg;

	ibValueMetaObjectAttributeBase* contractCounterparty = nullptr;   // parameters: fields of the chosen contract
	ibValueMetaObjectAttributeBase* contractOrganisation = nullptr;

	// A form is born against its session's frame (framedSession.h).
	std::shared_ptr<FramedSession> session;
	std::unique_ptr<KeyFrame> frame;
	std::unique_ptr<ibSessionScope> scope;

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

		ibValueMetaObject* contracts = cfg.CreateMetaObject(g_metaCatalogCLSID, cfg.GetRootMetaObject(), /*runObject*/ false);
		ASSERT_NE(contracts, nullptr);
		contracts->SetName(wxT("Contracts"));
		contractCounterparty = Attribute(contracts, wxT("Counterparty"));
		contractOrganisation = Attribute(contracts, wxT("Organisation"));
		ASSERT_TRUE(contractCounterparty && contractOrganisation);

		session = std::make_shared<FramedSession>(wxT("form-attribute-choice"), ibSessionKind::Workspace);
		frame = std::make_unique<KeyFrame>(ibApplicationInstance::GetSessionRegistry()->MintUnlisted(session));
		session->Attach(frame.get());
		scope = std::make_unique<ibSessionScope>(session.get());

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
		scope.reset();
		frame.reset();
		session.reset();
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

// A field on the form, bound to `attribute`, as the studio's add makes one.
ibTypeControlFactory* BoundField(ibValueForm* form, const ibFormAttributeValue* attribute)
{
	ibTypeControlFactory* control = dynamic_cast<ibTypeControlFactory*>(form->NewObject(g_controlFieldCLSID, form));
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
	ibTypeControlFactory* control = BoundField(form, contract);
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
	ibTypeControlFactory* contractField = BoundField(form, contract);
	ibControlFrame* organisationField = dynamic_cast<ibControlFrame*>(BoundField(form, organisation));
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
