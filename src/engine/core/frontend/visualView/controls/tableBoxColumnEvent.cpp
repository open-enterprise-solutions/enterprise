#include "tableBox.h"
#include "core/metadata/metadata.h"
#include "core/metadata/singleClass.h"

bool CValueTableBoxColumn::TextProcessing(wxTextCtrl* textCtrl, const wxString& strData)
{
	IMetadata* metaData = GetMetaData();
	wxASSERT(metaData);
	CValue selValue; GetControlValue(selValue);
	const CValue& newValue = metaData->CreateObject(selValue.GetTypeClass());
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

	IValueControl::CallEvent(m_eventOnChange, GetValue());
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////

#include "frontend/controls/textEditor.h"

void CValueTableBoxColumn::ChoiceProcessing(CValue& vSelected)
{
	CValue standartProcessing = true;
	IValueControl::CallEvent(m_eventChoiceProcessing, GetValue(), vSelected, standartProcessing);
	if (standartProcessing.GetBoolean()) {
		IValueTable::IValueModelReturnLine* retLine = GetReturnLine();
		if (retLine != NULL) {
			retLine->SetValueByMetaID(
				m_dataSource.isValid() ? GetIdByGuid(m_dataSource) : m_controlId, vSelected
			);
		}
		wxDataViewColumnObject* columnObject =
			dynamic_cast<wxDataViewColumnObject*>(GetWxObject());
		if (columnObject != NULL) {
			CValueViewRenderer* renderer = columnObject->GetRenderer();
			wxASSERT(renderer);
			wxTextContainerCtrl* textEditor = dynamic_cast<wxTextContainerCtrl*>(renderer->GetEditorCtrl());
			if (textEditor != NULL) {
				textEditor->SetTextValue(vSelected.GetString());
				textEditor->SetInsertionPointEnd();
				//textEditor->SetFocus();
			}
			else {
				renderer->FinishSelecting();
			}
		}
		IValueControl::CallEvent(m_eventOnChange, GetValue());
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
			wxDataViewColumnObject* columnObject =
				dynamic_cast<wxDataViewColumnObject*>(GetWxObject());
			wxASSERT(columnObject);
			CValueViewRenderer* columnRenderer = columnObject->GetRenderer();
			wxASSERT(columnRenderer);
			const CLASS_ID& clsid = selValue.GetTypeClass();
			if (!ITypeControl::QuickChoice(this, clsid, columnRenderer->GetEditorCtrl())) {
				IMetadata* metaData = GetMetaData();
				wxASSERT(metaData);
				IMetaTypeObjectValueSingle* so = metaData->GetTypeObject(clsid);
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

void CValueTableBoxColumn::OnOpenButtonPressed(wxCommandEvent& event)
{
	CValue standartProcessing = true;
	IValueControl::CallEvent(m_eventOpening, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean()) {
		CValue selValue; 
		if (GetControlValue(selValue) && !selValue.IsEmpty())
			selValue.ShowValue();
	}
}

void CValueTableBoxColumn::OnClearButtonPressed(wxCommandEvent& event)
{
	CValue standartProcessing = true;
	IValueControl::CallEvent(m_eventClearing, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean())
		SetControlValue();
}