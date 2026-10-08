#include "gridBox.h"
#include "backend/system/value/valueDataComposition.h"   // …is there a schema behind this sheet at all
#include "core/serialize/dataBuilder.h"                // ibDataNode — an event's arguments
#include "frmserver/visualView/choiceRequest.h"           // ibRequestMenu — what a click offers, the desktop's popup menu
#include "frmserver/docView/templates/docViewSpreadsheet.h"   // the document the box holds

//***********************************************************************
//*                       Cell events — the click as a QUESTION         *
//***********************************************************************
//
// (⭐ THE DETAIL ITSELF LIVES IN gridBoxDetail.cpp — a click is a gesture, a choice and a routed
//  answer; narrowing a composition to one cell and opening it as a report is another thing
//  entirely, and it had grown into this file until the file stopped being about events.)

// A cell names itself on the wire as Fetch wrote it — from 1; the sheet, and the handler, count from 0. A
// click that names no cell is no click.
bool ibValueGridBox::OnClientEvent(ibProtocolEvent event, const ibDataNode& args)
{
	switch (event) {
	case ibProtocolEvent::Cell: {
		const int row = args.GetValue<s32>(wxT("Row")) - 1, col = args.GetValue<s32>(wxT("Col")) - 1;
		if (row >= 0 && col >= 0)
			OnCellLeftClick(row, col);
		return true;
	}
	case ibProtocolEvent::Change: {
		// THE SHEET AS THE PERSON LEFT IT — edited on the client and handed over whole (Change {Sheet}): the sheet on
		// show from then on, which is the model's own. Its version stays: the client holds that sheet already.
		ibSpreadsheetDescription sheet;
		if (!m_gridDocument->GetSpreadsheet().IsEditable()
			|| !ibSpreadsheetDescriptionMemory::ReadNode(args.GetProperty(wxT("Sheet")), sheet))
			return false;
		m_gridDocument->SetSpreadsheetDesc(std::move(sheet));
		return true;
	}
	default:
		return ibValueWindowComposite::OnClientEvent(event, args);
	}
}

// ⭐⭐ A CLICK ON A CELL IS A QUESTION ABOUT THE FIGURE — the report term is "detail processing".
// The sheet is not a picture: every figure was composed from something, and the cell already knows
// what (the composer stamps its value as the cell's details parameter). What was missing is the
// FORK — a report saying what a click means in it — so these verbs are asked one step before the
// answer that already existed.
//
// ⚠ THE HANDLER RUNS FIRST, AND ITS `StandardProcessing` DECIDES THE REST. Cleared, the default is
// dropped: no value opened, no choice offered. Left as it is, everything below happens as it always
// did. Same shape the tablebox's Selection event has, deliberately — a reader who has learned one
// knows the other.
void ibValueGridBox::OnCellLeftClick(int row, int col)
{
	ibValue standardProcessing = true;
	CallAsEvent(m_eventOnDetailProcessing,
		GetValue(),                          // control
		ibValue(row),                        // row
		ibValue(col),                        // column
		standardProcessing
	);
	if (!standardProcessing.GetBoolean())
		return;   // the runtime said no — nothing opens, nothing is offered

	// ⭐⭐ A CLICKED FIGURE HAS MORE THAN ONE ANSWER, SO IT ASKS. Opening the value is one of them;
	// "how was this built" is another, and there is no way to guess which the person meant. So the
	// click offers them — first the value, then the detail (Max, 2026-08-26, over the reference
	// report: "the right button is taken by copying, we cannot reuse it; on the LEFT click drop a
	// menu — open the value first, detail second").
	//
	// ⚠ ONLY WHERE THERE IS SOMETHING TO ANSWER. A cell with no details parameter was composed from
	// nothing a reader can follow — a caption, a blank — and a menu over it would be commands that
	// do nothing. The click then means nothing. Asked of the sheet ON SHOW: the cell clicked is one
	// the person was shown.
	const wxObjectDataPtr<ibBackendSpreadsheetObject> document = m_gridDocument->GetSpreadsheetDocument();
	if (!document)
		return;

	wxString detailsParameter;
	document->GetCellDetailsParameter(row, col, detailsParameter);
	if (detailsParameter.IsEmpty())
		return;

	// DETAIL is offered only where there is a composition to copy: a hand-filled sheet has no schema
	// and nothing to re-compose, so the entry would be a command that cannot mean anything.
	const bool composed = ResolveComposition() != nullptr;

	enum : s32 { idOpenValue = 1, idShowDetail, idDetailByFirst };
	std::vector<ibMenuItem> menu;
	menu.push_back(ibMenuItem{ idOpenValue, _("Open value") });
	ibMenuItem detail{ idShowDetail, _("Detail...") };
	detail.enabled = composed;
	menu.push_back(detail);
	// …and one entry per field this cell can be broken down by — the submenu answers for itself,
	// including whether there is anything to offer (gridBoxDetail.cpp).
	const std::vector<wxString> byPath = AppendDetailByMenu(menu, idDetailByFirst, row, col);

	s32 chosen = 0;
	if (!ibRequestMenu(menu, chosen))
		return;   // closed without choosing — the click meant nothing after all

	if (chosen >= idDetailByFirst && chosen - idDetailByFirst < static_cast<s32>(byPath.size())) {
		ShowCellDetail(row, col, byPath[static_cast<size_t>(chosen - idDetailByFirst)]);
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
		ShowCellDetail(row, col);
		break;
	default:
		break;
	}
}

// (⚠ AND NOTHING IS BOUND TO THE RIGHT BUTTON. It belongs to copy / paste and cannot be taken over
//  (Max, 2026-08-26) — so detail lives on the click a client reports as Cell, and what it offers is
//  this control's own.)
