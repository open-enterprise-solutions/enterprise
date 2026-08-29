#include "tableBox.h"
#include "tableBoxColumnRenderer.h"             // ibDataViewColumnObject — the current column carries its control
#include "backend/composition/drivers/spreadsheetComposeDriver.h"   // the SAME driver a report's sheet is drawn by
#include "backend/backend_spreadsheet.h"        // ibBackendSpreadsheetObject — the document the list is printed into
#include "backend/session/session.h"            // ibSession::CurrentFrame — the door a finished document is shown through
#include "backend/system/systemManager.h"       // ibValueSystemFunction::Message — the platform's own way to speak
#include "backend/backend_mainFrame.h"          // ibBackendDocFrame — ShowSpreadsheetDocument lives on it
#include "backend/composition/ramComposer.h"    // ibDataRamComposer — the composer a table of values prints through
#include <memory>
#include <wx/choicdlg.h>
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/picturePredefined.h"          // g_pic*CLSID — the TableBox composes the standard command band
#include "backend/compositionDescription.h"     // the description the quick filter writes into
#include "backend/appData.h"
#include "frontend/win/dlgs/settings/list/listSettings.h"   // the ONE door a model's settings are opened by
#include "frontend/win/dlgs/settings/composer/composerSettings.h"   // the saved-settings shelf — shared with the report's world
#include "backend/settings/settingsComposer.h"              // ibSettingsCategory — which shelf a list's settings sit on
#include "form.h"

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

	// The FRONT owns the rows a command runs against — read them once here. m_selection = the selected row
	// (delete / edit / copy target). m_anchor = WHERE a new element is created — CREATE always anchors here,
	// NEVER on the selection — resolved PER VIEW MODE:
	//   List         → flat, no hierarchy → no anchor (a new element lands at the root).
	//   Hierarchical → the folder the user has drilled INTO — the frozen top-parent crumb (rendered separately
	//                  above the scroll area), NOT GetTopItem (that returns the first NON-frozen visible row).
	//                  Empty when at the root → a new element lands at the root.
	//   Tree         → the folder the user stands in: the current item if it IS a folder, else its parent folder.
	ibDataViewCommandContext ctx;
	ctx.m_selection = m_tableCurrentLine != nullptr ? m_tableCurrentLine->GetLineItem() : ibDataViewItem();
	if (auto* ctrl = dynamic_cast<ibDataViewCtrl*>(GetInnerWx())) {
		switch (ctrl->GetViewMode()) {
		case ibDataViewHierarchical:
			ctx.m_anchor = ctrl->GetDrillHierarchyItem();   // the drilled-into folder (the hierarchical-drill crumb, rendered separately above the scroll)
			break;
		case ibDataViewTree: {
			const ibDataViewItem cur = ctrl->GetCurrentItem();
			ctx.m_anchor = cur.IsContainer() ? cur : cur.GetParentItem();
			break;
		}
		case ibDataViewList:
		default:
			break;   // flat list — no anchor
		}

		// …and the COLUMN the cursor stands on, read exactly where the by-column FILTER reads it
		// (Command_FilterByCurrentColumn). It travels as the column itself — name, synonym and the source
		// description — never as a number: a column bound to a HOP ("Product.Vendor") is a whole path, and
		// its name is the dotted field a sort is written against.
		if (ibDataViewColumn* const current = ctrl->GetCurrentColumn()) {
			if (auto* const columnObject = dynamic_cast<ibDataViewColumnObject*>(current)) {
				if (ibValueModelTableBoxColumn* const columnControl = columnObject->GetControl()) {
					ctx.m_column.m_name = columnControl->GetSourceFieldName();
					ctx.m_column.m_synonym = columnControl->GetCaption();
					ctx.m_column.m_source = columnControl->GetSourceDesc();
				}
			}
		}
	}

	switch (lNumAction)
	{
	case enTableSelect:         Command_Choose(srcForm);             break;   // returns the current ReturnLine
	case enTableFilter:         Command_ShowListSettings();          break;   // direct → the settings window
	case enTableFilterByColumn: Command_FilterByCurrentColumn();     break;   // direct → control + L5
	case enTableFilterClear:    Command_ClearFilter();               break;   // direct → L5
	case enTableViewMode:       Command_ShowViewMode();              break;   // direct → control
	case enTableSettingsRestore: Command_ShowSavedSettings(/*restore*/true);  break;
	case enTableSettingsSave:    Command_ShowSavedSettings(/*restore*/false); break;
	case enTableOutputList:      Command_OutputList();                        break;   // → a spreadsheet document
	default:
		// The model runs its command against the current ROW as-is (its Edit id has the eStartEditingFlag bit baked
		// in, so its own `case eEditValue` matches — a list opens the object form, a value-table does nothing there).
		// Then, if that bit is set, the FRONT ALSO forces the row's inline editor: a value-table / tabular row edits
		// inline (EditCurrentRow), a list row no-ops (not inline-editable — its form already opened). Tested per id.
		m_tableModel->CallAsCommand(lNumAction, ctx, srcForm);
		if (lNumAction & eStartEditingFlag)
			EditCurrentRow(ctx.m_selection);
		break;
	}
}

// Open the inline cell editor on `item`'s first editable cell (FRONT). Prefer the column the user is on; else the
// first editable column (skips a read-only one like the tabular line-number). false if no cell is editable (a list
// row — the caller opens the object form instead). Shared by double-click AND the Edit command's eStartEditingFlag
// intercept in CallAsAction.
bool ibValueModelTableBox::EditCurrentRow(const ibDataViewItem& item)
{
	if (!item.IsOk() || m_tableModel == nullptr)
		return false;

	auto* ctrl = dynamic_cast<ibDataViewCtrl*>(GetInnerWx());
	if (ctrl == nullptr)
		return false;

	ibDataViewColumn* editCol = nullptr;
	ibDataViewColumn* cur = ctrl->GetCurrentColumn();
	if (cur != nullptr && m_tableModel->EditableLine(item, cur->GetModelColumn()))
		editCol = cur;
	else
		for (unsigned int i = 0; i < ctrl->GetColumnCount(); ++i) {
			ibDataViewColumn* c = ctrl->GetColumn(i);
			if (c != nullptr && m_tableModel->EditableLine(item, c->GetModelColumn())) { editCol = c; break; }
		}

	if (editCol == nullptr)
		return false;

	ctrl->EditItem(item, editCol);
	return true;
}


//****************************************************************************
//*   Command handlers — the view-state band, driven DIRECTLY on the control *
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

// ⭐ ASKED FOR BY NAME — the settings window's own door, the same one the gridbox uses for a report.
// It used to be reached through a method of the CONTROL, which is a widget carrying the settings
// road inside it; the road belongs to the window that IS the settings.
void ibValueModelTableBox::Command_ShowListSettings()
{
	// ⭐ A COPY OF THE ACTIVE SETTING GOES IN, AND ON OK IT IS SET BACK ON THE MODEL — that is the
	// whole of it. Nothing is kept on this side: the active setting lives in the model's composer,
	// which is not serialised by the schema (Max, 2026-08-24).
	// …and the CONFIGURATION is handed in by the box: it knows which one it is showing, and a window
	// that went looking would be guessing between the several that are open (Max, 2026-08-24).
	ibDialogListSettings::ShowUserSettings(dynamic_cast<wxWindow*>(GetInnerWx()), m_tableModel, GetMetaData());
}

// ⭐⭐ THE SAME SHELF A REPORT HAS, addressed the same way: by the LEAF OF THE BINDING, so the
// settings belong to what is shown rather than to the widget showing it. They live in a category of
// their own, so a list's "Sales" and a report's can never be the same row (Max, 2026-08-26).
//
// The window and the two verbs are the composer's; nothing here is a second implementation. Which
// verb is which is the argument: restore asks WHICH one to put on, save asks WHERE to put what is in
// force.
void ibValueModelTableBox::Command_ShowSavedSettings(bool restore)
{
	if (m_tableModel == nullptr)
		return;

	wxWindow* over = dynamic_cast<wxWindow*>(GetInnerWx());
	const wxString objectKey = SettingsObjectKey();

	if (restore) {
		if (ibDialogComposerSettings::ShowRestoreSettings(over, m_tableModel->GetModelComposer(),
				ibSettingsCategory::List, objectKey, GetMetaData()))
			// ⚠ A LIST IS NOT A REPORT: it re-reads AT ONCE. The report's sheet stays the one that was
			// built until somebody says Compose; here the rows on screen are the answer to the setting
			// that was just replaced, so leaving them would show the previous setting's rows.
			m_tableModel->RefetchAll();
		return;
	}

	ibDialogComposerSettings::ShowSavedSettings(over, m_tableModel->GetModelComposer(),
		ibSettingsCategory::List, objectKey, GetMetaData());
}

void ibValueModelTableBox::Command_FilterByCurrentColumn()
{
	auto* ctrl = dynamic_cast<ibTableViewCtrl*>(GetInnerWx());
	if (ctrl == nullptr || m_tableModel == nullptr)
		return;

	// Current row + current column come straight off the live control — the model never pulls them.
	const ibDataViewItem sel = ctrl->GetSelection();
	ibDataViewColumn* col = ctrl->GetCurrentColumn();
	if (!sel.IsOk() || col == nullptr)
		return;

	const unsigned int colId = col->GetModelColumn();
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
		m_tableModel->RefetchAll();
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
// ⭐ AND THE COLUMNS ARE ASKED. The sheet repeats the box's own column layout, so what it may repeat is what
// the box shows; a person ticks off the ones they want and the rest take no column at all (the driver reads
// ibCompositionOutputInfo::m_shown, which is what the selected-fields table becomes).
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

	wxArrayString names;
	std::vector<ibValueModelTableBoxColumn*> offered;
	for (ibValueModelTableBoxColumn* column : columns) {
		if (column == nullptr || !column->GetVisibleColumn() || column->GetSourceFieldName().IsEmpty())
			continue;   // a hidden column is not on the screen; a column bound to nothing has no field to print
		names.Add(column->GetCaption().IsEmpty() ? column->GetSourceFieldName() : column->GetCaption());
		offered.push_back(column);
	}
	if (offered.empty())
		return;

	wxWindow* const over = dynamic_cast<wxWindow*>(GetInnerWx());
	wxMultiChoiceDialog chooser(over, _("Which columns do you want to see?"), _("Output list"), names);
	wxArrayInt all;
	for (size_t i = 0; i < offered.size(); ++i) all.Add(static_cast<int>(i));
	chooser.SetSelections(all);   // everything shown, ticked — the answer most people want is the default
	if (chooser.ShowModal() != wxID_OK)
		return;
	const wxArrayInt chosen = chooser.GetSelections();
	if (chosen.IsEmpty())
		return;

	// The settings in force, with the chosen columns as the fields to print.
	ibSettingsDescription settings = m_tableModel->GetModelComposer().GetCurrentSettingsDesc();
	settings.m_selected.clear();
	for (const int index : chosen)
		settings.m_selected.push_back(
			ibSelectedFieldDescription::Field(offered[static_cast<size_t>(index)]->GetSourceFieldName()));


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
	// ⭐ ASKED OF BOTH, IN THE RIGHT ORDER. The `ViewMode` PROPERTY is what the runtime sets — that is what
	// it was put there for — and the live CONTROL is what a person is actually looking at. They cannot
	// disagree for longer than one refresh (OnUpdated pushes the property onto the control, OnViewSet pushes
	// a person's switch back onto the property), so the control wins where there is one and the property
	// answers where there is not — the web front, which has no wxDVC at all.
	bool flatView = (m_propertyViewMode->GetValueAsEnum() == ibDataViewList);
	if (auto* const viewCtrl = dynamic_cast<ibDataViewCtrl*>(GetInnerWx()))
		flatView = (viewCtrl->GetViewMode() == ibDataViewList);

	// ⭐⭐ …AND THE FOLDER SOMEBODY IS STANDING IN IS THE DELIMITER. Drilled into a group and pressed
	// «output list» means that group — not the whole catalog (Max, 2026-08-29). It goes in as a FILTER LINE
	// on the copy's settings, not as a scope: a scope is the engine's own condition for one fetch, while
	// this is the person's own answer to "what am I looking at", and it must survive the whole print.
	//
	// `InHierarchy` rather than `=`: membership that walks DOWN, so the sub-folders under the one they
	// opened come with their contents instead of standing empty.
	if (!flatView) {
		if (auto* const viewCtrl = dynamic_cast<ibDataViewCtrl*>(GetInnerWx())) {
			const ibDataViewItem drilled = viewCtrl->GetDrillHierarchyItem();
			if (drilled.IsOk())
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
	}

	// The setting is put on AFTER the view mode has had its say — it is what the read composes on, so
	// clearing the grouping in the copy above and then assigning the old one back would undo it.
	own->SetUserSettingsDesc(settings);

	// ⭐⭐ …AND THE LADDER IS ASKED FOR, NOT ASSEMBLED HERE. Everything that used to stand in this place —
	// the row's identity, whether the source has a tree, which grouping becomes which level, where the
	// records go — is a question about the COMPOSITION and its SOURCE, and this widget knows neither. It
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

void ibValueModelTableBox::Command_ShowViewMode()
{
	if (auto* ctrl = dynamic_cast<ibTableViewCtrl*>(GetInnerWx()))
		ctrl->ShowViewMode();
}