#include "widgets.h"
#include "frontend/win/ctrls/controltextEditor.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaData.h"

bool ibValueTextCtrl::TextProcessing(wxTextCtrl* textCtrl, const wxString& strData)
{
	const ibMetaData* metaData = GetMetaData();
	wxASSERT(metaData);
	ibValue selValue; GetControlValue(selValue);
	const ibValue& newValue = metaData->CreateObject(selValue.GetClassType());
	if (newValue.GetType() == ibValueTypes::TYPE_EMPTY) {
		textCtrl->SetValue(selValue.GetString());
		textCtrl->SetInsertionPointEnd();
		return false;
	}
	if (strData.Length() > 0) {
		std::vector<ibValue> listValue;
		if (newValue.FindValue(strData, listValue)) {
			SetControlValue(listValue.at(0));
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

	ibValueControl::CallAsEvent(m_eventOnChange, GetValue());
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////

void ibValueTextCtrl::ChoiceProcessing(ibValue& vSelected)
{
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventChoiceProcessing, GetValue(), vSelected, standartProcessing);
	if (standartProcessing.GetBoolean()) {
		SetControlValue(vSelected);
		ibValueControl::CallAsEvent(m_eventOnChange, GetValue());
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////

void ibValueTextCtrl::OnTextEnter(wxCommandEvent& event)
{
	wxTextCtrl* textCtrl = wxDynamicCast(
		event.GetEventObject(), wxTextCtrl
	);

	m_textModified = false;
	TextProcessing(textCtrl, textCtrl->GetValue());
	event.Skip();
}

#include "frontend/visualView/ctrl/form.h"

void ibValueTextCtrl::OnTextUpdated(wxCommandEvent& event)
{
	if (m_formOwner != nullptr) {
		ibSourceDataObject* sourceObject = m_formOwner->GetSourceObject();
		if (sourceObject != nullptr && sourceObject->ModifiesData()) {
			sourceObject->Modify(true);
		}
	}

	m_textModified = true;
	event.Skip();
}

void ibValueTextCtrl::OnKillFocus(wxFocusEvent& event)
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

void ibValueTextCtrl::OnSelectButtonPressed(wxCommandEvent& event)
{
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventStartChoice, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean()) {
		ibValue selValue; GetControlValue(selValue); bool setType = false;
		if (selValue.GetType() == ibValueTypes::TYPE_EMPTY) {
			const ibClassID& clsid = GetDataType();
			if (clsid != 0) {
				ibMetaData* metaData = GetMetaData();
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
			const ibClassID& clsid = selValue.GetClassType();
			wxWindow* textCtrl = wxDynamicCast(GetWxObject(), wxWindow);
			if (!ibTypeControlFactory::QuickChoice(this, clsid, textCtrl)) {
				const ibMetaData* metaData = GetMetaData();
				wxASSERT(metaData);
				const ibCtorMetaValueType* so = metaData->GetTypeCtor(clsid);
				if (so != nullptr && so->GetMetaTypeCtor() == ibCtorObjectMetaType_Reference) {
					ibValueMetaObject* metaObject = so->GetMetaObject();
					if (metaObject != nullptr) {
						const ibMetaID& id = m_propertyChoiceForm->GetValueAsInteger();
						if (id != wxNOT_FOUND) {
							const ibMetaData* metaData = GetMetaData();
							const ibValueMetaObject* foundedObject = metaData != nullptr
								? metaData->FindAnyObjectByFilter(id) : nullptr;
							metaObject->ProcessChoice(this, foundedObject != nullptr ? foundedObject->GetName() : wxString(), GetSelectMode());
						}
						else {
							metaObject->ProcessChoice(this, wxEmptyString, GetSelectMode());
						}
					}
				}
			}
		}
	}
}

void ibValueTextCtrl::OnOpenButtonPressed(wxCommandEvent& event)
{
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventOpening, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean()) {
		ibValue selValue;
		if (GetControlValue(selValue) && !selValue.IsEmpty())
			selValue.ShowValue();
	}
}

void ibValueTextCtrl::OnClearButtonPressed(wxCommandEvent& event)
{
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventClearing, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean())
		SetControlValue();
}