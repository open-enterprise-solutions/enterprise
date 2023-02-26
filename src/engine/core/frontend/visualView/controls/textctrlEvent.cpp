#include "widgets.h"
#include "frontend/controls/textEditor.h"
#include "core/metadata/metaObjects/objects/object.h"
#include "core/metadata/metadata.h"

bool CValueTextCtrl::TextProcessing(wxTextCtrl* textCtrl, const wxString& strData)
{
	IMetadata* metaData = GetMetaData();
	wxASSERT(metaData);
	CValue selValue; GetControlValue(selValue);
	const CValue& newValue = metaData->CreateObject(selValue.GetTypeClass());
	if (newValue.GetType() == eValueTypes::TYPE_EMPTY) {
		textCtrl->SetValue(selValue.GetString());
		textCtrl->SetInsertionPointEnd();
		return false;
	}
	if (strData.Length() > 0) {
		std::vector<CValue> foundedObjects;
		if (newValue.FindValue(strData, foundedObjects)) {
			SetControlValue(foundedObjects.at(0));
		}
		else {
			textCtrl->SetValue(selValue.GetString());
			textCtrl->SetInsertionPointEnd();	
			return false;
		}
	}
	else {
		SetControlValue(newValue);
	}

	IValueControl::CallEvent(m_eventOnChange, GetValue());
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////

void CValueTextCtrl::ChoiceProcessing(CValue& vSelected)
{
	CValue standartProcessing = true;
	IValueControl::CallEvent(m_eventChoiceProcessing, GetValue(), vSelected, standartProcessing);
	if (standartProcessing.GetBoolean()) {
		SetControlValue(vSelected);
		IValueControl::CallEvent(m_eventOnChange, GetValue());
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////

void CValueTextCtrl::OnTextEnter(wxCommandEvent& event)
{
	wxTextCtrl* textCtrl = wxDynamicCast(
		event.GetEventObject(), wxTextCtrl
	);
	m_textModified = false;
	TextProcessing(textCtrl, textCtrl->GetValue());
	event.Skip();
}

void CValueTextCtrl::OnTextUpdated(wxCommandEvent& event)
{
	m_textModified = true;
	event.Skip();
}

void CValueTextCtrl::OnKillFocus(wxFocusEvent& event)
{
	if (m_textModified) {
		wxTextCtrl* textCtrl = wxDynamicCast(
			event.GetEventObject(), wxTextCtrl
		);
		m_textModified = false;
		TextProcessing(textCtrl, textCtrl->GetValue());
	}
	event.Skip();
}

#include "metadata/singleClass.h"

void CValueTextCtrl::OnSelectButtonPressed(wxCommandEvent& event)
{
	CValue standartProcessing = true;
	IValueControl::CallEvent(m_eventStartChoice, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean()) {
		CValue selValue; GetControlValue(selValue); bool setType = false;
		if (selValue.GetType() == eValueTypes::TYPE_EMPTY) {
			const CLASS_ID& clsid = GetDataType();
			if (clsid != 0) {
				IMetadata* metaData = GetMetaData();
				wxASSERT(metaData);
				if (metaData->IsRegisterObject(clsid)) {
					SetControlValue(
						metaData->CreateObject(clsid)
					);
				}
			}
			setType = true;
		}
		if (!setType) {
			const CLASS_ID& clsid = selValue.GetTypeClass();
			wxWindow* textCtrl = wxDynamicCast(
				GetWxObject(), wxWindow
			);
			if (!ITypeControl::QuickChoice(this, clsid, textCtrl)) {
				IMetadata* metaData = GetMetaData();
				wxASSERT(metaData);
				IMetaTypeObjectValueSingle *so = metaData->GetTypeObject(clsid);
				if (so != NULL && so->GetMetaType() == enReference) {
					IMetaObject* metaObject = so->GetMetaObject();
					if (metaObject != NULL) {
						metaObject->ProcessChoice(this, m_propertyChoiceForm->GetValueAsInteger(), GetSelectMode());
					}
				}
			}
		}
	}
}

void CValueTextCtrl::OnOpenButtonPressed(wxCommandEvent& event)
{
	CValue standartProcessing = true;
	IValueControl::CallEvent(m_eventOpening, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean()) {
		CValue selValue; 
		if (GetControlValue(selValue) && !selValue.IsEmpty())
			selValue.ShowValue();
	}
}

void CValueTextCtrl::OnClearButtonPressed(wxCommandEvent& event)
{
	CValue standartProcessing = true;
	IValueControl::CallEvent(m_eventClearing, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean())
		SetControlValue();
}