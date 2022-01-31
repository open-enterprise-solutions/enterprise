#include "widgets.h"
#include "form.h"
#include "frontend/controls/textCtrl.h"
#include "metadata/objects/baseObject.h"
#include "metadata/singleMetaTypes.h"
#include "metadata/metadata.h"

CLASS_ID CValueTextCtrl::GetTypeClsid()
{
	CLASS_ID clsid = 0;

	if (m_source != wxNOT_FOUND && m_formOwner->GetSourceObject()) {
		IMetaAttributeObject *metaObject = NULL;
		if (m_formOwner) {
			IMetaObjectValue *metaObjectValue = m_formOwner->GetMetaObject();
			if (metaObjectValue) {
				metaObject = wxDynamicCast(
					metaObjectValue->FindMetaObjectByID(m_source), IMetaAttributeObject
				);
			}
			if (metaObject != NULL) {
				clsid = metaObject->GetClassTypeObject();
			}
		}
	}
	else {
		IMetadata *metaData = GetMetaData();
		if (m_typeDescription.GetTypeObject() >= defaultMetaID) {
			IMetaObjectValue *metaObject = wxDynamicCast(
				metaData->GetMetaObject(m_typeDescription.GetTypeObject()), IMetaObjectValue
			);
			wxASSERT(metaObject);

			IMetaTypeObjectValueSingle *clsFactory =
				metaObject->GetTypeObject(eMetaObjectType::enReference);
			wxASSERT(clsFactory);
			clsid = clsFactory->GetTypeID();
		}
		else {
			clsid = CValue::GetIDByVT((const eValueTypes)m_typeDescription.GetTypeObject());
		}
	}

	return clsid;
}

bool CValueTextCtrl::TextProcessing(const wxString &stringData)
{
	CLASS_ID clsid = GetTypeClsid();
	if (clsid > 0) {
		IMetadata *metaData = GetMetaData();	
		wxString className =
			metaData->GetNameObjectFromID(clsid);
		CValue newValue = metaData->CreateObject(className);
		if (stringData.Length() > 0) {
			std::vector<CValue> foundedObjects;
			if (newValue.FindValue(stringData, foundedObjects)) {
				SetControlValue(foundedObjects.at(0));
			}
		}
		else {
			SetControlValue(newValue);
		}
		return true;
	}
	return false;
}

void CValueTextCtrl::OnTextEnter(wxCommandEvent &event)
{
	wxTextCtrl *textCtrl = wxDynamicCast(
		event.GetEventObject(), wxTextCtrl
	);

	if (TextProcessing(textCtrl->GetValue())) {
		textCtrl->SetValue(m_selValue.GetString());
	}

	event.Skip();
}

void CValueTextCtrl::OnKillFocus(wxFocusEvent &event)
{
	wxTextCtrl *textCtrl = wxDynamicCast(
		event.GetEventObject(), wxTextCtrl
	);

	if (TextProcessing(textCtrl->GetValue())) {
		textCtrl->SetValue(m_selValue.GetString());
	}

	event.Skip();
}

void CValueTextCtrl::OnSelectButtonPressed(wxCommandEvent &event)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent("startChoice", selfControl, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}

	if (m_source != wxNOT_FOUND && m_formOwner->GetSourceObject()) {
		IMetaObject *metaObject = NULL;
		if (m_formOwner) {
			IMetaObjectValue *metaObjectValue = m_formOwner->GetMetaObject();
			if (metaObjectValue) {
				metaObject = metaObjectValue->FindMetaObjectByID(m_source);
			}
		}
		if (metaObject) {
			metaObject->ProcessChoice(this, m_choiceForm);
		}
	}
	else {
		IMetadata *metaData = GetMetaData();
		if (metaData) {
			IMetaObject* metaObject = metaData->GetMetaObject(m_typeDescription.GetTypeObject());
			if (metaObject) {
				metaObject->ProcessChoice(this, m_choiceForm);
			}
		}
	}
}

void CValueTextCtrl::OnListButtonPressed(wxCommandEvent &event)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent("startListChoice", selfControl, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}

}

void CValueTextCtrl::OnClearButtonPressed(wxCommandEvent &event)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent("clearing", selfControl, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}

	SetControlValue(CValue());
}

/////////////////////////////////////////////////////////////////

#include "form.h"
#include "frontend/visualView/visualEditorView.h"

CValue CValueTextCtrl::GetControlValue() const
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

#include "metadata/singleMetaTypes.h"
#include "compiler/valueTypeDescription.h"

void CValueTextCtrl::SetControlValue(CValue &vSelected)
{
	CValueForm *ownerForm = GetOwnerForm();

	if (m_source != wxNOT_FOUND && m_formOwner->GetSourceObject()) {
		IDataObjectSource *srcObject = ownerForm->GetSourceObject();
		if (srcObject) {
			srcObject->SetValueByMetaID(m_source, vSelected);
		}
	}

	IMetadata *metaData = GetMetaData();

	if (m_source != wxNOT_FOUND && m_formOwner->GetSourceObject()) {
		IMetaAttributeObject *metaObject = NULL; 
		if (m_formOwner) {
			IMetaObjectValue *metaObjectValue = m_formOwner->GetMetaObject();
			if (metaObjectValue) {
				metaObject = wxDynamicCast(
					metaObjectValue->FindMetaObjectByID(m_source), IMetaAttributeObject
				);
			}
			wxASSERT(metaObject);
			CValueTypeDescription *td = new CValueTypeDescription(GetTypeClsid(),
				metaObject->GetNumberQualifier(), metaObject->GetDateQualifier(), metaObject->GetStringQualifier());

			m_selValue = td->AdjustValue(vSelected);
			wxDELETE(td);
		}
	}
	else {

		CValueTypeDescription *td = new CValueTypeDescription(GetTypeClsid(),
			GetNumberQualifier(), GetDateQualifier(), GetStringQualifier());

		m_selValue = td->AdjustValue(vSelected);
	}

	CTextCtrl *textCtrl = dynamic_cast<CTextCtrl *>(GetWxObject());
	if (textCtrl) {
		textCtrl->SetTextValue(m_selValue.GetString());
	}
}

void CValueTextCtrl::ChoiceProcessing(CValue &vSelected)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent("choiceProcessing", selfControl, vSelected, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}

	SetControlValue(vSelected);

	IValueControl::CallEvent("onChange", selfControl);
}