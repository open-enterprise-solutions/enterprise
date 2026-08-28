#include "gridBox.h"
#include "backend/system/value/valueDataComposition.h"   // the schema a detail copies
#include "backend/system/value/valueSpreadsheetDetails.h" // …and what the clicked cell was composed from
#include "backend/metadataReport.h"                       // the in-memory external report
#include "backend/metaCollection/metaComposerObject.h"    // …and the composer metaobject it declares

#include <algorithm>

namespace {

// ⭐⭐ A GROUPING THE CONTEXT ALREADY PINS HAS NOTHING TO SHOW. A detail is the report minus the
// groupings, keeping the ones it is being broken down BY (Max, 2026-08-26) — and which those are is
// not a question anybody has to be asked: the cell's chain fixes its own levels to one value each,
// so a heading over them would print that one value and fold nothing. What is left below is exactly
// what the reader has not seen yet.
//
// ⚠ THE CHILDREN ARE LIFTED, NOT DROPPED WITH IT. A level that goes away takes its place in the
// ladder with it, not the levels underneath — those are the breakdown being asked for.
void ibDropPinnedLevels(std::vector<ibLevelDescription>& levels,
	const std::vector<ibValueSpreadsheetDetails::ibSpreadsheetDetailsField>& context)
{
	for (size_t at = 0; at < levels.size(); ) {
		ibDropPinnedLevels(levels[at].m_children, context);

		const std::vector<ibGroupLineDescription>& lines = levels[at].m_settings.m_group.m_lines;
		const bool pinned = !lines.empty() &&
			std::all_of(lines.begin(), lines.end(), [&context](const ibGroupLineDescription& line) {
				return std::any_of(context.begin(), context.end(),
					[&line](const ibValueSpreadsheetDetails::ibSpreadsheetDetailsField& field) {
						return field.m_path == line.m_path;
					});
			});
		if (!pinned) {
			++at;
			continue;
		}

		std::vector<ibLevelDescription> children = levels[at].m_children;
		levels.erase(levels.begin() + at);
		levels.insert(levels.begin() + at, children.begin(), children.end());
	}
}

}   // namespace


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
	ibCompositionDescription detailDesc = composition->GetCompositionDesc();

	// ⭐⭐ …AND NARROWED TO THE CELL THAT WAS CLICKED. The figure was composed under a chain of
	// headings, the composer wrote that chain into the cell, and here it is read back: every link is
	// a condition, and a grouping the conditions already pin has nothing left to show.
	//
	// THE BASIS IS WHAT THE READER IS LOOKING AT — the author's variant with the reader's own
	// sections laid over it, part by part, which is the rule a run follows. A detail built on the
	// author's settings alone while the reader had changed the report would answer about a report
	// nobody has on screen.
	std::vector<ibValueSpreadsheetDetails::ibSpreadsheetDetailsField> context;
	wxObjectDataPtr<ibBackendSpreadsheetObject> document = composition->GetSpreadsheetDocument();
	if (document != nullptr) {
		wxString bound;
		document->GetCellDetailsParameter(row, col, bound);
		ibValue cellValue;   // held, not a temporary — From returns a pointer INTO it
		if (!bound.IsEmpty() && document->GetParameter(bound, cellValue)) {
			if (ibValueSpreadsheetDetails* details = ibValueSpreadsheetDetails::From(cellValue))
				details->CollectContext(context);
		}
	}
	if (!context.empty()) {
		// ⭐⭐ SECTION BY SECTION, AND `IsOk` IS ASKED OF THE PART. A reader who set only a filter has
		// user settings that "are ok" and no structure at all — taken whole, that would hand the
		// detail a report with no outputs, which composes nothing. Each part falls back to the
		// author's on its own, which is the same rule a run follows.
		ibSettingsDescription narrowed = detailDesc.GetCompositionSettingsDesc();
		const ibSettingsDescription& reader = composition->GetUserSettingsDesc();
		if (reader.m_filter.IsOk())      narrowed.m_filter = reader.m_filter;
		if (reader.m_sort.IsOk())        narrowed.m_sort = reader.m_sort;
		if (!reader.m_structure.empty()) narrowed.m_structure = reader.m_structure;
		if (!reader.m_selected.empty())  narrowed.m_selected = reader.m_selected;

		// ⭐ AND THE CELL'S OWN CONTEXT IS ADDED TO IT — one condition per link, ANDed onto whatever
		// filter was already in force. Replacing it would answer about a different report: the
		// figure that was clicked was measured under the reader's filter too.
		//
		// 🛑 AND IT MUST **AND**, WHICH THE ROOT DOES NOT ALWAYS DO. A filter whose root joins with
		// OR would take the cell's context as an ALTERNATIVE to the reader's conditions — a detail
		// showing MORE than the report it was opened from, which is the one thing it can never be.
		// So an OR root is pushed down into a group of its own and the root becomes an AND: the same
		// filter, said in a way the context can be added to.
		if (narrowed.m_filter.m_rootKind != ibFilterGroupKind_And && narrowed.m_filter.IsOk()) {
			ibFilterNodeDescription kept;
			kept.m_kind = ibFilterNodeKind_Group;
			kept.m_groupKind = narrowed.m_filter.m_rootKind;
			kept.m_children = narrowed.m_filter.m_nodes;
			narrowed.m_filter.m_nodes.assign(1, kept);
		}
		narrowed.m_filter.m_rootKind = ibFilterGroupKind_And;

		// ⭐⭐ AND A CONDITION IS NOT A PATH AND A VALUE — IT IS A FIELD, WITH ITS SCHEMA. The window
		// draws the left cell as the field it holds and the right one THROUGH THE LEFT'S TYPE
		// (`AdjustValue(item->m_left.m_type, …)`), so a line carrying only a path and a value is a
		// line with nothing to draw: both cells come up blank and the value cannot be edited either.
		// A condition a person adds is filled in by the picker (see settingsFilterEditor's Add) —
		// one added from here has to say the same three things.
		//
		// ⭐ THE TYPE IS ASKED OF THE COMPOSITION, never worked out here: `GetConstructorFields` is
		// the same list the picker itself is built from, so there is one answer to "what does this
		// query offer" and a detail's line and a typed line are the same object.
		//
		// (⚠ NO LEAF ID, deliberately: a field of a PARSED TEXT stands behind no metaobject
		//  attribute, and the picker passes wxNOT_FOUND for exactly that reason.)
		const std::vector<ibQueryConstructorField> offered = composition->GetConstructorFields();
		for (const ibValueSpreadsheetDetails::ibSpreadsheetDetailsField& field : context) {
			ibFilterNodeDescription& line =
				narrowed.m_filter.Append(field.m_path, ibComparisonKind_Equal, field.m_value);
			line.m_left.m_presentation = detailDesc.TitleForPath(field.m_path);
			for (const ibQueryConstructorField& known : offered) {
				if (!known.m_name.IsSameAs(field.m_path, false))
					continue;
				line.m_left.m_type = known.m_type;
				if (line.m_left.m_presentation.IsEmpty())
					line.m_left.m_presentation = known.m_presentation;
				break;
			}
		}
		for (ibOutputDescription& output : narrowed.m_structure) {
			ibDropPinnedLevels(output.m_rowGroups, context);
			ibDropPinnedLevels(output.m_columnGroups, context);
		}
		detailDesc.GetCompositionSettingsDesc() = narrowed;
	}
	composer->SetCompositionDesc(detailDesc);

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
