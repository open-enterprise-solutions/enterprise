#include "widgets.h"
#include "frontend/controls/checkBoxEditor.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueCheckbox, IValueWindow)

//****************************************************************************

#include "form.h"
#include "core/metadata/metadata.h"

ISourceObject* CValueCheckbox::GetSourceObject() const
{
	return m_formOwner ? m_formOwner->GetSourceObject()
		: NULL;
}

//****************************************************************************
//*                              Checkbox                                    *
//****************************************************************************

CValueCheckbox::CValueCheckbox() : IValueWindow(), ITypeControl(eValueTypes::TYPE_BOOLEAN)
{
}

wxObject* CValueCheckbox::Create(wxWindow* wxparent, IVisualHost* visualHost)
{
	CCheckBox* checkbox = new CCheckBox(wxparent, wxID_ANY,
		m_propertyCaption->GetValueAsString(),
		wxDefaultPosition,
		wxDefaultSize);

	checkbox->BindCheckBoxCtrl(&CValueCheckbox::OnClickedCheckbox, this);

	return checkbox;
}

void CValueCheckbox::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)
{
}

void CValueCheckbox::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	CCheckBox* checkbox = dynamic_cast<CCheckBox*>(wxobject);

	if (checkbox != NULL) {
		wxString textCaption = wxEmptyString;
		if (m_dataSource.isValid()) {
			ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
			if (srcObject != NULL) {
				IMetaObjectWrapperData* objMetaValue = srcObject->GetMetaObject();
				IMetaObject* metaObject = objMetaValue->FindMetaObjectByID(m_dataSource);
				if (metaObject != NULL) {
					textCaption = metaObject->GetSynonym() + wxT(":");
				}
				srcObject->GetValueByMetaID(GetIdByGuid(m_dataSource), m_selValue);
			}
		}

		checkbox->SetCheckBoxLabel(!m_propertyCaption->IsOk() ?
			textCaption : m_propertyCaption->GetValueAsString());
		checkbox->SetCheckBoxValue(m_selValue.GetBoolean());
		checkbox->SetWindowStyle(
			m_propertyTitle->GetValueAsInteger() == 1 ? wxALIGN_LEFT :
			wxALIGN_RIGHT
		);

		checkbox->BindCheckBoxCtrl(&CValueCheckbox::OnClickedCheckbox, this);
	}

	UpdateWindow(checkbox);
	UpdateLabelSize(checkbox);
}

void CValueCheckbox::Cleanup(wxObject* obj, IVisualHost* visualHost)
{
	CCheckBox* checkbox = dynamic_cast<CCheckBox*>(obj);
	if (checkbox) {
		checkbox->UnbindCheckBoxCtrl(&CValueCheckbox::OnClickedCheckbox, this);
	}
}

//*******************************************************************
//*							 Control value	                        *
//*******************************************************************

bool CValueCheckbox::GetControlValue(CValue& pvarControlVal) const
{
	CValueForm* ownerForm = GetOwnerForm();
	if (m_dataSource.isValid() && m_formOwner->GetSourceObject()) {
		ISourceDataObject* srcObject = ownerForm->GetSourceObject();
		if (srcObject != NULL) {
			return srcObject->GetValueByMetaID(GetIdByGuid(m_dataSource), pvarControlVal);
		}
	}
	pvarControlVal = m_selValue;
	return true; 
}

#include "core/compiler/valueType.h"
#include "frontend/controls/checkBoxEditor.h"

bool CValueCheckbox::SetControlValue(const CValue& varControlVal)
{
	if (m_dataSource.isValid() && m_formOwner->GetSourceObject()) {
		ISourceDataObject* srcObject = m_formOwner->GetSourceObject();
		if (srcObject != NULL) {
			srcObject->SetValueByMetaID(GetIdByGuid(m_dataSource), varControlVal);
		}
	}

	m_selValue = varControlVal.GetBoolean();

	CCheckBox* checkboxCtrl = dynamic_cast<CCheckBox*>(GetWxObject());
	if (checkboxCtrl != NULL) {
		checkboxCtrl->SetCheckBoxValue(varControlVal.GetBoolean());
	}

	return true;
}

//*******************************************************************
//*							 Data	                                *
//*******************************************************************

bool CValueCheckbox::LoadData(CMemoryReader& reader)
{
	wxString caption; reader.r_stringZ(caption);
	m_propertyCaption->SetValue(caption);
	m_propertyTitle->SetValue(reader.r_s32());

	if (!ITypeControl::LoadTypeData(reader))
		return false;

	return IValueWindow::LoadData(reader);
}

bool CValueCheckbox::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(m_propertyCaption->GetValueAsString());
	writer.w_s32(m_propertyTitle->GetValueAsInteger());

	if (!ITypeControl::SaveTypeData(writer))
		return false;

	return IValueWindow::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_VALUE_REGISTER(CValueCheckbox, "checkbox", "widget", TEXT2CLSID("CT_CHKB"));