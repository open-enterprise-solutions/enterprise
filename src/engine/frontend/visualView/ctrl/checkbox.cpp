#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#else
#include "frontend/win/ctrls/controlCheckboxEditor.h"
#endif


//****************************************************************************

#include "form.h"
#include "backend/metaData.h"

ibSourceObject* ibValueCheckbox::GetSourceObject() const
{
	return m_formOwner != nullptr ?
		m_formOwner->GetSourceObject() : nullptr;
}

bool ibValueCheckbox::GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const
{
	return m_formOwner != nullptr ? m_formOwner->GetSourceList(GetFilterSourceDataType(), out) : false;
}

//****************************************************************************
//*                              Checkbox                                    *
//****************************************************************************

enum prop {
	eControlValue,
};

ibValueCheckbox::ibValueCheckbox() : ibValueWindow(), ibTypeControlFactory()//(ibValueTypes::TYPE_BOOLEAN)
{
	m_members.Bind(this, &ibValueCheckbox::FillControlMembers);
}

const ibMetaData* ibValueCheckbox::GetMetaData() const
{
	return m_formOwner != nullptr ?
		m_formOwner->GetMetaData() : nullptr;
}

void ibValueCheckbox::FillControlMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("Value"), eControlValue, eControl);
}

bool ibValueCheckbox::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum); bool refreshColumn = false;
	if (lPropAlias == eControl) {
		const long lPropData = m_members.GetPropData(lPropNum);
		if (lPropData == eControlValue) {
			SetControlValue(varPropVal);
		}
	}

	return ibValueFrame::SetPropVal(lPropNum, varPropVal);
}

bool ibValueCheckbox::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eControl) {
		const long lPropData = m_members.GetPropData(lPropNum);
		if (lPropData == eControlValue) {
			return GetControlValue(pvarPropVal);
		}
	}
	return ibValueFrame::GetPropVal(lPropNum, pvarPropVal);
}

wxString ibValueCheckbox::GetControlTitle() const
{
	if (!m_propertyTitle->IsEmptyProperty()) {
		return m_propertyTitle->GetValueAsTranslateString();
	}
	else if (!m_propertySource->IsEmptyProperty()) {
		const ibBackendSourceColumn* column = m_propertySource->GetSourceAttributeObject();
		if (column != nullptr)   // null when the bound field is gone / whole-attribute binding
			return column->GetSynonym();
	}
	return wxEmptyString;
}

wxObject* ibValueCheckbox::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
	(void)visualHost;
#ifdef OES_USE_WEB
	(void)wxparent;
	auto* checkbox = new ibWebCheckBox(GetControlID());
#else
	auto* checkbox = new wxControlNavigationCheckbox(
		wxparent, wxID_ANY, wxDefaultPosition, wxDefaultSize);
#endif
	checkbox->Bind(wxEVT_CHECKBOX, &ibValueCheckbox::OnClickedCheckbox, this);
	return checkbox;
}

void ibValueCheckbox::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
}

#include "backend/appData.h"

void ibValueCheckbox::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	(void)visualHost;
#ifdef OES_USE_WEB
	ibWebCheckBox* checkbox = static_cast<ibWebCheckBox*>(wxobject);
#else
	ibControlCheckbox* checkbox = static_cast<ibControlCheckbox*>(wxobject);
#endif

	if (checkbox != nullptr) {
		// Source-backed value refresh: hand the source the binding path; it walks it.
		if (!m_propertySource->IsEmptyProperty() && m_formOwner != nullptr) {
			m_formOwner->GetValueByAttributePath(m_propertySource->GetValueAsPath(), m_selValue);
		}

		checkbox->SetLabel(GetControlTitle());
		// A dotted reference path is read-only; unbound = editable.
		const bool writableBinding = m_propertySource->IsEmptyProperty()
			|| (m_formOwner != nullptr && m_formOwner->IsWritableBinding(m_propertySource->GetValueAsPath()));
		if (!writableBinding)
			checkbox->Enable(false);
#ifdef OES_USE_WEB
		checkbox->SetValue(m_selValue.GetBoolean());
#else
		if (!appData->DesignerMode())
			checkbox->SetValue(m_selValue.GetBoolean());
		checkbox->SetWindowStyle(
			m_propertyTitleLocation->GetValueAsInteger() == 1 ? wxALIGN_LEFT
			                                                  : wxALIGN_RIGHT);
#endif
	}

	UpdateWindow(checkbox);
}

void ibValueCheckbox::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)obj;
	(void)visualHost;
#else
	ibControlCheckbox* checkbox = static_cast<ibControlCheckbox*>(obj);
	if (checkbox != nullptr) {
		checkbox->Unbind(wxEVT_COMMAND_CHECKBOX_CLICKED, &ibValueCheckbox::OnClickedCheckbox, this);
	}
#endif
}

//*******************************************************************
//*							 Control value	                        *
//*******************************************************************

bool ibValueCheckbox::GetControlValue(ibValue& pvarControlVal) const
{
	if (!m_propertySource->IsEmptyProperty() && m_formOwner != nullptr &&
		m_formOwner->GetValueByAttributePath(m_propertySource->GetValueAsPath(), pvarControlVal)) {
		return true;   // attribute-table / dotted path -> read-only walk
	}

	pvarControlVal = ibTypeControlFactory::AdjustValue(m_selValue);
	return true;
}

#include "backend/system/value/valueType.h"

bool ibValueCheckbox::SetControlValue(const ibValue& varControlVal)
{
	if (!m_propertySource->IsEmptyProperty() && m_formOwner != nullptr) {
		// Form writes only a direct-field binding (head selects the attribute); a
		// dotted reference path is read-only → no-op.
		m_formOwner->SetValueByAttributePath(m_propertySource->GetValueAsPath(), varControlVal);
	}

	m_selValue = varControlVal.GetBoolean();

#ifdef OES_USE_WEB
	// Web: the next CreateAndUpdateVisualHost pass re-reads m_selValue
	// from the source and emits it as the new JSON. No live node to
	// poke — the rebuild is stateless.
#else
	ibControlCheckbox* checkboxCtrl = static_cast<ibControlCheckbox*>(GetWxObject());
	if (checkboxCtrl != nullptr) {
		checkboxCtrl->SetValue(varControlVal.GetBoolean());
	}
#endif

	return true;
}

//*******************************************************************
//*							 Data	                                *
//*******************************************************************

bool ibValueCheckbox::ReadData(const ibDataNode& node)
{
	m_propertyTitle->ReadNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyTitleLocation->ReadNodeValue(node.GetProperty(m_propertyTitleLocation->GetName()));
	m_propertySource->ReadNodeValue(node.GetProperty(m_propertySource->GetName()));

	//events
	m_onCheckboxClicked->ReadNodeValue(node.GetProperty(m_onCheckboxClicked->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueCheckbox::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyTitleLocation->GetName(), m_propertyTitleLocation->GetNodeValue());
	node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());

	//events
	node.SetProperty(m_onCheckboxClicked->GetName(), m_onCheckboxClicked->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueCheckbox, "Checkbox", "Widget", string_to_clsid("CT_CHKB"));