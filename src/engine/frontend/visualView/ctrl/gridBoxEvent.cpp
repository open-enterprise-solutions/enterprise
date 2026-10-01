#include "gridBox.h"
#include "backend/system/value/valueDataComposition.h"   // …is there a schema behind this sheet at all

//***********************************************************************
//*                       Cell events — the click as a QUESTION         *
//***********************************************************************
//
// (⭐ THE DETAIL ITSELF LIVES IN gridBoxDetail.cpp — a click is a gesture, a menu and a routed
//  choice; narrowing a composition to one cell and opening it as a report is another thing
//  entirely, and it had grown into this file until the file stopped being about events.)

// ⭐⭐ A CLICK ON A CELL IS A QUESTION ABOUT THE FIGURE — the report term is "detail processing".
// The sheet is not a picture: every figure was composed from something, and the cell already knows
// what (the composer stamps its value as the cell's details parameter). What was missing is the
// FORK — a report saying what a click means in it — so these verbs are asked one step before the
// answer that already existed.
//
// ⚠ THE HANDLER RUNS FIRST, AND ITS `StandardProcessing` DECIDES THE REST. Cleared, the default is
// dropped: no value shown on the left, no popup on the right. Skipped-through, everything below
// happens as it always did. Same shape the tablebox's Selection event has, deliberately — a reader
// who has learned one knows the other.
void ibValueGridBox::OnCellLeftClick(ibGridEvent& event)
{
	ibValue standardProcessing = true;
	CallAsEvent(m_eventOnDetailProcessing,
		GetValue(),                          // control
		ibValue(event.GetRow()),             // row
		ibValue(event.GetCol()),             // column
		standardProcessing
	);
	if (!standardProcessing.GetBoolean())
		return;   // the runtime said no — nothing opens, no menu

	// ⭐⭐ A CLICKED FIGURE HAS MORE THAN ONE ANSWER, SO IT ASKS. Opening the value is one of them;
	// "how was this built" is another, and there is no way to guess which the person meant. So the
	// click offers them — first the value, then the detail (Max, 2026-08-26, over the reference
	// report: "the right button is taken by copying, we cannot reuse it; on the LEFT click drop a
	// menu — open the value first, detail second").
	//
	// ⚠ ONLY WHERE THERE IS SOMETHING TO ANSWER. A cell with no details parameter was composed from
	// nothing a reader can follow — a caption, a blank — and a menu over it would be commands that
	// do nothing. The click then means what it always meant, and the editor handles it.
	wxObjectDataPtr<ibBackendSpreadsheetObject> document;
	if (m_spreadsheetModel)
		document = m_spreadsheetModel->GetSpreadsheetDocument();
	wxWindow* grid = dynamic_cast<wxWindow*>(GetInnerWx());
	if (document == nullptr || grid == nullptr) {
		event.Skip();
		return;
	}

	wxString detailsParameter;
	document->GetCellDetailsParameter(event.GetRow(), event.GetCol(), detailsParameter);
	if (detailsParameter.IsEmpty()) {
		event.Skip();
		return;
	}

	// DETAIL is offered only where there is a composition to copy: a hand-filled sheet has no schema
	// and nothing to re-compose, so the entry would be a command that cannot mean anything.
	const bool composed = dynamic_cast<ibValueDataComposition*>(
		static_cast<ibValueSpreadsheetModel*>(m_spreadsheetModel)) != nullptr;

	enum { idOpenValue = wxID_HIGHEST + 1, idShowDetail, idDetailByFirst };
	wxMenu menu;
	menu.Append(idOpenValue, _("Open value"));
	menu.Append(idShowDetail, _("Detail..."))->Enable(composed);
	// …and one entry per field this cell can be broken down by — the submenu answers for itself,
	// including whether there is anything to offer (gridBoxDetail.cpp).
	const std::vector<wxString> byPath =
		AppendDetailByMenu(menu, idDetailByFirst, event.GetRow(), event.GetCol());

	const int chosen = grid->GetPopupMenuSelectionFromUser(menu);
	if (chosen >= idDetailByFirst && chosen - idDetailByFirst < static_cast<int>(byPath.size())) {
		ShowCellDetail(event.GetRow(), event.GetCol(), byPath[chosen - idDetailByFirst]);
		return;
	}

	switch (chosen) {
	// ⭐ OPENING IS THE VALUE'S OWN, and the runtime does it: the cell is asked what it is bound to
	// and that value is shown. The document used to have a verb for this, which was the same two
	// lines with a door in front of them — and the door belonged to the value, not to the sheet.
	case idOpenValue: {
		ibValue bound;
		if (document->GetParameter(detailsParameter, bound))
			bound.ShowValue();
		break;
	}
	case idShowDetail:
		ShowCellDetail(event.GetRow(), event.GetCol());
		break;
	default:
		break;   // closed without choosing — the click meant nothing after all
	}
}

// (⚠ AND NOTHING IS BOUND TO THE RIGHT BUTTON. It belongs to copy / paste and cannot be taken over
//  (Max, 2026-08-26) — so detail lives on the left click, and the popup there is this control's own
//  rather than the editor's.)
