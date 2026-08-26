#include "gridBox.h"
#include "backend/system/value/valueDataComposition.h"   // the schema a detail copies
#include "backend/metadataReport.h"                       // the in-memory external report
#include "backend/metaCollection/metaComposerObject.h"    // …and the composer metaobject it declares


//***********************************************************************
//*                       Cell events — the click as a QUESTION         *
//***********************************************************************

// ⭐⭐ A CLICK ON A CELL IS A QUESTION ABOUT THE FIGURE — the report term is "detail processing".
// The sheet is not a picture: every figure was composed from something, and the cell already knows
// what (the composer stamps its value as the cell's details parameter). What was missing is the
// FORK — a report saying what a click means in it — so these two verbs are asked one step before
// the answer that already existed.
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
	// nothing a reader can follow — a caption, a blank — and a menu over it would be two commands
	// that do nothing. The click then means what it always meant, and the editor handles it.
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

	enum { idOpenValue = wxID_HIGHEST + 1, idShowDetail };
	wxMenu menu;
	menu.Append(idOpenValue, _("Open value"));
	// DETAIL is offered only where there is a composition to copy: a hand-filled sheet has no schema
	// and nothing to re-compose, so the entry would be a command that cannot mean anything.
	menu.Append(idShowDetail, _("Detail..."))
		->Enable(dynamic_cast<ibValueDataComposition*>(
			static_cast<ibValueSpreadsheetModel*>(m_spreadsheetModel)) != nullptr);

	switch (grid->GetPopupMenuSelectionFromUser(menu)) {
	case idOpenValue:
		document->OpenCellDetailsParameter(event.GetRow(), event.GetCol());
		break;
	case idShowDetail:
		ShowCellDetail(event.GetRow(), event.GetCol());
		break;
	default:
		break;   // closed without choosing — the click meant nothing after all
	}
}

// ⭐⭐ DETAIL IS A NEW REPORT OVER THE SAME SCHEMA — not this report re-drawn.
//
// A form cannot be copied (Max, 2026-08-26), and it does not need to be: what a reader is asking is
// "show me this figure broken down another way", and that is a COMPOSITION, not a window. So the
// schema is copied into a FRESH external report living in memory, and that report opens its own
// form — which it generates from its composer, so nothing has to be drawn.
//
// ⭐ THE WHOLE CYCLE ALREADY EXISTS. `ibMetaDataReport`'s constructor builds the root, registers it
// and stands up the module manager — "so a freshly created (not-from-file) report already has it".
// Opening from a file is the same path with `LoadCommonTree` in the middle; here the middle is a
// composer carrying a copy of the schema.
//
// ⚠ THE COPY IS THE POINT, and it must carry the PARAMETERS with it. A detail that re-read the data
// with different parameters — another period, another organisation — would show figures that do not
// add up to the one it was opened from, which is worse than showing nothing.
void ibValueGridBox::ShowCellDetail(int row, int col)
{
	ibValueDataComposition* composition = dynamic_cast<ibValueDataComposition*>(
			static_cast<ibValueSpreadsheetModel*>(m_spreadsheetModel));
	if (composition == nullptr)
		return;   // a drawn document has no schema — see where the entry is enabled

	// A report in memory: the ctor has already made its root and its module manager.
	ibMetaDataReport* detailMeta = new ibMetaDataReport();
	ibValueMetaObject* root = detailMeta->GetCommonMetaObject();
	if (root == nullptr) {
		wxDELETE(detailMeta);
		return;
	}

	// …AND THE COMPOSER IT DECLARES. A report's main node IS its default composer, which is what
	// puts a gridbox on the generated form without anybody drawing one.
	ibValueMetaObjectComposer* composer =
		root->CreateMetaObjectAndSetParent<ibValueMetaObjectComposer>();
	if (composer == nullptr) {
		wxDELETE(detailMeta);
		return;
	}
	// REGISTERED LIKE ANY OTHER METAOBJECT — an external report's tree is registered as it is built
	// (Max: "as an external report it still has to be registered"), and a node that skipped it has
	// no ctor behind its class.
	composer->OnCreateMetaObject(detailMeta, newObjectFlag);
	composer->OnLoadMetaObject(detailMeta);

	// THE SCHEMA, WHOLE — query, parameters with the values the reader entered, resources, fields.
	// ⏭ What still has to happen here is the DETAIL itself: the field clicked becomes the single
	// grouping and the cell's context becomes the filter. That needs the cell to know what it was
	// built from, which it does not yet — so for now this opens the same report over the same data,
	// which is the half that can be proved today.
	composer->SetCompositionDesc(composition->GetCompositionDesc());

	if (!detailMeta->RunDatabase()) {
		wxDELETE(detailMeta);
		return;
	}

	// …and the object owns the container from here on (ibValueRecordDataObjectExternalReport holds
	// it by RAII), so nothing is deleted below: the form's life is what keeps the report alive.
	if (ibValueModuleRuntimeManagerExternalReport* manager = detailMeta->GetManagerModule()) {
		if (ibValueRecordDataObjectReport* object =
				dynamic_cast<ibValueRecordDataObjectReport*>(manager->GetObjectValue()))
			object->ShowFormValue();
	}
}

// (⚠ AND NOTHING IS BOUND TO THE RIGHT BUTTON. It belongs to copy / paste and cannot be taken over
//  (Max, 2026-08-26) — so detail lives on the left click, and the popup there is this control's own
//  rather than the editor's.)
