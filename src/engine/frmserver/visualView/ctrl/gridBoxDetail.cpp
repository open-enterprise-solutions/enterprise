#include "gridBox.h"
#include "backend/system/value/valueDataComposition.h"    // the schema a detail copies
#include "backend/system/value/valueSpreadsheetDetails.h"  // …and what the clicked cell was composed from
#include "backend/metadataReport.h"                        // the in-memory external report
#include "backend/metaCollection/metaComposerObject.h"     // …and the composer metaobject it declares
#include "backend/metaCollection/genericData.h"            // ibValueMetaObjectGenericData — the report a detail is named after
#include "frmserver/visualView/ctrl/form.h"                 // ibValueForm::GetMetaObject — …reached through the grid's form
#include "backend/backend_localization.h"                  // a title read in the reader's language
#include "backend/query/queryable.h"                       // ibReadPeriodUnit — the word a period grouping says
#include "frmserver/visualView/choiceRequest.h"            // ibMenuItem — the fields offered, as the click's submenu

#include <algorithm>

//***********************************************************************
//*        THE DETAIL — a figure asked what it was composed from        *
//***********************************************************************
//
// ⭐⭐ ITS OWN FILE, beside the events rather than inside them (the same split the events themselves
// were given). A click is one thing — a gesture, a choice, a routed answer — and a DETAIL is another:
// a composition narrowed to one cell and opened as a report of its own. Both were in the event file
// and the file had stopped being about events.
//
// Nothing here draws or waits: it reads what the cell carries, states a composition, and hands it to
// a report. That is why the pieces below are free functions over DESCRIPTIONS — they can be read,
// and reasoned about, without a window in sight.

namespace {

// WHAT A CELL WAS COMPOSED FROM — asked TWICE about one click: once to offer the fields worth
// breaking down by, once to narrow the detail itself. One lookup for both, so the choice and the
// report it opens can never disagree about the same cell.
void ibCollectCellContext(const ibBackendSpreadsheetObject* document, int row, int col,
	std::vector<ibValueSpreadsheetDetails::ibSpreadsheetDetailsField>& into)
{
	if (document == nullptr)
		return;

	wxString bound;
	document->GetCellDetailsParameter(row, col, bound);
	ibValue cellValue;   // held, not a temporary — From returns a pointer INTO it
	if (!bound.IsEmpty() && document->GetParameter(bound, cellValue)) {
		if (ibValueSpreadsheetDetails* details = ibValueSpreadsheetDetails::From(cellValue))
			details->CollectContext(into);
	}
}

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

// ⭐⭐ WHAT THE READER IS LOOKING AT — SECTION BY SECTION, AND `IsOk` ASKED OF THE PART. A reader who
// set only a filter has user settings that "are ok" and no structure at all: taken whole, that would
// hand the detail a report with no outputs, which composes nothing. Each part falls back to the
// author's variant on its own, which is the rule a run follows.
ibSettingsDescription ibSettingsInForce(const ibValueDataComposition& composition,
	const ibCompositionDescription& desc)
{
	ibSettingsDescription basis = desc.GetCompositionSettingsDesc();
	const ibSettingsDescription& reader = composition.GetUserSettingsDesc();

	if (reader.m_filter.IsOk())      basis.m_filter    = reader.m_filter;
	if (reader.m_sort.IsOk())        basis.m_sort      = reader.m_sort;
	if (!reader.m_structure.empty()) basis.m_structure = reader.m_structure;
	if (!reader.m_selected.empty())  basis.m_selected  = reader.m_selected;
	return basis;
}

// 🛑 A CONTEXT MUST **AND**, WHICH THE ROOT DOES NOT ALWAYS DO. A filter whose root joins with OR
// would take the cell's context as an ALTERNATIVE to the reader's conditions — a detail showing MORE
// than the report it was opened from, which is the one thing it can never be. So an OR root is
// pushed down into a group of its own and the root becomes an AND: the same filter, said in a way
// the context can be added to.
void ibForceAndRoot(ibFilterDescription& filter)
{
	if (filter.m_rootKind != ibFilterGroupKind_And && filter.IsOk()) {
		ibFilterNodeDescription kept;
		kept.m_kind      = ibFilterNodeKind_Group;
		kept.m_groupKind = filter.m_rootKind;
		kept.m_children  = filter.m_nodes;
		filter.m_nodes.assign(1, kept);
	}
	filter.m_rootKind = ibFilterGroupKind_And;
}

// ⭐⭐ A PERIOD HEADING STANDS FOR ITS WHOLE PERIOD, not for the moment it begins. What a heading of a
// `PERIODS(Month)` grouping carries is the month's first moment — the key the fold groups by — and
// `Period = 01.08.2026 00:00:00` is a condition no record meets, so the detail came up empty (Max,
// 2026-09-30). The unit is the grouping's own, on its line in the structure in force, so it is asked
// there.
bool ibPeriodUnitOfPath(const std::vector<ibLevelDescription>& levels, const wxString& path, ibTotalsPeriod& unit)
{
	for (const ibLevelDescription& level : levels) {
		for (const ibGroupLineDescription& line : level.m_settings.m_group.m_lines)
			if (line.m_path == path && line.m_periods.IsOk())
				return ibReadPeriodUnit(line.m_periods.m_unit, unit);
		if (ibPeriodUnitOfPath(level.m_children, path, unit))
			return true;
	}
	return false;
}

bool ibPeriodUnitOfPath(const std::vector<ibOutputDescription>& structure, const wxString& path, ibTotalsPeriod& unit)
{
	for (const ibOutputDescription& output : structure)
		if (ibPeriodUnitOfPath(output.m_rowGroups, path, unit) || ibPeriodUnitOfPath(output.m_columnGroups, path, unit))
			return true;
	return false;
}

// ⭐⭐ A CONDITION IS NOT A PATH AND A VALUE — IT IS A FIELD, WITH ITS SCHEMA. The settings window
// draws the left cell as the field it holds and the right one THROUGH THE LEFT'S TYPE
// (`AdjustValue(item->m_left.m_type, …)`), so a line carrying only a path and a value is a line with
// nothing to draw: both cells come up blank and the value cannot be edited either. A condition a
// person adds is filled in by the picker; one added from here says the same three things.
//
// ⭐ THE TYPE IS ASKED OF THE COMPOSITION, never worked out here: `GetConstructorFields` is the list
// the picker itself is built from, so there is ONE answer to "what does this query offer".
//
// (⚠ NO LEAF ID, deliberately: a field of a PARSED TEXT stands behind no metaobject attribute, and
//  the picker passes wxNOT_FOUND for exactly that reason.)
//
// ⭐ …AND A PERIOD HEADING IS SAID AS ITS PERIOD'S TWO EDGES — see ibPeriodUnitOfPath.
void ibAppendContextConditions(ibFilterDescription& filter,
	const std::vector<ibValueSpreadsheetDetails::ibSpreadsheetDetailsField>& context,
	const std::vector<ibOutputDescription>& structure,
	const ibValueDataComposition& composition, const ibCompositionDescription& desc)
{
	const std::vector<ibQueryConstructorField> offered = composition.GetConstructorFields();
	for (const ibValueSpreadsheetDetails::ibSpreadsheetDetailsField& field : context) {
		ibFilterOperandDescription left;
		left.m_path         = field.m_path;
		left.m_presentation = desc.TitleForPath(field.m_path);
		for (const ibQueryConstructorField& known : offered) {
			if (!known.m_name.IsSameAs(field.m_path, false))
				continue;
			left.m_type = known.m_type;
			if (left.m_presentation.IsEmpty())
				left.m_presentation = known.m_presentation;
			break;
		}

		ibTotalsPeriod unit = ibTotalsPeriod::Month;
		if (field.m_value.GetType() == ibValueTypes::TYPE_DATE && ibPeriodUnitOfPath(structure, field.m_path, unit)) {
			const ibDateTime date = field.m_value.GetDate();
			filter.Append(field.m_path, ibComparisonKind_GreaterEqual, ibValue(date.BeginOfPeriod(unit))).m_left = left;
			filter.Append(field.m_path, ibComparisonKind_LessEqual, ibValue(date.EndOfPeriod(unit))).m_left = left;
		}
		else
			filter.Append(field.m_path, ibComparisonKind_Equal, field.m_value).m_left = left;
	}
}

// ⭐⭐ ONE OUTPUT, ONE GROUPING — what "detail by this field" IS. The report the detail opens is then
// exactly "this cell, by this field": the filter above is the cell, the level below is the question.
// Everything else the composition declares — its query, resources, parameters, selected fields —
// travels untouched, which is why a field the report never grouped by works as well as one it did.
//
// ⚠ ONE OUTPUT, and that is not a guess about which output the cell belonged to: the cell does not
// carry that today. Building one is the honest answer — pretending to know which of several the
// reader clicked would be wrong exactly where it mattered.
std::vector<ibOutputDescription> ibStructureByField(const wxString& path)
{
	ibLevelDescription level;
	level.m_kind = ibCompositionLevelKind::Grouping;
	level.m_settings.m_group.Append(path);

	ibOutputDescription output;
	output.m_kind = ibCompositionOutputKind::Grouping;
	output.m_rowGroups.push_back(level);

	return std::vector<ibOutputDescription>(1, output);
}

// ⭐⭐ DETAIL IS A NEW REPORT OVER THE SAME SCHEMA — not this report re-drawn.
//
// A form cannot be copied (Max, 2026-08-26), and it does not need to be: what a reader is asking is
// "show me this figure broken down another way", and that is a COMPOSITION, not a window. So the
// schema is copied into a FRESH external report living in memory, and that report opens its own form
// — which it generates from its composer, so nothing has to be drawn.
//
// ⭐ THE WHOLE CYCLE ALREADY EXISTS. `ibMetaDataReport`'s constructor builds the root, registers it
// and stands up the module manager — "so a freshly created (not-from-file) report already has it".
// Opening from a file is the same path with `LoadCommonTree` in the middle; here the middle is a
// composer carrying a copy of the schema.
// `source` — the report the cell was composed in; the detail is named after it.
void ibOpenReportOver(const ibCompositionDescription& desc, const ibValueMetaObject* source)
{
	// A report in memory, held from birth: the ctor has already made its root and its module manager.
	const std::shared_ptr<ibMetaDataReport> detailMeta = ibMetaData::MakeShared<ibMetaDataReport>();
	ibValueMetaObject* root = detailMeta->GetCommonMetaObject();
	if (root == nullptr)
		return;

	// ⭐ …AND NAMED AFTER THE REPORT IT DETAILS. A detail is that report's figure broken down, so its window says
	// whose it is (Max, 2026-09-30); a fresh report answers with the default name, "External report1".
	if (source != nullptr) {
		root->SetName(source->GetName());
		root->SetSynonym(source->GetSynonym());
	}

	// …AND THE COMPOSER IT DECLARES. A report's main node IS its default composer, which is what puts
	// a gridbox on the generated form without anybody drawing one.
	ibValueMetaObjectComposer* composer =
		root->CreateMetaObjectAndSetParent<ibValueMetaObjectComposer>();
	if (composer == nullptr)
		return;

	// REGISTERED LIKE ANY OTHER METAOBJECT — an external report's tree is registered as it is built
	// (Max: "as an external report it still has to be registered"), and a node that skipped it has no
	// ctor behind its class.
	composer->OnCreateMetaObject(detailMeta.get(), newObjectFlag);
	composer->OnLoadMetaObject(detailMeta.get());
	composer->SetCompositionDesc(desc);

	if (!detailMeta->RunDatabase())
		return;

	// …and the object holds the container from its run on (the module manager RunDatabase builds hands it over), so
	// this reference may go: the form's life is what keeps the report alive.
	if (ibValueModuleRuntimeManagerExternalReport* manager = detailMeta->GetManagerModule()) {
		if (ibValueRecordDataObjectReport* object =
				dynamic_cast<ibValueRecordDataObjectReport*>(manager->GetObjectValue()))
			object->ShowFormValue();
	}
}

}   // namespace

// ⭐⭐ WHICH FIELDS THIS CELL CAN BE BROKEN DOWN BY — the second question a click asks, and the one
// the sheet cannot answer for the reader. Without a field named, the detail keeps the groupings its
// context has not pinned: right in the ordinary case, and no help at all when what a person wants is
// a field the report never grouped by ("show me this sum by warehouse").
//
// ⚠ WHAT IS OFFERED IS WHAT CAN BE GROUPED BY: the composition's own field list — the same one the
// settings window's pickers are built from — minus the RESOURCES (a figure is what is measured,
// never what it is measured by) and minus the fields this cell's context has already fixed to one
// value, which would fold into a single heading saying what the filter above it already says.
//
// ⭐ AND IT IS READ BY A PERSON, so the items say the TITLE — the same words that stand over the
// column in the report. The technical name travels into the query and is not something a reader
// should ever be shown.
std::vector<wxString> ibValueGridBox::AppendDetailByMenu(std::vector<ibMenuItem>& menu, s32 firstId, int row, int col) const
{
	std::vector<wxString> paths;

	ibValueDataComposition* composition = dynamic_cast<ibValueDataComposition*>(
		static_cast<ibValueSpreadsheetModel*>(m_spreadsheetModel));
	if (composition == nullptr)
		return paths;   // a drawn document has no schema — there is nothing to break anything down by

	std::vector<ibValueSpreadsheetDetails::ibSpreadsheetDetailsField> context;
	ibCollectCellContext(composition->GetSpreadsheetDocument().get(), row, col, context);

	const ibCompositionDescription& desc = composition->GetCompositionDesc();
	ibMenuItem byMenu{ 0, _("Detail by") };
	for (const ibQueryConstructorField& field : composition->GetConstructorFields()) {
		bool taken = false;
		for (const ibResourceDescription& resource : desc.m_resources)
			taken = taken || resource.m_path.IsSameAs(field.m_name, false);
		for (const ibValueSpreadsheetDetails::ibSpreadsheetDetailsField& pinned : context)
			taken = taken || pinned.m_path.IsSameAs(field.m_name, false);
		if (taken)
			continue;

		// …in the reader's language: a title is kept in every language it was written in, and a sheet reads
		// it as a cell lands (PutArea) — a menu is not a sheet, and printed it every language at once.
		wxString title = ibLocalization::GetTranslateGetRawLocText(ibBackendLocalization::GetUserLanguage(), desc.TitleForPath(field.m_name));
		if (title.IsEmpty())
			title = ibTitleFromName(field.m_name);
		byMenu.items.push_back(ibMenuItem{ firstId + static_cast<s32>(paths.size()), title });
		paths.push_back(field.m_name);
	}

	if (paths.empty())
		return paths;   // nothing left to break down by — an empty submenu is a dead end

	menu.push_back(std::move(byMenu));
	return paths;
}

// ⭐⭐ THE CELL, NARROWED AND OPENED. The figure was composed under a chain of headings, the composer
// wrote that chain into the cell, and here it is read back: every link is a condition, and what the
// reader sees below is either the field they named or the groupings their context has not pinned.
//
// ⚠ THE COPY IS THE POINT, and it must carry the PARAMETERS with it. A detail that re-read the data
// with different parameters — another period, another organisation — would show figures that do not
// add up to the one it was opened from, which is worse than showing nothing.
void ibValueGridBox::ShowCellDetail(int row, int col, const wxString& byPath)
{
	ibValueDataComposition* composition = dynamic_cast<ibValueDataComposition*>(
		static_cast<ibValueSpreadsheetModel*>(m_spreadsheetModel));
	if (composition == nullptr)
		return;   // a drawn document has no schema — see where the entry is offered

	// THE SCHEMA, WHOLE — query, parameters with the values the reader entered, resources, fields.
	ibCompositionDescription detailDesc = composition->GetCompositionDesc();

	std::vector<ibValueSpreadsheetDetails::ibSpreadsheetDetailsField> context;
	ibCollectCellContext(composition->GetSpreadsheetDocument().get(), row, col, context);

	// ⭐ A NAMED FIELD REPLACES THE STRUCTURE; AN UNNAMED ONE PRUNES IT. Those are the two questions a
	// click can ask, and they differ by one thing only — whether the reader said what to break the
	// figure down BY. With neither a context nor a field there is nothing to narrow, and the detail
	// is then honestly the same report over the same data.
	if (!context.empty() || !byPath.IsEmpty()) {
		ibSettingsDescription narrowed = ibSettingsInForce(*composition, detailDesc);

		ibForceAndRoot(narrowed.m_filter);
		ibAppendContextConditions(narrowed.m_filter, context, narrowed.m_structure, *composition, detailDesc);

		if (!byPath.IsEmpty()) {
			narrowed.m_structure = ibStructureByField(byPath);
		}
		else {
			for (ibOutputDescription& output : narrowed.m_structure) {
				ibDropPinnedLevels(output.m_rowGroups, context);
				ibDropPinnedLevels(output.m_columnGroups, context);
			}
		}
		detailDesc.GetCompositionSettingsDesc() = narrowed;
	}

	// …over the report this grid stands in — the form's own object (ibValueForm::GetMetaObject).
	const ibValueForm* form = GetOwnerForm();
	ibOpenReportOver(detailDesc, form != nullptr ? form->GetMetaObject() : nullptr);
}
