#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode — an event's arguments
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaData.h"
#include "sfrontend/visualView/ctrl/form.h"

// The typed text becomes a value of the field's type, or is refused. A refused text leaves the value
// as it was, and the next Update shows that value again in place of what was typed. A read-only field
// (IsReadOnly) takes no text at all.
bool ibValueTextCtrl::TextProcessing(const wxString& strData)
{
	if (IsReadOnly())
		return false;

	const ibMetaData* metaData = GetMetaData();
	wxASSERT(metaData);
	if (metaData == nullptr)
		return false;
	ibValue selValue; GetControlValue(selValue);
	const ibValue& newValue = metaData->CreateObject(selValue.GetClassType());
	if (newValue.GetType() == ibValueTypes::TYPE_EMPTY)
		return false;
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

bool ibValueTextCtrl::OnClientEvent(ibClientEvent event, const ibDataNode& args)
{
	switch (event) {
	case ibClientEvent::Input:  OnTextUpdated(); return true;
	case ibClientEvent::Change: OnTextEnter(args.GetValue<wxString>(wxT("Text"))); return true;
	case ibClientEvent::Select: OnSelectButtonPressed(); return true;
	case ibClientEvent::Open:   OnOpenButtonPressed(); return true;
	case ibClientEvent::Clear:  OnClearButtonPressed(); return true;
	default:                    return ibValueWindow::OnClientEvent(event, args);
	}
}

// The text as it stands is committed.
void ibValueTextCtrl::OnTextEnter(const wxString& text)
{
	TextProcessing(text);
}

// Typing (or the text cleared by hand) — nothing is committed yet, but the form's object is modified
// from the first keystroke.
void ibValueTextCtrl::OnTextUpdated()
{
	if (IsReadOnly())
		return;

	if (m_formOwner != nullptr) {
		ibSourceDataObject* sourceObject = m_formOwner->GetSourceObject();
		if (sourceObject != nullptr && sourceObject->ModifiesData()) {
			sourceObject->Modify(true);
		}
	}
}

#include "backend/objCtor.h"

void ibValueTextCtrl::OnSelectButtonPressed()
{
	if (IsReadOnly())
		return;

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
	ibTypeControlFactory::ChooseValue(this, choiceForm);
}

void ibValueTextCtrl::OnOpenButtonPressed()
{
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventOpening, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean()) {
		ibValue selValue;
		if (GetControlValue(selValue) && !selValue.IsEmpty())
			selValue.ShowValue();
	}
}

void ibValueTextCtrl::OnClearButtonPressed()
{
	if (IsReadOnly())
		return;

	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventClearing, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean())
		SetControlValue();
}
