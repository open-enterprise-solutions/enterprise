#include "tableBox.h"
#include "form.h"

#include "backend/metaData.h"
#include "backend/objCtor.h"

// The typed text becomes a value of the cell's type, or is refused. A refused text leaves the cell as it
// was, and the next fetch of the row shows that value again in place of what was typed. A read-only
// column (IsReadOnly) takes no text at all.
bool ibValueModelTableBoxColumn::TextProcessing(const wxString& strData)
{
	if (IsReadOnly())
		return false;

	const ibMetaData* metaData = GetMetaData();
	wxASSERT(metaData);
	if (metaData == nullptr)
		return false;
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

void ibValueModelTableBoxColumn::ChoiceProcessing(ibValue& vSelected)
{
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventChoiceProcessing, GetValue(), vSelected, standartProcessing);
	// ⭐ THE SAME DOOR A TYPED VALUE GOES THROUGH, as on a form's field (ibValueTextCtrl::ChoiceProcessing).
	// This wrote the row and the editor by itself — the same lines as SetControlValue, less the refresh —
	// so a chosen value and a typed one reached the cell by two roads.
	if (standartProcessing.GetBoolean()) {
		SetControlValue(vSelected);
		ibValueControl::CallAsEvent(m_eventOnChange, GetValue());
	}
}

///////////////////////////////////////////////////////////////////////

// Enter — the text as it stands is committed into the current row's cell.
void ibValueModelTableBoxColumn::OnTextEnter(const wxString& text)
{
	TextProcessing(text);
}

// THE EDIT IS OVER — what the cell editor's FinishEditing did once the editor was gone: the form refreshes,
// and then the rows are read again.
//
// ⭐⭐ A ROW EDITED OUT OF THE FILTER MUST LEAVE THE LIST, and THIS is the moment to say so. The write itself
// only changes the row, so a cell changed to something the filter no longer passes would stay on screen until
// something else happened to read again. The simplest true answer is to read again (Max, 2026-08-29).
//
// 🛑⭐⭐ AND THERE IS NO CONDITION ON IT. There was one — "only when there is a filter" — and it was wrong twice
// for the same reason: a FILTER decides whether a row belongs, a GROUPING decides where it belongs, and a SORT
// decides that too. The rule is the one an edit actually justifies — the edit is over, READ AGAIN — and what
// that means is the composer's to decide (Max: *"I suggest removing this check altogether"*).
void ibValueModelTableBoxColumn::OnEditingDone()
{
	if (m_formOwner != nullptr)
		m_formOwner->RefreshForm();

	if (ibValueModelTableBox* owner = GetOwner()) {
		if (ibValueModel* model = owner->GetTableModel())
			model->RefetchAll();
	}
}

void ibValueModelTableBoxColumn::OnSelectButtonPressed()
{
	if (IsReadOnly())
		return;

	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventStartChoice, GetValue(), standartProcessing);
	if (!standartProcessing.GetBoolean())
		return;

	// THE ONE ROUTE (ibTypeControlFactory::ChooseValue) — the column already knew
	// how to ask for its type; that knowledge stays here, as the answer to
	// GetDataType, while the sequence itself is no longer a third copy of it.
	const ibMetaID& formId = m_propertyChoiceForm->GetValueAsInteger();
	const ibMetaData* metaData = GetMetaData();
	const ibValueMetaObject* choiceForm = (formId != wxNOT_FOUND && metaData != nullptr)
		? metaData->FindAnyObjectByFilter(formId) : nullptr;
	ibTypeControlFactory::ChooseValue(this, choiceForm);
}

void ibValueModelTableBoxColumn::OnOpenButtonPressed()
{
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventOpening, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean()) {
		ibValue selValue;
		if (GetControlValue(selValue) && !selValue.IsEmpty())
			selValue.ShowValue();
	}
}

void ibValueModelTableBoxColumn::OnClearButtonPressed()
{
	if (IsReadOnly())
		return;

	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventClearing, GetValue(), standartProcessing);
	if (standartProcessing.GetBoolean())
		SetControlValue();
}