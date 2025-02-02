#include "tableBox.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"

bool CValueTableBoxColumn::TextProcessing(wxTextCtrl* textCtrl, const wxString& strData)
{
	IMetaData* metaData = GetMetaData();
	wxASSERT(metaData);
	CValue selValue; GetControlValue(selValue);
	const CValue& newValue = metaData->CreateObject(selValue.GetClassType());
	if (newValue.GetType() == eValueTypes::TYPE_EMPTY) {
		//wxMessageBox(_("please select field type"));
		return false;
	}
	if (strData.Length() > 0) {
		std::vector<CValue> foundedObjects;
		if (newValue.FindValue(strData, foundedObjects)) {
			SetControlValue(foundedObjects.at(0));
		}
		else {
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

#include "frontend/win/ctrls/textEditor.h"

void CValueTableBoxColumn::ChoiceProcessing(CValue& vSelected)
{
	CValue standartProcessing = true;
	IValueControl::CallAsEvent(m_eventChoiceProcessing, GetValue(), vSelected, standartProcessing);
	if (standartProcessing.GetBoolean()) {
		IValueTable::IValueModelReturnLine* retLine = GetReturnLine();
		if (retLine != nullptr) {
			retLine->SetValueByMetaID(
				m_dataSource.isValid() ? GetIdByGuid(m_dataSource) : m_controlId, vSelected
			);
		}
		wxDataViewColumnObject* columnObject =
			dynamic_cast<wxDataViewColumnObject*>(GetWxObject());
		if (columnObject != nullptr) {
			CValueViewRenderer* renderer = columnObject->GetRenderer();
			wxASSERT(renderer);
			wxTextContainerCtrl* textEditor = dynamic_cast<wxTextContainerCtrl*>(renderer->GetEditorCtrl());
			if (textEditor != nullptr) {
				textEditor->SetTextValue(vSelected.GetString());
				textEditor->SetInsertionPointEnd();
				//textEditor->SetFocus();
			}
			else {
				renderer->FinishSelecting();
			}
		}
		IValueControl::CallAsEvent(m_eventOnChange, GetValue());
	}
}

///////////////////////////////////////////////////////////////////////

void CValueTableBoxColumn::OnTextEnter(wxCommandEvent& event)
{
	wxTextCtrl* textCtrl = wxDynamicCast(
		event.GetEventObject(), wxTextCtrl
	);
	TextProcessing(textCtrl, textCtrl->GetValue());
	event.Skip();
}

void CValueTableBoxColumn::OnKillFocus(wxFocusEvent& event)
{
	event.Skip();
}

void CValueTableBoxColumn::OnSelectButtonPressed(wxCommandEvent& event)
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
			wxDataViewColumnObject* columnObject =
				dynamic_cast<wxDataViewColumnObject*>(GetWxObject());
			wxASSERT(columnObject);
			CValueViewRenderer* columnRenderer = columnObject->GetRenderer();
			wxASSERT(columnRenderer);
			const class_identifier_t& clsid = selValue.GetClassType();
			if (!ITypeControlAttribute::QuickChoice(this, clsid, columnRenderer->GetEditorCtrl())) {
				IMetaData* metaData = GetMetaData();
				wxASSERT(metaData);
				IMetaValueTypeCtor* so = metaData->GetTypeCtor(clsid);
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

void CValueTableBoxColumn::OnOpenButtonPressed(wxCommandEvent& event)
{
	CValue standartProcessing = true;
	IValueControl::CallAsEvent(m_eventOpening, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean()) {
		CValue selValue; 
		if (GetControlValue(selValue) && !selValue.IsEmpty())
			selValue.ShowValue();
	}
}

void CValueTableBoxColumn::OnClearButtonPressed(wxCommandEvent& event)
{
	CValue standartProcessing = true;
	IValueControl::CallAsEvent(m_eventClearing, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean())
		SetControlValue();
}