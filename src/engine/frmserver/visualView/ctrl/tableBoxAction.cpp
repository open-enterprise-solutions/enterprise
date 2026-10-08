#include "tableBox.h"
#include "backend/composition/drivers/spreadsheetComposeDriver.h"   // the SAME driver a report's sheet is drawn by
#include "backend/backend_spreadsheet.h"        // ibBackendSpreadsheetObject — the document the list is printed into
#include "backend/session/session.h"            // ibSession::CurrentFrame — the door a finished document is shown through
#include "backend/system/systemManager.h"       // ibValueSystemFunction::Message — the platform's own way to speak
#include "backend/backend_mainFrame.h"          // ibBackendDocFrame — ShowSpreadsheetDocument lives on it
#include "backend/composition/ramComposer.h"    // ibDataRamComposer — the composer a table of values prints through
#include <algorithm>
#include <functional>
#include <memory>
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/picturePredefined.h"          // g_pic*CLSID — the TableBox composes the standard command band
#include "backend/compositionDescription.h"     // the description the quick filter writes into
#include "backend/appData.h"
#include "backend/settings/settingsComposer.h"  // the reader's shelf — ibSettingsCategory::List, restore / save
#include "frmserver/visualView/choiceRequest.h" // ibRequestChoice — the columns printed
#include "frmserver/win/dlgs/settings/savedSettings.h"   // ibDialogSavedSettings — the reader's shelf
#include "frmserver/win/dlgs/settings/list/listSettings.h"   // ibDialogListSettings — the reader's settings window
#include "protocol/protocol.h"     // ibProtocolRequestKind::ViewMode — the client's window of the view mode
#include "core/serialize/dataBuilder.h"      // ibDataNode — a request and its answer
#include "form.h"

// ⭐ A FILTER CHANGES WHICH ROWS EXIST, AND THE ROW THE PERSON WAS READING MUST NOT GO WITH THEM
// (Max, 2026-09-16: *"I put a filter on and take it off again, and the value flies away somewhere -
// and it is the one we were looking at"*). Taking a filter off puts back every row it had been
// hiding, so a list that comes back looking at the same TOP row is looking somewhere else entirely.
//
// The box already knows which row that is — GetCurrentLine(), kept for every other purpose — and it
// is what a reset with no anchor reads around (ibValueModelTableBox::Fetch). So nothing is captured
// or remembered by the commands below: the rows are re-read, the client resets, and the current row
// comes back into view. A row the new filter excludes is simply not found, and the list opens at the
// top - the only honest answer there.

//****************************************************************************
//*                              actionData                                  *
//****************************************************************************

// The TableBox composes its command interface the way a form does (formAction.cpp): it MERGES the bound model's
// OWN narrow command set and DECORATES it with the standard, table-generic band — Select (choice), Filter /
// FilterByColumn / FilterClear, ViewMode. The ids are the TableBox's own (high base, like the form's enClose)
// so they never collide with a model's object-command ids; unknown ids are OBJECT commands and go to the model.
enum
{
	enTableSelect = 20000,
	enTableFilter,
	enTableFilterByColumn,
	enTableFilterClear,
	enTableViewMode,
	// ⭐ THE READER'S OWN SETTINGS — a LIST HAS THEM TOO (Max, 2026-08-26). Not the variants question,
	// which a list legitimately has none of: a variant is something the AUTHOR named in the
	// configuration, while these are what THIS person arranged and chose to keep. They live under
	// their own category, addressed by this control's guid rather than by a composer's.
	enTableSettingsRestore,
	enTableSettingsSave,
	// ⭐⭐ OUTPUT LIST — what is on the screen, as a spreadsheet document. A verb of the TABLE, so every
	// list and every table of values has it for nothing (Max, 2026-08-29). It READS: the same rows, the
	// same filter, the same sort and the same groupings, printed the way a report is.
	enTableOutputList,
};

ibValueModelTableBox::ibStandardCommandSet ibValueModelTableBox::GetStandardCommands(const ibFormID& formType)
{
	// Resolve the model: the created one, or (unbound path) the bound form-attribute's model.
	ibValuePtr<ibValueModel> resolved;
	ibValueModel* model = m_tableModel;
	if (model == nullptr && !m_propertySource->IsEmptyProperty() && m_formOwner != nullptr &&
		m_formOwner->GetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), resolved))
		model = resolved;

	if (model == nullptr)
		return ibStandardCommandSet();

	ibStandardCommandSet actionData(this);

	// 1) Select — always FIRST, only when this table is a picker (the TableBox's own affordance). View-state,
	//    not a data change → stays live in a view-only form.
	if (IsChoiceMode())
		actionData.AddAction(wxT("Select"), _("Select"), g_picSelectCLSID, true, enTableSelect).SetModify(false);

	// 2) The model's OWN command set, merged in — the model is just a command STORE (GetCommandCollection), the TableBox
	//    lays it out into the real action (name / caption / picture / separators), carrying each command's modify flag.
	std::vector<ibCommandItem> commands;
	model->GetCommandCollection(formType, commands);

	for (const ibCommandItem& c : commands) {
		if (c.m_actionId == wxNOT_FOUND)
			actionData.AddSeparator();
		else
			actionData.AddAction(c.m_name, c.m_caption, c.m_pictureDescription, c.m_pictureAndText, c.m_actionId).SetModify(c.m_modifiesData);
	}

	// 3) The standard view-state band — Filter / by-column / clear, ViewMode — never changes DATA, so it stays
	//    live in a view-only form (only the model's Add / Delete / Copy row greys out).
	actionData.AddSeparator();
	actionData.AddAction(wxT("Filter"), _("Filter"), g_picFilterCLSID, false, enTableFilter).SetModify(false);
	// (⚠ NO VARIANTS HERE, and it is a fact about the ENTITY rather than a gap in this band. A
	//  variant is a setting the AUTHOR named and put in the configuration, and there is nowhere to
	//  name one for a list: the variants are edited in the composition's own window, which is the
	//  report's. A list's setting is what the reader themselves narrowed to, and there is one of it.
	//  Max, 2026-08-26, arriving at it while the button was being built: "for a report it is needed,
	//  truly" — so the verb lives on the composition and not on every control that shows rows.)
	actionData.AddAction(wxT("FilterByColumn"), _("Filter by column"), g_picFilterSetCLSID, false, enTableFilterByColumn).SetModify(false);
	actionData.AddAction(wxT("FilterClear"), _("Filter clear"), g_picFilterClearCLSID, false, enTableFilterClear).SetModify(false);

	// …and the shelf: what this person kept, and where to put what they have now. Two verbs, because
	// they are opposite acts and a person reaches for one of them knowing which. View-state, like
	// everything in this band — a saved setting narrows what is READ and stores nothing of the data.
	actionData.AddSeparator();
	actionData.AddAction(wxT("RestoreSettings"), _("Restore settings"), g_picSelectCLSID, false, enTableSettingsRestore).SetModify(false);
	actionData.AddAction(wxT("SaveSettings"), _("Save settings"), g_picSaveCLSID, false, enTableSettingsSave).SetModify(false);

	// ⭐⭐ …AND WHAT IS ON THE SCREEN, AS A DOCUMENT. Every list and every table of values gets this for
	// nothing, because it asks the composition already in force — the same rows, the same filter, the same
	// order, the same groupings — and prints them the way a report is printed (Max, 2026-08-29). It READS,
	// so it stays live in a view-only form.
	actionData.AddSeparator();
	actionData.AddAction(wxT("OutputList"), _("Output list"), g_picPrintCLSID, false, enTableOutputList).SetModify(false);

	actionData.AddSeparator();
	actionData.AddAction(wxT("ViewMode"), _("View mode"), g_picHierarchyCLSID, false, enTableViewMode).SetModify(false);

	return actionData;
}

void ibValueModelTableBox::CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm)
{
	if (m_tableModel == nullptr || appData->DesignerMode())
		return;

	// The TABLE owns the rows a command runs against — read them once here. m_selection = the current row
	// (delete / edit / copy target). m_anchor = WHERE a new element is created — CREATE always anchors here,
	// NEVER on the selection — resolved PER VIEW MODE (GetCreateAnchor).
	ibDataViewCommandContext ctx;
	ctx.m_selection = m_tableCurrentLine != nullptr ? m_tableCurrentLine->GetLineItem() : ibDataViewItem();
	ctx.m_anchor = GetCreateAnchor();

	// …and the COLUMN the cursor stands on, the one the by-column FILTER reads (Command_FilterByCurrentColumn).
	// It travels as the column itself — name, synonym and the source description — never as a number: a
	// column bound to a HOP ("Product.Vendor") is a whole path, and its name is the dotted field a sort is
	// written against.
	if (const ibValueModelTableBoxColumn* const columnControl = FindColumn(m_currentColumn)) {
		ctx.m_column.m_name = columnControl->GetSourceFieldName();
		ctx.m_column.m_synonym = columnControl->GetCaption();
		ctx.m_column.m_source = columnControl->GetSourceDesc();
	}

	switch (lNumAction)
	{
	case enTableSelect:         Command_Choose(srcForm);             break;   // returns the current ReturnLine
	case enTableFilterByColumn: Command_FilterByCurrentColumn();     break;   // direct → table + L5
	case enTableFilterClear:    Command_ClearFilter();               break;   // direct → L5
	case enTableOutputList:     Command_OutputList();                break;   // → a spreadsheet document
	case enTableViewMode:       Command_ShowViewMode();              break;   // direct → control
	case enTableSettingsRestore: Command_ShowSavedSettings(/*restore*/true);  break;
	case enTableSettingsSave:    Command_ShowSavedSettings(/*restore*/false); break;
	case enTableFilter:         Command_ShowListSettings();          break;   // direct → the settings window
	default:
		// The model runs its command against the current ROW as-is (its Edit id has the eStartEditingFlag bit baked
		// in, so its own `case eEditValue` matches — a list opens the object form, a value-table does nothing there).
		m_tableModel->CallAsCommand(lNumAction, ctx, srcForm);

		// …and when that bit is set, the row's editor opens — on the cell FindEditableColumn answers; on a row
		// with none (a list's) there is nothing to open.
		if ((lNumAction & eStartEditingFlag) != 0) {
			if (const ibValueModelTableBoxColumn* const column = FindEditableColumn(ctx.m_selection)) {
				m_editRow = FindRowHandle(ctx.m_selection);
				m_editColumn = column->GetControlID();
			}
		}
		break;
	}
}

// WHERE A NEW ELEMENT IS CREATED, per view mode:
//   List         → flat, no hierarchy → no anchor (a new element lands at the root).
//   Hierarchical → the folder the client has drilled INTO (the Parent of its last reset). Empty at the root →
//                  a new element lands at the root.
//   Tree         → the folder the person stands in: the current row if it IS a folder, else its parent folder.
ibDataViewItem ibValueModelTableBox::GetCreateAnchor() const
{
	switch (m_propertyViewMode->GetValueAsEnum()) {
	case ibDataViewHierarchical:
		return m_drillItem;
	case ibDataViewTree: {
		const ibDataViewItem current = m_tableCurrentLine != nullptr
			? m_tableCurrentLine->GetLineItem() : ibDataViewItem();
		return current.IsContainer() ? current : current.GetParentItem();
	}
	case ibDataViewList:
	default:
		return ibDataViewItem();   // flat list — no anchor
	}
}


//****************************************************************************
//*   Command handlers — the view-state band, driven DIRECTLY on the table   *
//****************************************************************************

void ibValueModelTableBox::Command_Choose(ibBackendValueForm* srcForm)
{
	// Choice returns the CURRENT ROW as a value — the ReturnLine, which itself pins the model alive for as long
	// as the caller (the opener) holds it. NotifyChoice hands it over; no reference re-resolution on the model.
	ibValueModel::ibValueModelReturnLine* line = GetCurrentLine();
	if (line == nullptr || srcForm == nullptr)
		return;

	// The picker returns the row's SELECT value — a reference / key, defined PER LINE TYPE (GetSelectValue),
	// not the generic row value.
	ibValue selectValue = line->GetSelectValue();
	srcForm->NotifyChoice(selectValue);
}

void ibValueModelTableBox::Command_ShowListSettings()
{
	// ⭐ A COPY OF THE ACTIVE SETTING GOES IN, AND ON OK IT IS SET BACK ON THE MODEL — that is the whole of it, and the
	// window's own door does it (ibDialogListSettings::ShowUserSettings). …and the CONFIGURATION is handed in by the
	// box: it knows which one it is showing.
	ibDialogListSettings::ShowUserSettings(m_tableModel, GetMetaData());
}

// ⭐⭐ THE SAME SHELF A REPORT HAS, addressed the same way: by the LEAF OF THE BINDING, so the
// settings belong to what is shown rather than to the control showing it. They live in a category of
// their own, so a list's "Sales" and a report's can never be the same row (Max, 2026-08-26).
//
// The window and the two verbs are the composer's; nothing here is a second implementation. Which
// verb is which is the argument: restore asks WHICH one to put on, save asks WHERE to put what is in
// force.
void ibValueModelTableBox::Command_ShowSavedSettings(bool restore)
{
	if (m_tableModel == nullptr)
		return;

	if (restore) {
		if (ibDialogSavedSettings::Show(m_tableModel->GetModelComposer(), ibDialogSavedSettings::Mode::Restore,
				ibSettingsCategory::List, SettingsObjectKey(), GetMetaData())) {
			// ⚠ A LIST IS NOT A REPORT: it re-reads AT ONCE. The report's sheet stays the one that was built until
			// somebody says Compose; here the rows on screen are the answer to the setting that was just replaced,
			// so leaving them would show the previous setting's rows.
			m_tableModel->RefetchAll();
		}
		return;
	}

	ibDialogSavedSettings::Show(m_tableModel->GetModelComposer(), ibDialogSavedSettings::Mode::Save,
		ibSettingsCategory::List, SettingsObjectKey(), GetMetaData());
}

void ibValueModelTableBox::Command_FilterByCurrentColumn()
{
	if (m_tableModel == nullptr)
		return;

	// Current row + current column are the table's own — the model never pulls them.
	const ibDataViewItem sel = m_tableCurrentLine != nullptr
		? m_tableCurrentLine->GetLineItem() : ibDataViewItem();
	const ibValueModelTableBoxColumn* col = FindColumn(m_currentColumn);
	if (!sel.IsOk() || col == nullptr)
		return;

	const unsigned int colId = static_cast<unsigned int>(col->GetModelColumn());
	ibValue value;
	m_tableModel->GetValueByMetaID(sel, colId, value);      // reading a cell value is a plain data op
	const wxString name = m_tableModel->GetColumnNameByID(colId);
	if (!name.empty()) {
		// ⭐ THROUGH THE SETTINGS, NEVER THROUGH THE COMPOSER'S OWN VERBS. This control says WHAT to
		// show; the model turns that into a read. So: take a COPY OF WHAT IS IN FORCE — the reader's
		// where they set one, the author's where they did not (starting from the user's section alone
		// would silently drop the author's filter the moment somebody narrowed by a column) — add the
		// condition, and hand the whole of it back as the user's. The list's own description, what the
		// configuration saved, is untouched.
		ibSettingsDescription settings = m_tableModel->GetModelComposer().GetCurrentSettingsDesc();
		settings.m_filter.Append(name, ibComparisonKind_Equal, value);
		m_tableModel->GetModelComposer().SetUserSettingsDesc(settings);
		m_tableModel->RefetchAll();   // the row whose value this filter is stays current — and in view
	}
}

void ibValueModelTableBox::Command_ClearFilter()
{
	if (m_tableModel == nullptr)
		return;
	// ⭐ CLEARING THE FILTER CLEARS THE FILTER. It used to assign an EMPTY SETTING, which wipes all
	// three sections — so a person who had set a sort and then pressed "Filter clear" lost the sort
	// too, under a command that says nothing about sorting (Max, 2026-08-24).
	//
	// An empty filter, not "no user setting": whatever else the reader chose is theirs and stays.
	// ⚠ And an empty filter is an ANSWER — the author's does NOT come back under it (2026-08-29: a
	// setting that exists answers every part). "Clear the filter" means no filter, which is what the
	// person pressing it asked for; going back to the developer's is `ClearUserSettings`, a different
	// verb that drops the setting whole.
	// The row being read outlives the clearing — it stays the current line, and the client's next reset
	// reads around it; without that the list comes back at the old top row with every previously hidden
	// row now standing between the two, and the value is off screen.
	ibSettingsDescription settings = m_tableModel->GetModelComposer().GetCurrentSettingsDesc();
	settings.m_filter.Clear();
	m_tableModel->GetModelComposer().SetUserSettingsDesc(settings);
	m_tableModel->RefetchAll();
}

// ⭐⭐ WHAT IS ON THE SCREEN, AS A DOCUMENT — «Output list».
//
// The whole feature is a JOINING, not an engine: the list already HAS a composition (its filter, its order,
// its groupings), a composition already knows how to print itself onto a sheet (the report's own
// ibSpreadsheetComposeDriver), and a finished sheet already knows how to be shown (ShowSpreadsheetDocument).
// This asks the first, hands it to the second and gives the result to the third.
//
// ⭐ A COMPOSER OF ITS OWN, seeded from what is in force (Max, 2026-08-29: *"take the settings that exist and
// drive them into your own separate composer"*). The list goes on reading while this one runs, and a second
// pass over the LIST's composer would be a second reader of one object — it registers parameters while it
// builds a filter, and the list fetches on another thread.
//
// ⭐ AND THE COLUMNS ARE CHOSEN. The sheet repeats the box's own column layout, so what it may repeat is what
// the box shows; a person ticks off the ones they want — asked as the desktop asks, every one ticked to begin with
// (ibRequestChoice, several) — and the rest take no column at all (the driver reads
// ibCompositionOutputInfo::m_shown, which is what the selected-fields table becomes). Cancelled — nothing is output.
void ibValueModelTableBox::Command_OutputList()
{
	if (m_tableModel == nullptr)
		return;

	// The columns this box shows, in the order it shows them — the layout the sheet repeats. Walked, not
	// looped: a column may sit inside a column GROUP, and groups nest (the same walk CreateTable makes).
	std::vector<ibValueModelTableBoxColumn*> columns;
	std::function<void(const ibValueFrame*)> walk = [&](const ibValueFrame* parent) {
		if (parent == nullptr)
			return;
		for (unsigned int idx = 0; idx < parent->GetChildCount(); idx++) {
			ibValueFrame* child = parent->GetChild(idx);
			if (child == nullptr)
				continue;
			if (ibValueModelTableBoxColumn* column = dynamic_cast<ibValueModelTableBoxColumn*>(child))
				columns.push_back(column);
			else if (dynamic_cast<ibValueModelTableBoxColumnGroup*>(child) != nullptr)
				walk(child);
		}
	};
	walk(this);

	std::vector<ibChoiceItem> names;
	std::vector<ibValueModelTableBoxColumn*> offered;
	for (ibValueModelTableBoxColumn* column : columns) {
		if (column == nullptr || !column->GetVisibleColumn() || column->GetSourceFieldName().IsEmpty())
			continue;   // a hidden column is not on the screen; a column bound to nothing has no field to print
		ibChoiceItem item;
		item.id = static_cast<s32>(offered.size());
		item.caption = column->GetCaption().IsEmpty() ? column->GetSourceFieldName() : column->GetCaption();
		item.selected = true;   // everything shown, ticked — the answer most people want is the default
		names.push_back(item);
		offered.push_back(column);
	}
	if (offered.empty())
		return;

	std::vector<s32> chosen;
	if (!ibRequestChoice(_("Which columns do you want to see?"), names, chosen))
		return;

	std::vector<ibValueModelTableBoxColumn*> printed;
	for (const s32 index : chosen)
		printed.push_back(offered[static_cast<std::size_t>(index)]);

	// The settings in force, with the chosen columns as the fields to print.
	ibSettingsDescription settings = m_tableModel->GetModelComposer().GetCurrentSettingsDesc();
	settings.m_selected.clear();
	for (const ibValueModelTableBoxColumn* column : printed)
		settings.m_selected.push_back(ibSelectedFieldDescription::Field(column->GetSourceFieldName()));


	// ⭐⭐ A COPY OF THE LIST'S OWN COMPOSER, and that is more faithful than building one (Max, 2026-08-29:
	// *"we can just copy the composer that exists and give it a new output"*). A settings description is not
	// the whole of what a list is reading: the GROUPING LADDER set imperatively (AddGroup / ClearGroups)
	// lives in the composer's own store, not in the setting, so a composer assembled from the setting alone
	// would print without a grouping the screen plainly shows. The copy carries everything — the source it
	// is bound to included, which is why the kind is asked for and nothing is re-bound.
	//
	// ⚠ …AND TWO THINGS IT MUST NOT INHERIT: the DRIVERS on its outputs (they point at whatever the original
	// was last printed into) and the per-read scope with its registered parameters (the engine's own
	// condition for ONE fetch — the folder somebody drilled into). Cleared here, so the copy is what the
	// list READS and nothing of how it happened to be reading it.
	std::unique_ptr<ibDataComposer> own(m_tableModel->GetModelComposer().Clone());
	if (!own)
		return;

	for (ibDataComposer::Output& output : own->Outputs())
		output.m_driver = nullptr;
	own->ClearScope();

	// ⭐⭐ …AND THE GROUPING BECOMES A LADDER OF LEVELS. A LIST does not need one: it draws its own tree and
	// reads a level at a time as somebody drills, so its grouping lives in the SETTING and the ladder stays
	// empty. A SHEET is not drilled — it is printed whole — and the driver lays out headings and the rows
	// under them from the LEVELS. With none, the read folded by the setting and the sheet got one column of
	// headings and no detail records at all (Max, 2026-08-29: *"it seems to output the grouping, and the
	// detail records do not appear"*).
	//
	// `WantsDetails` asks the ladder too, so without this the rows were not merely unprinted — they were
	// never read. One level per grouping line, in order, through the ordinary door.
	// ⭐⭐ THE VIEW MODE DECIDES, AND IT WINS OVER A STORED GROUPING — the same rule the model's own read
	// follows (a flat List view passes the ignore-parent sentinel, and the grouping is off: *"the user set
	// the Flat view → a flat table, even with a grouping configured"*). So a box showing a FLAT list prints
	// a flat list — every row a detail record — whatever the setting still holds (Max, 2026-08-29).
	//
	// ⭐ ASKED OF THE PROPERTY. The `ViewMode` PROPERTY is what the runtime sets, and a person's switch on the
	// client is written back onto it (OnViewModeChanged) — the same one the next fetch reads, so it is what
	// the person is actually looking at.
	const bool flatView = (m_propertyViewMode->GetValueAsEnum() == ibDataViewList);

	// ⭐⭐ …AND THE FOLDER SOMEBODY IS STANDING IN IS THE DELIMITER. Drilled into a group and pressed
	// «output list» means that group — not the whole catalog (Max, 2026-08-29). It goes in as a FILTER LINE
	// on the copy's settings, not as a scope: a scope is the engine's own condition for one fetch, while
	// this is the person's own answer to "what am I looking at", and it must survive the whole print.
	//
	// `InHierarchy` rather than `=`: membership that walks DOWN, so the sub-folders under the one they
	// opened come with their contents instead of standing empty.
	// ⭐⭐ …AND A GROUP SOMEBODY HAS OPENED IS THE DELIMITER TOO. "Where am I" was asked of ONE road —
	// the hierarchical-drill crumb, which `SetTopParent` fills when a person walks into a FOLDER. A
	// tree of GROUPINGS sets no crumb (it is empty in List and Tree mode by definition), so standing
	// inside a group and pressing «output list» printed the whole table from the root and folded it
	// afresh: the list was reading `WHERE Attribute4 = <group> TOTALS BY Attribute3` and the sheet
	// rendered `TOTALS BY Attribute4, Attribute3` with no WHERE at all (journal, 2026-08-30).
	//
	// Hence "it works on catalogs" — a catalog folder IS the crumb road — and hence Max's reading of
	// the sheet: *"the value is wrong… when you enter a group you should print the top element once
	// and expand the current one fully"*.
	//
	// ⭐ THE NODE ALREADY KNOWS. A row of this model IS an ibComposerNode (`ibDataViewItem(this)`),
	// and a node carries the rungs it stands under — root → itself. One equality per rung, against
	// the grouping line of the same depth, and the rows come out matched by the filter exactly as the
	// hierarchy shows them (Max: *"they will simply match by the filter"*). The levels above print
	// once as the top; the level you are in expands.
	if (!flatView) {

		// 🛑 THE ENGINE'S SCOPE CANNOT BE ASKED HERE, and a probe proved it: it read 0 conditions
		// while the fetch a second earlier carried both. The model overlays the scope for ONE fetch
		// and takes it straight back (`MarkScope` … restore, tabularModelDb.cpp) — the right tier,
		// the wrong moment.
		//
		// ⭐ WHAT SURVIVES IS THE NODE. The model builds that very scope out of the browsed node's
		// GROUP PATH (`pnode->GetGroupPath()`), so the path IS the position, and it lasts as long as
		// the tree does. One equality per rung, against the grouping line of the same depth: the
		// levels above print once as the top, the level you stand in expands, and the rows come out
		// matched by the filter exactly as the hierarchy shows them.
		// ⭐ THE BOX ALREADY HOLDS THE CURRENT ROW — `m_tableCurrentLine`, kept in step with the
		// client's cursor (Max, 2026-08-30: *"the current row you can get from the box itself"*). One
		// holder of that fact, and the print reads the same one every other command on this band reads.
		//
		// ⭐⭐ …AND WHAT DELIMITS IS THE GROUP THE ROW STANDS IN, not the row. A heading's path ends with its
		// own value, so standing on a heading printed that one group alone, while a person means the level
		// they are looking at (Max, 2026-09-29: *"not where you stand, but which group you are in"*). The
		// group a row stands in is its parent in the tree; a top-level heading has none, and the whole list
		// is printed.
		const ibValueModel::ibValueModelReturnLine* const line = GetCurrentLine();
		const ibDataViewItem where = line != nullptr ? line->GetLineItem() : ibDataViewItem();
		const ibDataViewItem in = m_tableModel->GetParent(where);
		const ibComposerNode* const node = m_tableModel->GetViewData<ibComposerNode>(in);
		const std::vector<ibValue> path = node != nullptr ? node->GetGroupPath() : std::vector<ibValue>();
		const std::vector<ibGroupLineDescription>& rungs = settings.m_group.m_lines;

		size_t fixed = 0;
		for (size_t at = 0; at < path.size() && at < rungs.size(); ++at) {
			if (rungs[at].m_path.IsEmpty())
				continue;
			settings.m_filter.Append(rungs[at].m_path, ibComparisonKind_Equal, path[at]);
			fixed++;
		}

		// The probe separates the ways this comes back empty, because they want different answers:
		// no current item, a row standing at the root, and a group that is not a node.
		ibJournalInfo(wxT("ui.list"),
			wxT("output list: item %s, in %s, %u rung(s) deep, %u grouping line(s), %u filter(s)"),
			where.IsOk() ? wxT("ok") : wxT("none"),
			!in.IsOk() ? wxT("the root") : node != nullptr ? wxT("a group") : wxT("not a node"),
			static_cast<unsigned>(path.size()), static_cast<unsigned>(rungs.size()), static_cast<unsigned>(fixed));

		// 🛑 …AND ONLY WHEN WHAT WE ARE INSIDE IS A ROW. `InHierarchy` below is stated over the
		// source's PRIMARY KEY — "this document is inside that folder" — so the value handed to it
		// has to be a row of this source. A GROUPING heading is not: its value is an attribute
		// (`Attribute4`), and `Ref InHierarchy <an Attribute4>` matches nothing at all, which is why
		// drilling into a grouped tree printed an empty sheet rather than a wrong one (Max,
		// 2026-08-30: *"as soon as the hierarchical view appears it stops outputting anything"*).
		//
		// A node that IS a row says so — it carries the source's key. A heading carries none. The folder
		// drilled into is the one the client read its level of last (m_drillItem, Hierarchical only).
		const ibDataViewItem drilled = m_drillItem;
		const ibComposerNode* const inside = m_tableModel->GetViewData<ibComposerNode>(drilled);
		if (drilled.IsOk() && inside != nullptr && !inside->GetRowKey().empty())
			if (const ibBackendQueryable* const queryable = m_tableModel->GetSourceQueryable()) {
				// …stated over the REFERENCE, like the hierarchy level above: "this row is inside that
				// folder" is a fact about the row, and the engine walks the parent map to answer it.
				const std::vector<const ibBackendQueryColumn*> key = queryable->GetPrimaryKeyColumns();
				if (!key.empty() && key.front() != nullptr) {
					ibValuePtr<ibValueModel::ibValueModelReturnLine> folder(m_tableModel->GetRowAt(drilled));
					if (folder != nullptr)
						settings.m_filter.Append(key.front()->GetName(),
							ibComparisonKind_InHierarchy, folder->GetSelectValue());
				}
			}
	}

	// The setting is put on AFTER the view mode has had its say — it is what the read composes on, so
	// clearing the grouping in the copy above and then assigning the old one back would undo it.
	own->SetUserSettingsDesc(settings);

	// ⭐⭐ …AND THE LADDER IS ASKED FOR, NOT ASSEMBLED HERE. Everything that used to stand in this place —
	// the row's identity, whether the source has a tree, which grouping becomes which level, where the
	// records go — is a question about the COMPOSITION and its SOURCE, and this control knows neither. It
	// answered them anyway, and the report answered them differently, which is how one state came to print
	// two different sheets (Max, 2026-08-29: *"our job is to bring these two paths together"*, and:
	// *"one serious divergence and everything falls apart"*).
	//
	// What is passed is what only the box knows: whether a person is looking at a tree or at a flat table.
	own->BuildPrintLevels(!flatView, m_tableModel->GetSourceQueryable());

	// The sheet is titled by the FORM it was output from — that is what a person will call this page a
	// week later; the control's own name means nothing outside the designer.
	const wxString title = m_formOwner != nullptr && !m_formOwner->GetCaption().IsEmpty()
		? m_formOwner->GetCaption() : wxString(_("List"));

	wxObjectDataPtr<ibBackendSpreadsheetObject> sheet(new ibBackendSpreadsheetObject());
	ibSpreadsheetComposeDriver driver(sheet.get());
	driver.SetTitle(title);

	// The ONE-DRIVER entrance — "the short way in for a caller holding a single driver — a list", which is
	// exactly what this is. A list has one output and says so at the call.
	if (!own->Run(driver)) {
		ibValueSystemFunction::Message(_("The list could not be output"), ibStatusMessage::ibStatusMessage_Warning);
		return;
	}

	if (ibBackendDocFrame* const frame = ibSession::CurrentFrame())
		frame->ShowSpreadsheetDocument(title, sheet);
}

// The desktop's ibTableViewCtrl::ShowViewMode, its window the client's (wxTableViewModeDialog): the mode in force sent,
// the one chosen put on as the control's own change of it is.
void ibValueModelTableBox::Command_ShowViewMode()
{
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr)
		return;

	ibDataNode request;
	request.SetValue(wxT("Kind"), static_cast<s32>(ibProtocolRequestKind::ViewMode));
	request.SetValue(wxT("Picture"), wxString(ibBackendPicture::GetServerPicture(g_picHierarchyCLSID).GetData()));
	request.SetValue(wxT("Mode"), static_cast<s32>(m_propertyViewMode->GetValueAsEnum()));

	ibDataNode response;
	if (!frame->Request(request, response) || response.FindField(wxT("Mode")) == nullptr)
		return;   // cancelled

	const s32 chosen = response.GetValue<s32>(wxT("Mode"));
	if (chosen >= ibDataViewTree && chosen <= ibDataViewList)
		OnViewModeChanged(static_cast<ibDataViewViewMode>(chosen));
}