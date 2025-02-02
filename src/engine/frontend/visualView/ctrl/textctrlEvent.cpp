#include "widgets.h"
#include "frontend/win/ctrls/textEditor.h"
#include "backend/metaCollection/partial/object.h"
#include "backend/metaData.h"

bool CValueTextCtrl::TextProcessing(wxTextCtrl* textCtrl, const wxString& strData)
{
	IMetaData* metaData = GetMetaData();
	wxASSERT(metaData);
	CValue selValue; GetControlValue(selValue);
	const CValue& newValue = metaData->CreateObject(selValue.GetClassType());
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

	IValueControl::CallAsEvent(m_eventOnChange, GetValue());
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////

void CValueTextCtrl::ChoiceProcessing(CValue& vSelected)
{
	CValue standartProcessing = true;
	IValueControl::CallAsEvent(m_eventChoiceProcessing, GetValue(), vSelected, standartProcessing);
	if (standartProcessing.GetBoolean()) {
		SetControlValue(vSelected);
		IValueControl::CallAsEvent(m_eventOnChange, GetValue());
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

#include "backend/objCtor.h"

void CValueTextCtrl::OnSelectButtonPressed(wxCommandEvent& event)
{
	CValue standartProcessing = true;
	IValueControl::CallAsEvent(m_eventStartChoice, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean()) {
		CValue selValue; GetControlValue(selValue); bool setType = false;
		if (selValue.GetType() == eValueTypes::TYPE_EMPTY) {
			const class_identifier_t& clsid = GetDataType();
			if (clsid != 0) {
				IMetaData* metaData = GetMetaData();
				wxASSERT(metaData);
				if (metaData->IsRegisterCtor(clsid)) {
					SetControlValue(
						metaData->CreateObject(clsid)
					);
				}
			}
			setType = true;
		}
		if (!setType) {
			const class_identifier_t& clsid = selValue.GetClassType();
			wxWindow* textCtrl = wxDynamicCast(
				GetWxObject(), wxWindow
			);
			if (!ITypeControlAttribute::QuickChoice(this, clsid, textCtrl)) {
				IMetaData* metaData = GetMetaData();
				wxASSERT(metaData);
				IMetaValueTypeCtor *so = metaData->GetTypeCtor(clsid);
				if (so != nullptr && so->GetMetaTypeCtor() == eCtorMetaType_Reference) {
					IMetaObject* metaObject = so->GetMetaObject();
					if (metaObject != nullptr) {
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
	IValueControl::CallAsEvent(m_eventOpening, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean()) {
		CValue selValue; 
		if (GetControlValue(selValue) && !selValue.IsEmpty())
			selValue.ShowValue();
	}
}

void CValueTextCtrl::OnClearButtonPressed(wxCommandEvent& event)
{
	CValue standartProcessing = true;
	IValueControl::CallAsEvent(m_eventClearing, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean())
		SetControlValue();
}