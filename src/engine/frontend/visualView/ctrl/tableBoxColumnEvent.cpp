#include "tableBox.h"
#include "tableBoxColumnRenderer.h"

#include "backend/metaData.h"
#include "backend/objCtor.h"

bool ibValueModelTableBoxColumn::TextProcessing(wxTextCtrl* textCtrl, const wxString& strData)
{
	const ibMetaData* metaData = GetMetaData();
	wxASSERT(metaData);
	ibValue selValue; GetControlValue(selValue);
	const ibValue& newValue = metaData->CreateObject(selValue.GetClassType());
	if (newValue.GetType() == ibValueTypes::TYPE_EMPTY) {
		//wxMessageBox(_("please select field type"));
		return false;
	}
	if (strData.Length() > 0) {
		std::vector<ibValue> listValue;
		if (newValue.FindValue(strData, listValue)) {
			SetControlValue(listValue.at(0));
		}
		else {
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
#include "frontend/win/ctrls/controlTextEditor.h"

void ibValueModelTableBoxColumn::ChoiceProcessing(ibValue& vSelected)
{
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventChoiceProcessing, GetValue(), vSelected, standartProcessing);
	if (standartProcessing.GetBoolean()) {
		
		ibValueModel::ibValueModelReturnLine* currentLine = GetCurrentLine();
		
		if (currentLine != nullptr) {
			currentLine->SetValueByMetaID(
				GetModelColumn(), vSelected
			);
		}

		ibDataViewColumnObject* columnObject =
			dynamic_cast<ibDataViewColumnObject*>(GetWxObject());

		if (columnObject != nullptr) {
			ibDataViewValueRenderer* renderer = columnObject->GetRenderer();
			wxASSERT(renderer);
			ibControlTextEditor* textEditor = dynamic_cast<ibControlTextEditor*>(renderer->GetEditorCtrl());
			if (textEditor != nullptr) {
				textEditor->SetValue(vSelected.GetString());
				textEditor->SetInsertionPointEnd();
			}
			else {
				renderer->FinishSelecting();
			}
		}
		ibValueControl::CallAsEvent(m_eventOnChange, GetValue());
	}
}

///////////////////////////////////////////////////////////////////////

void ibValueModelTableBoxColumn::OnTextEnter(wxCommandEvent& event)
{
	wxTextCtrl* textCtrl = wxDynamicCast(
		event.GetEventObject(), wxTextCtrl
	);
	TextProcessing(textCtrl, textCtrl->GetValue());
	event.Skip();
}

void ibValueModelTableBoxColumn::OnKillFocus(wxFocusEvent& event)
{
	ibDataViewColumnObject* columnObject =
		dynamic_cast<ibDataViewColumnObject*>(GetWxObject());

	if (columnObject != nullptr) {
		ibDataViewValueRenderer* renderer = columnObject->GetRenderer();
		wxASSERT(renderer);
		renderer->FinishEditing();
	}

	event.Skip();
}

void ibValueModelTableBoxColumn::OnSelectButtonPressed(wxCommandEvent& event)
{
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventStartChoice, GetValue(), standartProcessing);
	if (!standartProcessing.GetBoolean())
		return;

	// THE ONE ROUTE (ibTypeControlFactory::ChooseValue) — the column already knew
	// how to ask for its type; that knowledge stays here, as the answer to
	// GetDataType, while the sequence itself is no longer a third copy of it.
	ibDataViewColumnObject* columnObject = dynamic_cast<ibDataViewColumnObject*>(GetWxObject());
	ibDataViewValueRenderer* columnRenderer = columnObject != nullptr ? columnObject->GetRenderer() : nullptr;
	const ibMetaID& formId = m_propertyChoiceForm->GetValueAsInteger();
	const ibMetaData* metaData = GetMetaData();
	const ibValueMetaObject* choiceForm = (formId != wxNOT_FOUND && metaData != nullptr)
		? metaData->FindAnyObjectByFilter(formId) : nullptr;
	ibTypeControlFactory::ChooseValue(this, choiceForm,
		columnRenderer != nullptr ? columnRenderer->GetEditorCtrl() : nullptr);
}

void ibValueModelTableBoxColumn::OnOpenButtonPressed(wxCommandEvent& event)
{
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventOpening, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean()) {
		ibValue selValue;
		if (GetControlValue(selValue) && !selValue.IsEmpty())
			selValue.ShowValue();
	}
}

void ibValueModelTableBoxColumn::OnClearButtonPressed(wxCommandEvent& event)
{
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventClearing, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean())
		SetControlValue();
}