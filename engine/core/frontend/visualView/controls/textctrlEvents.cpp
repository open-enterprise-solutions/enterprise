#include "widgets.h"
#include "frontend/controls/textEditor.h"
#include "metadata/metaObjects/objects/baseObject.h"
#include "metadata/metadata.h"

bool CValueTextCtrl::TextProcessing(const wxString& strData)
{
	CValue selValue = GetControlValue();

	IMetadata* metaData = GetMetaData();
	wxASSERT(metaData);
	wxString className =
		metaData->GetNameObjectFromID(selValue.GetClassType());
	CValue newValue = metaData->CreateObject(className);

	if (newValue.GetType() == eValueTypes::TYPE_EMPTY) {
		//wxMessageBox(_("please select field type"));
		return false;
	}

	if (strData.Length() > 0) {
		std::vector<CValue> foundedObjects;
		if (newValue.FindValue(strData, foundedObjects)) {
			SetControlValue(foundedObjects.at(0));
		}
	}
	else {
		SetControlValue(newValue);
	}

	return true;
}

void CValueTextCtrl::ChoiceProcessing(CValue& vSelected)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent("choiceProcessing", selfControl, vSelected, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}

	SetControlValue(vSelected);

	IValueControl::CallEvent("onChange", selfControl);
}

///////////////////////////////////////////////////////////////////////////////////////////////

void CValueTextCtrl::OnTextEnter(wxCommandEvent& event)
{
	wxTextCtrl* textCtrl = wxDynamicCast(
		event.GetEventObject(), wxTextCtrl
	);

	if (TextProcessing(textCtrl->GetValue())) {
		textCtrl->SetValue(m_selValue.GetString());
	}
	else {
		textCtrl->SetValue(wxEmptyString);
	}

	event.Skip();
}

void CValueTextCtrl::OnKillFocus(wxFocusEvent& event)
{
	wxTextCtrl* textCtrl = wxDynamicCast(
		event.GetEventObject(), wxTextCtrl
	);

	if (TextProcessing(textCtrl->GetValue())) {
		textCtrl->SetValue(m_selValue.GetString());
	}
	else {
		textCtrl->SetValue(wxEmptyString);
	}

	event.Skip();
}

void CValueTextCtrl::OnSelectButtonPressed(wxCommandEvent& event)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent("startChoice", selfControl, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}

	CValue selValue = GetControlValue();

	if (selValue.GetType() == eValueTypes::TYPE_EMPTY) {

		CLASS_ID clsid = GetDataType();
		if (clsid == 0)
			return;

		IMetadata* metaData = GetMetaData();
		CValue newValue = metaData->CreateObject(
			metaData->GetNameObjectFromID(clsid)
		);

		SetControlValue(newValue);
		return;
	}

	IMetaObject* metaObject =
		IAttributeControl::GetMetaObjectById(selValue.GetClassType());

	if (metaObject != NULL) {
		metaObject->ProcessChoice(this, m_choiceForm);
	}
}

void CValueTextCtrl::OnListButtonPressed(wxCommandEvent& event)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent("startListChoice", selfControl, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}

}

void CValueTextCtrl::OnClearButtonPressed(wxCommandEvent& event)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent("clearing", selfControl, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}

	SetControlValue(CValue());
}