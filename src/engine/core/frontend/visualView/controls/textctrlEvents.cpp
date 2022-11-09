#include "widgets.h"
#include "frontend/controls/textEditor.h"
#include "metadata/metaObjects/objects/object.h"
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

	CValue selfControl = this;
	IValueControl::CallEvent(m_eventOnChange, selfControl);
	return true;
}

void CValueTextCtrl::ChoiceProcessing(CValue& vSelected)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent(m_eventChoiceProcessing, selfControl, vSelected, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}

	SetControlValue(vSelected);

	IValueControl::CallEvent(m_eventOnChange, selfControl);
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
	IValueControl::CallEvent(m_eventStartChoice, selfControl, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}

	CValue selValue = GetControlValue();

	wxWindow* textCtrl = wxDynamicCast(
		GetWxObject(), wxWindow
	);

	if (selValue.GetType() == eValueTypes::TYPE_EMPTY) {

		const CLASS_ID& clsid = GetDataType();
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
	const CLASS_ID &clsid = selValue.GetClassType();
	if (!IAttributeControl::SelectSimpleValue(clsid, textCtrl)) {
		eSelectMode selMode = eSelectMode::eSelectMode_Items; 
		IMetaAttributeObject *srcValue = 
			dynamic_cast<IMetaAttributeObject *>(IAttributeControl::GetMetaSource());
		if (srcValue != NULL)
			selMode = srcValue->GetSelectMode();
		IMetaObject* metaObject = 
			IAttributeControl::GetMetaObjectById(clsid);
		if (metaObject != NULL) {
			metaObject->ProcessChoice(this, m_propertyChoiceForm->GetValueAsInteger(), selMode);
		}
	}
}

void CValueTextCtrl::OnListButtonPressed(wxCommandEvent& event)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent(m_eventStartListChoice, selfControl, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}
}

void CValueTextCtrl::OnClearButtonPressed(wxCommandEvent& event)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent(m_eventClearing, selfControl, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}
	SetControlValue();
}