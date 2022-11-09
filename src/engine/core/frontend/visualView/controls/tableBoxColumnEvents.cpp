#include "tableBox.h"
#include "metadata/metadata.h"
#include "metadata/singleMetaTypes.h"

bool CValueTableBoxColumn::TextProcessing(CValue& newValue, const wxString& strData)
{
	CValue selValue = GetControlValue();

	if (selValue.GetType() == eValueTypes::TYPE_EMPTY) {
		//wxMessageBox(_("please select field type"));
		return false;
	}

	if (strData.Length() > 0) {
		std::vector<CValue> foundedObjects;
		if (selValue.FindValue(strData, foundedObjects)) {
			newValue = foundedObjects.at(0);
		}
	}
	else {
		newValue = CValue();
	}

	SetControlValue(newValue);

	CValue selfControl = this;
	IValueControl::CallEvent(m_eventOnChange, selfControl);
	return true;
}

void CValueTableBoxColumn::ChoiceProcessing(CValue& vSelected)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent(m_eventChoiceProcessing, selfControl, vSelected, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}

	CValueTableBox* tableBox = wxDynamicCast(
		GetParent(),
		CValueTableBox
	);

	wxASSERT(tableBox);

	if (tableBox->m_tableCurrentLine) {
		if (m_dataSource.isValid()) {
			tableBox->m_tableCurrentLine->SetValueByMetaID(
				GetIdByGuid(m_dataSource), vSelected
			);
		}
	}

	CDataViewColumnObject* columnObject =
		dynamic_cast<CDataViewColumnObject*>(GetWxObject());

	if (columnObject) {
		CValueViewRenderer* renderer = columnObject->GetRenderer();
		wxASSERT(renderer);
		wxVariant valVariant = vSelected.GetString();
		renderer->FinishSelecting(valVariant);
	}

	IValueControl::CallEvent(m_eventOnChange, selfControl);
}

///////////////////////////////////////////////////////////////////////

void CValueTableBoxColumn::OnTextEnter(wxCommandEvent& event)
{
	wxTextCtrl* textCtrl = wxDynamicCast(
		event.GetEventObject(), wxTextCtrl
	);

	CValue selValue;
	if (TextProcessing(selValue, textCtrl->GetValue())) {
		textCtrl->SetValue(selValue.GetString());
	}
	else {
		textCtrl->SetValue(wxEmptyString);
	}

	event.Skip();
}

void CValueTableBoxColumn::OnKillFocus(wxFocusEvent& event)
{
	wxTextCtrl* textCtrl = wxDynamicCast(
		event.GetEventObject(), wxTextCtrl
	);

	CValue selValue;
	if (TextProcessing(selValue, textCtrl->GetValue())) {
		textCtrl->SetValue(selValue.GetString());
	}
	else {
		textCtrl->SetValue(wxEmptyString);
	}

	event.Skip();
}

void CValueTableBoxColumn::OnSelectButtonPressed(wxCommandEvent& event)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent(m_eventStartChoice, selfControl, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}
	CValue selValue = GetControlValue();
	if (selValue.GetType() == eValueTypes::TYPE_EMPTY) {

		CLASS_ID clsid = GetDataType();
		if (clsid == 0)
			return;

		IMetadata* metaData = GetMetaData();
		wxASSERT(metaData);
		if (metaData->IsRegisterObject(clsid)) {
			CValue newValue = metaData->CreateObject(
				metaData->GetNameObjectFromID(clsid)
			);
			SetControlValue(newValue);
		}
		return;
	}
	CDataViewColumnObject* columnObject =
		dynamic_cast<CDataViewColumnObject*>(GetWxObject());
	wxASSERT(columnObject);
	CValueViewRenderer* columnRenderer = columnObject->GetRenderer();
	wxASSERT(columnRenderer);
	const CLASS_ID& clsid = selValue.GetClassType();
	if (!IAttributeControl::SelectSimpleValue(clsid, columnRenderer->GetEditorCtrl())) {
		eSelectMode selMode = eSelectMode::eSelectMode_Items;
		IMetaAttributeObject* srcValue =
			dynamic_cast<IMetaAttributeObject*>(IAttributeControl::GetMetaSource());
		if (srcValue != NULL)
			selMode = srcValue->GetSelectMode();
		IMetaObject* metaObject =
			IAttributeControl::GetMetaObjectById(clsid);
		if (metaObject != NULL) {
			metaObject->ProcessChoice(this, m_propertyChoiceForm->GetValueAsInteger(), selMode);
		}
	}
}

void CValueTableBoxColumn::OnListButtonPressed(wxCommandEvent& event)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent(m_eventStartListChoice, selfControl, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}

}

#include "frontend/controls/textEditor.h"

void CValueTableBoxColumn::OnClearButtonPressed(wxCommandEvent& event)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent(m_eventClearing, selfControl, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}

	CValueTableBox* tableBox = wxDynamicCast(
		GetParent(),
		CValueTableBox
	);

	wxASSERT(tableBox);

	if (tableBox->m_tableCurrentLine) {
		if (m_dataSource.isValid()) {
			tableBox->m_tableCurrentLine->SetValueByMetaID(GetIdByGuid(m_dataSource), CValue());
		}
	}

	CDataViewColumnObject* columnObject =
		dynamic_cast<CDataViewColumnObject*>(GetWxObject());
	if (columnObject) {
		CValueViewRenderer* renderer = columnObject->GetRenderer();
		wxASSERT(renderer);
		CTextCtrl* textEditor = dynamic_cast<CTextCtrl*>(renderer->GetEditorCtrl());
		if (textEditor) {
			textEditor->SetTextValue(wxEmptyString);
		}
		else {
			wxVariant valVariant = wxEmptyString;
			renderer->FinishSelecting(valVariant);
		}
	}
}