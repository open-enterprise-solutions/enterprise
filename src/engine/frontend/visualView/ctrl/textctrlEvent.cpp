#include "widgets.h"
#ifndef OES_USE_WEB
#include "frontend/win/ctrls/controlTextEditor.h"
#endif
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaData.h"
#include "frontend/visualView/ctrl/form.h"

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

#ifdef OES_USE_WEB
void ibValueTextCtrl::OnWebTextChanged(wxCommandEvent& event)
{
	// Mirrors desktop TextProcessing: coerce the incoming string
	// through the backing ibValue's type (so a numeric / date source
	// doesn't get silently overwritten with a raw string), commit via
	// SetControlValue (which fires RefreshForm + sourceObject update),
	// then fire the OnChange script the same way OnTextEnter does.
	const wxString newText = event.GetString();

	const ibMetaData* metaData = GetMetaData();
	if (metaData != nullptr) {
		ibValue selValue; GetControlValue(selValue);
		const ibValue& typed = metaData->CreateObject(selValue.GetClassType());
		if (typed.GetType() != ibValueTypes::TYPE_EMPTY) {
			if (newText.Length() > 0) {
				std::vector<ibValue> listValue;
				if (typed.FindValue(newText, listValue)) {
					SetControlValue(listValue.at(0));
				}
				// else: parse failure — keep the prior committed value;
				// the next JSON walk re-emits it, the client overwrites
				// its local buffer.
			}
			else {
				SetControlValue(typed);  // empty string → empty-typed value
			}
			ibValueControl::CallAsEvent(m_eventOnChange, GetValue());
		}
	}
	event.Skip();
}
#endif

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
	// The script may take the choice over entirely (StartChoice + standard
	// processing off) — that decision belongs to the control, not to the route.
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventStartChoice, GetValue(), standartProcessing);
	if (!standartProcessing.GetBoolean())
		return;

	// THE ONE ROUTE (ibTypeControlFactory::ChooseValue): settle the type — from the
	// metadata, asking only when the control admits more than one — then choose the
	// value of that type. This sequence used to be written out here, and it is the
	// original this control lends to every other value editor; it now lives in one
	// place so a filter cell and a table column walk exactly it, not a copy that
	// drifts.
	// The form the author picked in the property grid (null = the metaobject's own).
	const ibMetaID& formId = m_propertyChoiceForm->GetValueAsInteger();
	const ibMetaData* metaData = GetMetaData();
	const ibValueMetaObject* choiceForm = (formId != wxNOT_FOUND && metaData != nullptr)
		? metaData->FindAnyObjectByFilter(formId) : nullptr;
	ibTypeControlFactory::ChooseValue(this, choiceForm, wxDynamicCast(GetWxObject(), wxWindow));
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