#include "widgets.h"
#include "form.h"
#include "metadata/objects/baseObject.h"
#include "metadata/metadata.h"

//*******************************************************************
//*                             Events                              *
//*******************************************************************

void CValueCheckbox::OnClickedCheckbox(wxCommandEvent& event)
{
	wxCheckBox *checkbox =
		wxDynamicCast(event.GetEventObject(), wxCheckBox);

	m_selValue = checkbox->GetValue();

	if (m_source != wxNOT_FOUND && m_formOwner->GetSourceObject()) {
		IDataObjectSource *srcData = m_formOwner->GetSourceObject();
		wxASSERT(srcData);
		srcData->SetValueByMetaID(m_source, m_selValue);
	}

	event.Skip(CallEvent("onCheckboxClicked"));
}

CValue CValueCheckbox::GetControlValue() const
{
	CValueForm *ownerForm = GetOwnerForm();

	if (m_source != wxNOT_FOUND && m_formOwner->GetSourceObject()) {
		IDataObjectSource *srcObject = ownerForm->GetSourceObject();
		if (srcObject) {
			return srcObject->GetValueByMetaID(m_source);
		}
	}

	return m_selValue;
}

#include "compiler/valueTypeDescription.h"
#include "frontend/controls/checkBoxCtrl.h"

void CValueCheckbox::SetControlValue(CValue &vSelected)
{
	CValueForm *ownerForm = GetOwnerForm();

	if (m_source != wxNOT_FOUND && m_formOwner->GetSourceObject()) {
		IDataObjectSource *srcObject = ownerForm->GetSourceObject();
		if (srcObject) {
			srcObject->SetValueByMetaID(m_source, vSelected);
		}
	}

	m_selValue = vSelected.GetBoolean(); 

	CCheckBox *checkboxCtrl = dynamic_cast<CCheckBox *>(GetWxObject());
	if (checkboxCtrl) {
		checkboxCtrl->SetCheckBoxValue(vSelected.GetBoolean());
	}

	ownerForm->Modify(true);
}