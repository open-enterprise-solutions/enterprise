#include "widgets.h"
#include "frontend/win/ctrls/controlCheckboxEditor.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueCheckbox, ibValueWindow)

//****************************************************************************

#include "form.h"
#include "backend/metaData.h"

ibSourceObject* ibValueCheckbox::GetSourceObject() const
{
	return m_formOwner != nullptr ?
		m_formOwner->GetSourceObject() : nullptr;
}

//****************************************************************************
//*                              Checkbox                                    *
//****************************************************************************

enum prop {
	eControlValue,
};

ibValueCheckbox::ibValueCheckbox() : ibValueWindow(), ibTypeControlFactory()//(ibValueTypes::TYPE_BOOLEAN)
{
}

ibMetaData* ibValueCheckbox::GetMetaData() const
{
	return m_formOwner != nullptr ?
		m_formOwner->GetMetaData() : nullptr;
}

void ibValueCheckbox::PrepareNames() const
{
	ibValueFrame::PrepareNames();

	m_methodHelper->AppendProp(wxT("Value"), eControlValue, eControl);
}

bool ibValueCheckbox::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum); bool refreshColumn = false;
	if (lPropAlias == eControl) {
		const long lPropData = m_methodHelper->GetPropData(lPropNum);
		if (lPropData == eControlValue) {
			SetControlValue(varPropVal);
		}
	}

	return ibValueFrame::SetPropVal(lPropNum, varPropVal);
}

bool ibValueCheckbox::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eControl) {
		const long lPropData = m_methodHelper->GetPropData(lPropNum);
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
		const ibValueMetaObject* metaObject = m_propertySource->GetSourceAttributeObject();
		wxASSERT(metaObject);
		return metaObject->GetSynonym();
	}
	return wxEmptyString;
}

wxObject* ibValueCheckbox::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	ibControlCheckbox* checkbox = new wxControlNavigationCheckbox(wxparent, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	checkbox->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &ibValueCheckbox::OnClickedCheckbox, this);
	return checkbox;
}

void ibValueCheckbox::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
}

#include "backend/appData.h"

void ibValueCheckbox::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	ibControlCheckbox* checkbox = dynamic_cast<ibControlCheckbox*>(wxobject);

	if (checkbox != nullptr) {

		if (!m_propertySource->IsEmptyProperty()) {
			ibSourceDataObject* srcObject = m_formOwner->GetSourceObject();
			if (srcObject != nullptr) {
				srcObject->GetValueByMetaID(m_propertySource->GetValueAsSource(), m_selValue);
			}
		}

		checkbox->SetLabel(GetControlTitle());
		if (!appData->DesignerMode()) {
			checkbox->SetValue(m_selValue.GetBoolean());
		}
		checkbox->SetWindowStyle(
			m_propertyTitleLocation->GetValueAsInteger() == 1 ? wxALIGN_LEFT :
			wxALIGN_RIGHT
		);
	}

	UpdateWindow(checkbox);
}

void ibValueCheckbox::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
	ibControlCheckbox* checkbox = dynamic_cast<ibControlCheckbox*>(obj);
	if (checkbox != nullptr) {
		checkbox->Unbind(wxEVT_COMMAND_CHECKBOX_CLICKED, &ibValueCheckbox::OnClickedCheckbox, this);
	}
}

//*******************************************************************
//*							 Control value	                        *
//*******************************************************************

bool ibValueCheckbox::GetControlValue(ibValue& pvarControlVal) const
{
	if (!m_propertySource->IsEmptyProperty() && m_formOwner->GetSourceObject()) {
		ibSourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (srcObject != nullptr)
			return srcObject->GetValueByMetaID(m_propertySource->GetValueAsSource(), pvarControlVal);
	}

	pvarControlVal = ibTypeControlFactory::AdjustValue(m_selValue);
	return true;
}

#include "backend/system/value/valueType.h"

bool ibValueCheckbox::SetControlValue(const ibValue& varControlVal)
{
	if (!m_propertySource->IsEmptyProperty() && m_formOwner->GetSourceObject()) {
		ibSourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (srcObject != nullptr)
			srcObject->SetValueByMetaID(m_propertySource->GetValueAsSource(), varControlVal);
	}

	m_selValue = varControlVal.GetBoolean();

	ibControlCheckbox* checkboxCtrl = dynamic_cast<ibControlCheckbox*>(GetWxObject());
	if (checkboxCtrl != nullptr) {
		checkboxCtrl->SetValue(varControlVal.GetBoolean());
	}

	return true;
}

//*******************************************************************
//*							 Data	                                *
//*******************************************************************

bool ibValueCheckbox::LoadData(ibReaderMemory& reader)
{
	wxString title; reader.r_stringZ(title);
	m_propertyTitle->SetValue(title);
	m_propertyTitleLocation->SetValue(reader.r_s32());
	if (!m_propertySource->LoadData(reader))
		return false;

	//events
	m_onCheckboxClicked->LoadData(reader);
	return ibValueWindow::LoadData(reader);
}

bool ibValueCheckbox::SaveData(ibWriterMemory writer)
{
	writer.w_stringZ(m_propertyTitle->GetValueAsString());
	writer.w_s32(m_propertyTitleLocation->GetValueAsInteger());
	if (!m_propertySource->SaveData(writer))
		return false;

	//events
	m_onCheckboxClicked->SaveData(writer);
	return ibValueWindow::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueCheckbox, "Checkbox", "Widget", string_to_clsid("CT_CHKB"));