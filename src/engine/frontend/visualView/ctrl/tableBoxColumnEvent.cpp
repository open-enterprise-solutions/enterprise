#include "tableBox.h"
#include "tableBoxColumnRenderer.h"

#include "backend/metaData.h"
#include "backend/objCtor.h"

bool ibValueModelTableBoxColumn::TextProcessing(wxTextCtrl* textCtrl, const wxString& strData)
{
	ibMetaData* metaData = GetMetaData();
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
		
		ibValueModelTableBase::ibValueModelReturnLine* currentLine = GetCurrentLine();
		
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
	if (standartProcessing.GetBoolean()) {
		ibValue selValue; GetControlValue(selValue); bool setType = false;
		if (selValue.GetType() == ibValueTypes::TYPE_EMPTY) {
			const ibClassID& clsid = GetDataType();
			if (clsid != 0) {
				const ibMetaData* metaData = GetMetaData();
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
			ibDataViewColumnObject* columnObject =
				dynamic_cast<ibDataViewColumnObject*>(GetWxObject());
			wxASSERT(columnObject);
			ibDataViewValueRenderer* columnRenderer = columnObject->GetRenderer();
			wxASSERT(columnRenderer);
			const ibClassID& clsid = selValue.GetClassType();
			if (!ibTypeControlFactory::QuickChoice(this, clsid, columnRenderer->GetEditorCtrl())) {
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