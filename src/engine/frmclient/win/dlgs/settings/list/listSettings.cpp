#include "frmclient/win/dlgs/settings/list/listSettings.h"
#include "frmclient/win/dlgs/settings/settingsFieldTree.h"     // which fields this list has — one answer
#include "frmclient/win/dlgs/settings/settingsFilterEditor.h"   // SHARED with the composer's world
#include "frmclient/win/dlgs/settings/settingsSortEditor.h"     // …and so is this one
#include "frmclient/win/dlgs/settings/settingsConditionalAppearanceEditor.h"   // …and so is the conditional appearance
#include "frmclient/win/dlgs/settings/settingsStyle.h"          // art / grid styling, spelled once
#include "frmclient/win/dlgs/callbackDropTarget.h"                  // the same-process drag: the source knows what moved
#include "frmclient/win/dlgs/rowValueCell.h"                        // ibRowValueCellRenderer — the shared value cell
#include <wx/stc/stc.h>                                            // the arbitrary query is shown in the styled editor

#include "frmclient/backend/appData.h"                        // appData->DesignerMode() — the Query tab is a developer tool
#include "frmclient/backend/serialize/dataProtocol.h"         // the setting and its fields, as the server's question carries them
#include "frmclient/backend/metaCollection/partial/reference/reference.h"   // which of the fields' types are references

#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/treectrl.h>
#include <wx/splitter.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/stattext.h>
#include <wx/toolbar.h>
#include <wx/app.h>

// Group tab dataview columns.
// COLUMN 0 IS RESERVED by the ibDataViewCtrl fork (a model column 0 paints blank
// and does not edit) — which is why these start at 1. Starting at 0 is what once
// made the Field cell impossible to open at all: F2 and the Select button had
// nothing to open.
enum { eGroupField = 1, eGroupKind };

// ---- Group model — virtual list over the dialog's BUFFER group list (Field). ----
class ibListSettingsPanel::ibGroupModel : public ibDataViewVirtualListModel {
	ibListSettingsPanel* m_dialog;
	ibSettingsListedLines m_listed;   // which lines the rows are — a line on an unavailable field is not listed
public:
	explicit ibGroupModel(ibListSettingsPanel* dialog) : ibDataViewVirtualListModel(), m_dialog(dialog) {}
	ibGroupDescription* GetGroup() const { return m_dialog->GetGroupList(); }
	void ResetFromList() {
		m_listed = ibSettingsListedLines();
		if (const ibGroupDescription* g = GetGroup())
			m_listed.Read(g->m_lines, m_dialog->m_fieldSource.get());
		Reset(m_listed.Count());
	}
	// The line a row is — see ibSettingsListedLines.
	size_t LineAt(size_t row) const { return m_listed.At(row); }
	unsigned int GetListedCount() const { return m_listed.Count(); }
	virtual void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override {
		ibGroupDescription* g = GetGroup();
		if (g == nullptr) return;
		// BOUNDS FIRST. The view paints rows it has, the list may already have fewer
		// (a Reset lands after the paint is queued) — reading past the end crashed
		// on repaint.
		const size_t at = LineAt(row);
		if (at >= g->m_lines.size())
			return;
		if (col == eGroupField)
			variant = g->m_lines[at].m_path;
		else if (col == eGroupKind)
			variant = ibValue::CreateEnumObject<ibValueEnumGroupKind>(
				g->m_lines[at].m_kind).GetString();
	}
	virtual bool SetValueByRow(const wxVariant&, unsigned, unsigned) override { return false; }
};

// ===========================================================================
//  Construction, buffers, and the small shared accessors
// ===========================================================================

// ⭐⭐ ONE CTOR, AND NOTHING RUNNING IN IT (Max, 2026-08-24: "every UI element is driven through its
// description — why are you keeping those lists in there at all").
//
// This panel used to take a dynamic list, or any model, and read the runtime for four different
// things: which tabs to offer, where the fields come from, the arbitrary query, and the setting
// itself. Every one of those is IN the description — the query is a string it carries, the fields
// are what parsing that string yields, the setting is handed in, and a description forbids no tab.
// So the runtime was not a source of anything, only a second way to reach the same facts.
//
// `settings` is what the window EDITS; the caller took the copy and decides what accepting it means.
ibListSettingsPanel::ibListSettingsPanel(wxWindow* parent, ibCompositionDescription& desc,
	const ibMetaData* metaData, int pages)
	: wxPanel(parent, wxID_ANY),
	  m_schema(&desc), m_desc(&desc), m_metaData(metaData),
	  m_fieldSource(new ibSettingsFieldTree()), m_pages(pages)
{
	BuildPages();
}

// ⭐⭐ THE READER'S ROAD — see the header. The schema arrives CONST and stays that way; `settings` is
// the one thing written, and it is the caller's copy.
ibListSettingsPanel::ibListSettingsPanel(wxWindow* parent, const ibCompositionDescription& schema,
	const ibMetaData* metaData, ibSettingsDescription& settings,
	std::vector<ibSettingsPlainField> fields, int pages)
	: wxPanel(parent, wxID_ANY),
	  m_schema(&schema), m_metaData(metaData), m_settings(&settings), m_plainFields(std::move(fields)),
	  m_fieldSource(new ibSettingsFieldTree()), m_pages(pages)
{
	BuildPages();
}

// (Out of line, so the header can forward-declare the field tree it holds by pointer.)
ibListSettingsPanel::~ibListSettingsPanel() = default;

void ibListSettingsPanel::BuildPages()
{
	BindFieldSource();   // the editors below fill their trees as they are built

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	wxNotebook* notebook = new wxNotebook(this, wxID_ANY);
	ibStyleSettingsTabs(notebook);
	// ⭐ TABS ARE GATED BY WHAT THE HOST ASKED FOR, and by nothing else. A DESCRIPTION forbids no
	// part of itself: every one of them is there to be written. The gate used to ask a MODEL for
	// its Features — and a window opened without one got `Features{}`, flags zero, which reads as
	// "everything is off": an empty notebook with an OK button over blank grey.
	//
	// THE QUERY TAB IS A DEVELOPER TOOL — writing the source query is configuring the form, not
	// using it; an end user opening "Filter" has no business being offered it.
	if ((m_pages & Page_Query) != 0 && m_desc != nullptr && appData->DesignerMode())
		notebook->AddPage(BuildQueryPage(notebook), _("Query"), true, ibSettingsTabArt(ibSettingsTab::Query));

	// ⭐ THE TWO SHARED EDITORS. Built, not written: the composer's window builds the very same
	// pair over its own buffer, which is the whole point of them living one level up.
	//
	// ⭐ AND THEY REPORT THE SAME WAY the composer's pair does. `MarkModified` is deliberately empty
	// HERE (see it: a list has no document to mark), but the query-text edit already announces
	// through it, so leaving the editors unwired made the one verb answer for some edits and not
	// others — and the override the comment promises would have inherited that hole (audit,
	// 2026-08-24).
	if ((m_pages & Page_Filter) != 0) {
		m_filterEditor = new ibFilterEditor(notebook, &EditedSettings().m_filter, m_fieldSource.get());
		m_filterEditor->SetOnChanged([this] { MarkModified(); });
		// ⭐ WHOSE WINDOW THIS IS. A handed-in setting means a READER opened it, and a line the
		// author marked inaccessible is hidden from them — applied, never shown. The designer's
		// road (no setting handed in, the description edited directly) sees everything.
		m_filterEditor->SetAuthoring(m_settings == nullptr);
		notebook->AddPage(m_filterEditor, _("Filter"), notebook->GetPageCount() == 0, ibSettingsTabArt(ibSettingsTab::Filter));
	}
	if ((m_pages & Page_Sort) != 0) {
		m_sortEditor = new ibSortEditor(notebook, &EditedSettings().m_sort, m_fieldSource.get());
		m_sortEditor->SetOnChanged([this] { MarkModified(); });
		notebook->AddPage(m_sortEditor, _("Sort"), notebook->GetPageCount() == 0, ibSettingsTabArt(ibSettingsTab::Sort));
	}
	// …and the list's OWN fold, which the composer does not share: its structure is a tree of
	// levels, not a flat ordered list (Max, 2026-08-20).
	if ((m_pages & Page_Group) != 0)
		notebook->AddPage(BuildGroupPage(notebook), _("Group"), notebook->GetPageCount() == 0,
			ibSettingsTabArt(ibSettingsTab::Grouping));
	// ⭐ …AND WHAT STANDS OUT, WHEN — the rules a list's grid paints its rows by, the very editor a report's
	// settings carry (Max, 2026-09-30: "conditional appearance lives in lists and in reports, one mechanism").
	if ((m_pages & Page_ConditionalAppearance) != 0) {
		m_conditionalAppearanceEditor = new ibConditionalAppearanceEditor(notebook,
			&EditedSettings().m_conditionalAppearance, m_fieldSource.get(), [this] { return m_metaData; });
		m_conditionalAppearanceEditor->SetOnChanged([this] { MarkModified(); });
		notebook->AddPage(m_conditionalAppearanceEditor, _("Conditional appearance"), notebook->GetPageCount() == 0,
			ibSettingsTabArt(ibSettingsTab::ConditionalAppearance));
	}

	mainSizer->Add(notebook, 1, wxEXPAND);
	SetSizer(mainSizer);

	// ⭐ THE AUTHOR'S ROAD OPENS ON A COPY — the buffer this window can drop. A reader's road was
	// handed one by the box and edits THAT in place, so there is nothing to copy for it.
	//
	// ⚠ TAKEN AFTER THE EDITORS ARE BUILT, and that is safe because they hold POINTERS INTO the
	// buffer: assigning it fills the very structures they are standing over, and their addresses do
	// not move.
	if (m_settings == nullptr && m_desc != nullptr)
		m_edited = m_desc->GetCompositionSettingsDesc();

	LoadFromSettings();
}

ibGroupDescription* ibListSettingsPanel::GetGroupList()
{
	return &EditedSettings().m_group;
}

// THE FIELD PICKER, forwarded to the one thing that knows which fields exist.
ibValueCompositionField* ibListSettingsPanel::ChooseField(wxWindow* parent, const wxString& currentPath)
{
	return m_fieldSource != nullptr ? m_fieldSource->ChooseField(parent != nullptr ? parent : this, currentPath) : nullptr;
}

// WHERE THIS PANEL'S FIELDS COME FROM. A dynamic list and a model that IS a source describe
// themselves — walk the explorer, so a reference field unfolds into its target's. Anything else
// has flat columns, and even those get their [+] when the type is a reference.
void ibListSettingsPanel::BindFieldSource()
{
	if (m_fieldSource == nullptr)
		return;

	const ibMetaData* metaData = SourceMetaData();

	// (A query describing its own fields is the configuration's to read — on the client the server describes them.)

	// ⭐ …AND WHERE THERE IS NO QUERY, THE FIELDS WERE DESCRIBED TO US. A value table or a tabular
	// section has columns and no text; the caller states them as data and this window uses them
	// exactly as it uses the parsed ones. It used to reach into the live model for this — the same
	// facts, fetched through an object instead of being told.
	m_fieldSource->SetPlainFields(m_plainFields, metaData);
}

// ===========================================================================
//  Page construction — the tabs this world owns
// ===========================================================================

// The FIRST tab (dynamic-list only) — arbitrary-query source: an enable flag + the query text. When off, the list
// takes its picked metaobject source; when on, this TEXT is the source (composer.FromText). Edits the list's OWN
// UseCustomQuery / CustomQuery properties (serialised), NOT the settings buffer; applied to the list on OK.
wxWindow* ibListSettingsPanel::BuildQueryPage(wxWindow* WXUNUSED(parent))
{
	// THE DESIGNER'S TAB — the query a list reads, and its constructor. The client is never the designer (BuildPages asks
	// appData->DesignerMode()), so it is never built here.
	return nullptr;
}

// WHICH FIELDS EXIST CAN CHANGE UNDER THE PANEL, and not only through its own Query tab: an
// embedder edits the query somewhere else entirely, and the trees here were filled ONCE, when the
// pages were built.
//
// ⭐ That is what "available fields do not work" was (Max, 2026-08-19): the panel showed the answer
// as it stood at construction time — empty, because the source had not described its query yet —
// while the field PICKER, which fills its tree at the moment it opens, showed the fields correctly.
// One question, two moments, two answers. The host says when the answer changed.
void ibListSettingsPanel::ReloadFields()
{
	BindFieldSource();
	if (m_filterEditor != nullptr) m_filterEditor->ReloadFields();
	if (m_sortEditor   != nullptr) m_sortEditor->ReloadFields();
	if (m_fieldSource != nullptr) m_fieldSource->Populate(m_groupFieldTree);
}

// APPLY THE QUERY AND REBUILD WHAT DEPENDS ON IT. One place, because "the query changed" has exactly
// one consequence: the list reads something else, so the fields on offer are something else.
//
// The ENGINE's verdict is shown as it came. A query that cannot be described cannot be run, and
// finding that out here — in front of its author — is the whole point of describing it early.
void ibListSettingsPanel::ApplyQueryToList()
{
	// ⭐ THE DESCRIPTION ROAD — the text goes into the description and the fields are re-read from it.
	// There is no engine to ask and nothing to apply: what a query offers is what parsing it says.
	if (m_desc != nullptr) {
		if (m_queryText != nullptr)
			m_desc->m_query = m_queryText->GetText();

		// (What the text offers, and whether it compiles, is the engine's to say — the designer's tab, not built here.)

		ReloadFields();
		ShowQueryFault();

		// …AND THE CHANGE IS ANNOUNCED where it is made, exactly as the composer's window announces
		// its own — so the two behave the same when they are attributes of a form.
		MarkModified();
		return;
	}

	// (NO SECOND ROAD. There was one that pushed the text into a LIVE list and asked it what the
	//  engine thought — the description road above answers both from the text itself.)
	if (m_queryError != nullptr) {
		const wxString error = m_queryFault;
		m_queryError->SetLabel(error);
		m_queryError->Show(!error.IsEmpty());
		if (m_queryError->GetParent() != nullptr)
			m_queryError->GetParent()->Layout();
	}
}

// THE GROUP TAB — the list's own fold, a flat ordered list of Field + Kind.
wxWindow* ibListSettingsPanel::BuildGroupPage(wxWindow* parent)
{
	wxPanel* panel = new wxPanel(parent);
	wxSplitterWindow* splitter = new wxSplitterWindow(panel, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitter->SetMinimumPaneSize(FromDIP(120));

	// ---- LEFT pane: available fields (dot-walkable) ----
	wxPanel* leftPane = new wxPanel(splitter);
	wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
	leftSizer->Add(new wxStaticText(leftPane, wxID_ANY, _("Available fields")), 0, wxALL, FromDIP(4));
	m_groupFieldTree = new wxTreeCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	leftSizer->Add(m_groupFieldTree, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(4));
	leftPane->SetSizer(leftSizer);

	// ---- RIGHT pane: the grouping list — Field + Kind, model-driven ----
	wxPanel* rightPane = new wxPanel(splitter);
	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
	m_groupView = new ibDataViewCtrl(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
	ibStyleSettingsGrid(m_groupView);

	// WHO KNOWS THE SOURCE opens the picker.
	ibRowValueCellRenderer::FieldChooser chooser =
		[this](wxWindow* pickerParent, const wxString& held) -> ibValueCompositionField* {
			return ChooseField(pickerParent, held);
		};

	m_groupView->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Field"),
		new ibRowValueCellRenderer(this, chooser,
			[this](const ibDataViewItem& row) -> ibValue {
				const size_t idx = GroupIndexAt(row);
				ibGroupDescription* g = GetGroupList();
				if (g == nullptr || idx >= g->m_lines.size() || g->m_lines[idx].m_path.IsEmpty())
					return ibValue();
				return ibValue(new ibValueCompositionField(g->m_lines[idx].m_path));
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				ibGroupDescription* g = GetGroupList();
				const size_t idx = GroupIndexAt(row);
				if (g == nullptr || idx >= g->m_lines.size())
					return;
				// An empty value CLEARS the line's field (same reason as the sort editor).
				// ⚠ THE LINE IS EDITED WHERE IT STANDS, never removed and re-added: the second MOVES it
				// to the end, and order is the meaning of a grouping list ("by Warehouse, then by Item"
				// is not the other report).
				ibValueCompositionField* field = nullptr;
				const bool chosen = value.ConvertToValue(field) && field != nullptr;
				g->m_lines[idx].m_path = chosen ? field->GetPath() : wxString();
				if (m_groupModel != nullptr) m_groupModel->ResetFromList();
			}),
		eGroupField, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT));

	m_groupView->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Kind"),
		new ibRowValueCellRenderer(this, chooser,
			[this](const ibDataViewItem& row) -> ibValue {
				ibGroupDescription* g = GetGroupList();
				const size_t idx = GroupIndexAt(row);
				return (g != nullptr && idx < g->m_lines.size())
					? ibValue::CreateEnumObject<ibValueEnumGroupKind>(
						g->m_lines[idx].m_kind)
					: ibValue();
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				ibGroupDescription* g = GetGroupList();
				const size_t idx = GroupIndexAt(row);
				// THE KIND ALONE CHANGES. A grouping line IS a path plus a kind, and writing the kind
				// where it stands is the whole edit — rebuilding the line from an object that only
				// existed half the time is what used to crash the designer here ("added a field,
				// switched it to HierarchyOnly, crash").
				if (g != nullptr && idx < g->m_lines.size())
					g->m_lines[idx].m_kind = value.ConvertToEnumValue<ibQueryDimUnfold>();
				if (m_groupModel != nullptr) m_groupModel->ResetFromList();
			}),
		eGroupKind, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT));

	rightSizer->Add(m_groupView, 1, wxALL | wxEXPAND, FromDIP(4));

	// Same toolbar, same verbs — a grouping list is ordered too: "by Warehouse,
	// then by Item" is a different report from the other way round.
	wxToolBar* toolbar = new wxToolBar(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
	toolbar->SetToolBitmapSize(FromDIP(wxSize(16, 16)));
	toolbar->AddTool(wxID_ADD, _("Add"),
		ibSettingsArt(wxASCII_STR(wxART_NEW), this), _("Add (Ins)"));
	toolbar->AddTool(wxID_REMOVE, _("Delete"),
		ibSettingsArt(wxASCII_STR(wxART_DELETE), this), _("Delete (Del)"));
	toolbar->AddSeparator();
	toolbar->AddTool(wxID_UP, _("Move up"),
		ibSettingsArt(wxASCII_STR(wxART_GO_UP), this), _("Move up"));
	toolbar->AddTool(wxID_DOWN, _("Move down"),
		ibSettingsArt(wxASCII_STR(wxART_GO_DOWN), this), _("Move down"));
	toolbar->Realize();
	// THE BAR KEEPS THE GRID'S MARGINS. Stretched edge to edge over a view inset by 4, it overhung
	// the grid's top-left corner and the left border read as unfinished (Max, 2026-08-19: "the left
	// border where the field is is not fully visible - and it is like that everywhere").
	rightSizer->Insert(0, toolbar, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(4));
	rightPane->SetSizer(rightSizer);

	toolbar->Bind(wxEVT_TOOL, &ibListSettingsPanel::OnGroupAdd, this, wxID_ADD);
	toolbar->Bind(wxEVT_TOOL, &ibListSettingsPanel::OnGroupRemove, this, wxID_REMOVE);
	toolbar->Bind(wxEVT_TOOL, [this](wxCommandEvent&) { MoveGroupLine(-1); }, wxID_UP);
	toolbar->Bind(wxEVT_TOOL, [this](wxCommandEvent&) { MoveGroupLine(+1); }, wxID_DOWN);

	// WIDE ENOUGH FOR WHAT THE FIELDS ARE CALLED. At 180 a name like "Account dimension Dr1"
	// is cut after "Account dimension", so eight distinct slots read as eight identical rows and
	// the only way to tell them apart is to count positions. The pane is user-resizable; what
	// changes here is what it shows BEFORE anyone resizes it.
	splitter->SplitVertically(leftPane, rightPane, FromDIP(260));
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(splitter, 1, wxEXPAND);
	panel->SetSizer(panelSizer);

	if (m_fieldSource != nullptr) {
		m_fieldSource->Populate(m_groupFieldTree);
		m_fieldSource->Attach(m_groupFieldTree);   // unfold a reference, drag a field out
	}
	m_groupFieldTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibListSettingsPanel::OnGroupFieldActivated, this);
	rightPane->SetDropTarget(new ibCallbackDropTarget([this] {
		if (m_fieldSource != nullptr) AddGroupForField(m_fieldSource->GetDragItem());
	}));

	m_groupView->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](ibDataViewEvent& e) {
		if (m_groupView != nullptr)
			m_groupView->EditItem(e.GetItem(), e.GetDataViewColumn());
		e.Skip();
	});

	m_groupModel = new ibGroupModel(this);
	m_groupView->AssociateModel(m_groupModel);
	m_groupView->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibListSettingsPanel::OnListContextMenu, this);
	m_groupModel->ResetFromList();

	return panel;
}

// ===========================================================================
//  Load / apply / the metadata door
// ===========================================================================
//
//  Every editor is model-driven and edits its buffer list in place, so there is no
//  per-tab copy step — Load just syncs the row counts, Apply is a no-op. The buffer
//  is committed to the composer on OK.

void ibListSettingsPanel::LoadFromSettings()
{
	// THE TEXT IS THE QUERY — nothing else to sync, and nothing to enable or disable by: an empty
	// text is a list with no query of its own, which is a state and not a mode.
	if (m_schema != nullptr && m_queryText != nullptr)
		m_queryText->SetText(m_schema->m_query);
	// Every editor binds straight to its part of the copy (Filter / Order / Group) —
	// just sync them to what the copied description put there.
	if (m_filterEditor != nullptr) m_filterEditor->Reload();
	if (m_sortEditor   != nullptr) m_sortEditor->Reload();
	if (m_groupModel   != nullptr) m_groupModel->ResetFromList();
	if (m_conditionalAppearanceEditor != nullptr) m_conditionalAppearanceEditor->Reload();
}

// RE-READ THE SETTINGS THEMSELVES — what the panel edits changed under it.
//
// ⭐ THIS IS WHAT PICKING A VARIANT NEEDS. The setting the panel stands over is replaced wholesale,
// and the panel has to start over on it. Everything else is unchanged — the SAME object, so the
// buffer lists and the models bound to them stay valid; only its contents are new.
void ibListSettingsPanel::ReloadSettings()
{
	if (m_settings == nullptr) {
		if (m_desc == nullptr)
			return;
		m_edited = m_desc->GetCompositionSettingsDesc();
	}
	LoadFromSettings();
}

void ibListSettingsPanel::ApplyToSettings()
{
	// Query tab (dynamic-list only) — the flag and the text are applied AS THEY CHANGE (the field
	// pickers on the other tabs are built from them), so this is the last word rather than the only
	// one: it catches a text edited and then OK'd without the editor ever losing focus.
	ApplyQueryToList();
	// Every editor edits its buffer list IN PLACE through its model — nothing to copy back here.
	// The whole copy goes back to the model on OK (SetCompositionDesc).
}

// The config metaData that resolves reference targets — the dynamic list's own, else the ACTIVE
// config. Without a valid metaData, ConvertToMetaIds returns nothing and every field looks like a
// leaf (no [+]) — which is exactly the "flat list" bug.
// SOMETHING CHANGED — see the header. NOTHING HAPPENS HERE, and that is the whole answer for this
// window: a list has no settings of its own and lives only inside an ATTRIBUTE (Max, 2026-08-24), so
// there is no document to mark and nobody to tell mid-edit. Its modified-ness is the OK: the value
// is set, the cascade runs, the attribute changes, and the whole snapshot lands in the composer.
//
// The hook stays because the composer's panel has the same verb — and because a road WITH a document
// (a list edited on a tab of its own, if one ever appears) overrides exactly here.
void ibListSettingsPanel::MarkModified()
{
}

// THE RED LINE — see the header. One place, because two moments say it: the text changing, and the
// window OPENING. The composer's window carries the identical pair.
void ibListSettingsPanel::ShowQueryFault()
{
	if (m_queryError == nullptr)
		return;
	m_queryError->SetLabel(m_queryFault);
	m_queryError->Show(!m_queryFault.IsEmpty());
	if (m_queryError->GetParent() != nullptr)
		m_queryError->GetParent()->Layout();
}

const ibMetaData* ibListSettingsPanel::SourceMetaData() const
{
	// ⭐ THE CONFIGURATION THIS WINDOW WAS TOLD, and never the active one: a window may be shown for a
	// configuration that is not the one in front (there are several open at once). The caller hands
	// it in with the settings; nothing here goes looking.
	return m_metaData != nullptr ? m_metaData : activeMetaData;
}

// ===========================================================================
//  GROUP tab — its context menu and its verbs
// ===========================================================================

// THE SAME FOUR VERBS THE TOOLBAR CARRIES — add, delete, and the two that make an
// ordered list ordered. The menu used to offer two of them, so the same tab answered
// differently depending on where the user clicked.
void ibListSettingsPanel::OnListContextMenu(ibDataViewEvent&)
{
	wxMenu menu;
	ibAppendCmd(menu, wxID_ADD, _("Add") + wxT("\tIns"), wxASCII_STR(wxART_NEW), this);
	ibAppendCmd(menu, wxID_REMOVE, _("Delete") + wxT("\tDel"), wxASCII_STR(wxART_DELETE), this);
	menu.AppendSeparator();
	ibAppendCmd(menu, wxID_UP, _("Move up"), wxASCII_STR(wxART_GO_UP), this);
	ibAppendCmd(menu, wxID_DOWN, _("Move down"), wxASCII_STR(wxART_GO_DOWN), this);

	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnGroupAdd(e); }, wxID_ADD);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnGroupRemove(e); }, wxID_REMOVE);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent&)   { MoveGroupLine(-1); }, wxID_UP);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent&)   { MoveGroupLine(+1); }, wxID_DOWN);

	PopupMenu(&menu);
}

// A virtual-list row id is 1-based, and the row shows the line the model maps it to.
size_t ibListSettingsPanel::GroupIndexAt(const ibDataViewItem& row) const
{
	const size_t id = reinterpret_cast<size_t>(row.GetID());
	return id > 0 && m_groupModel != nullptr ? m_groupModel->LineAt(id - 1) : (size_t)-1;
}

// Add the chosen available field to the grouping list (BUFFER + model refresh).
void ibListSettingsPanel::AddGroupForField(const wxTreeItemId& item)
{
	ibValuePtr<ibValueCompositionField> field(ibSettingsFieldTree::FieldAt(m_groupFieldTree, item));
	ibGroupDescription* g = GetGroupList();
	if (!field || g == nullptr)
		return;
	g->Append(field->GetPath(), ibQueryDimUnfold::Elements);
	if (m_groupModel != nullptr) {
		m_groupModel->ResetFromList();
		ibSelectLastSettingsRow(m_groupView, m_groupModel->GetListedCount());
	}
}

void ibListSettingsPanel::OnGroupAdd(wxCommandEvent&)
{
	ibGroupDescription* g = GetGroupList();
	if (g == nullptr)
		return;
	g->Append(wxEmptyString, ibQueryDimUnfold::Elements);
	if (m_groupModel != nullptr) {
		m_groupModel->ResetFromList();
		ibSelectLastSettingsRow(m_groupView, m_groupModel->GetListedCount());
	}
}
void ibListSettingsPanel::OnGroupFieldActivated(wxTreeEvent& e) { AddGroupForField(e.GetItem()); }

void ibListSettingsPanel::OnGroupRemove(wxCommandEvent&)
{
	ibGroupDescription* g = GetGroupList();
	if (g == nullptr || m_groupView == nullptr)
		return;
	const ibDataViewItem& sel = m_groupView->GetSelection();
	if (!sel.IsOk())
		return;
	// ONE LINE LEAVES, the rest keep their order — which is the meaning of a
	// grouping list, so rebuilding the whole thing to drop one row is never right.
	const size_t index = GroupIndexAt(sel);
	if (index >= g->m_lines.size())
		return;
	g->m_lines.erase(g->m_lines.begin() + index);
	if (m_groupModel != nullptr)
		m_groupModel->ResetFromList();
}

void ibListSettingsPanel::MoveGroupLine(int delta)
{
	ibGroupDescription* g = GetGroupList();
	if (g == nullptr || m_groupView == nullptr)
		return;
	const ibDataViewItem& sel = m_groupView->GetSelection();
	if (!sel.IsOk())
		return;
	const size_t index = reinterpret_cast<size_t>(sel.GetID());   // 1-based ROW
	if (m_groupModel == nullptr || index == 0 || index > m_groupModel->GetListedCount())
		return;
	const int target = static_cast<int>(index - 1) + delta;
	if (target < 0 || target >= static_cast<int>(m_groupModel->GetListedCount()))
		return;   // already at that end
	// Traded with the next LISTED line — a hidden one keeps its place.
	std::swap(g->m_lines[m_groupModel->LineAt(index - 1)], g->m_lines[m_groupModel->LineAt(static_cast<size_t>(target))]);
	m_groupModel->ResetFromList();
	const size_t moved = (size_t)((int)index + delta);
	m_groupView->Select(ibDataViewItem(reinterpret_cast<void*>(moved)));
}

// ===========================================================================
//  Commit and entry points
// ===========================================================================

// COMMIT — what OK does, minus the closing. FALSE means the panel refused and already said
// why, so the host must stay open on the offending setting.
bool ibListSettingsPanel::Commit()
{
	ApplyToSettings();   // UI → buffer
	// COMMIT the buffer onto the composer (the store) + refresh — the whole transaction lands atomically on OK.
	// Cancel never reaches here, so the composer stays untouched.
	// THE SAME CHECK THE RUNTIME MAKES. A half-written line raises there; here that
	// exception becomes a warning and the form stays open on the offending setting,
	// instead of closing and quietly dropping it.
	try {
		ibValidateSettings(EditedSettings());
	}
	catch (const ibBackendException& err) {
		wxMessageBox(err.GetErrorDescription(), _("List settings"), wxOK | wxICON_WARNING, this);
		return false;   // the host stays open — nothing is committed
	}

	// ⭐⭐ THE DESCRIPTION ROAD — the snapshot IS the composer description, so committing is one
	// assignment into it and nothing else. Nobody is told and nothing is applied: the caller holds a
	// CLONE, and setting that clone back as the property's value is what carries the change onward —
	// through the grid's cycle to the form attribute, which is where modified-ness is decided
	// (Max, 2026-08-24: "we work with snapshots, and the snapshot is the composer description").
	// ⭐⭐ HANDED BACK, AND THAT IS ALL. The caller gave the setting; the caller decides what accepting
	// it means — a designer road writes it into the schema it came from, a reader's road puts it into
	// the composer's user section. This window knows neither, and it is not its business to (Max,
	// 2026-08-24: "I pass the setting, it changes it and gives it back with an OK; then I set it on
	// the composer myself").
	// A reader's road edited the caller's own setting in place — there is nothing to hand back, and
	// Cancel there means the caller drops the copy it took.
	if (m_settings == nullptr && m_desc != nullptr)
		m_desc->GetCompositionSettingsDesc() = m_edited;

	return true;
}

// ===========================================================================
//  The dialog — the panel plus OK / Cancel
// ===========================================================================

// ⭐ ONE CTOR — a description, its configuration, and the setting being edited. Nothing running: see
// the panel. `settings` null means the description's own settings section is what is edited.
ibDialogListSettings::ibDialogListSettings(wxWindow* parent, ibCompositionDescription& desc,
	const ibMetaData* metaData)
	: wxDialog(parent, wxID_ANY, _("List settings"), wxDefaultPosition, wxSize(660, 450),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	m_panel = new ibListSettingsPanel(this, desc, metaData);
	Build();
}

ibDialogListSettings::ibDialogListSettings(wxWindow* parent, const ibCompositionDescription& schema,
	const ibMetaData* metaData, ibSettingsDescription& settings,
	std::vector<ibSettingsPlainField> fields)
	: wxDialog(parent, wxID_ANY, _("List settings"), wxDefaultPosition, wxSize(660, 450),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	m_panel = new ibListSettingsPanel(this, schema, metaData, settings, std::move(fields));
	Build();
}

bool ibDialogListSettings::ShowListSettings(ibCompositionDescription& desc, const ibMetaData* metaData)
{
	wxWindow* top = (wxTheApp != nullptr) ? wxTheApp->GetTopWindow() : nullptr;

	const ibCompositionDescription before = desc;

	ibDialogListSettings dlg(top, desc, metaData);
	if (dlg.ShowModal() != wxID_OK)
		return false;

	return before != desc;   // false = opened and closed on the same snapshot
}

void ibDialogListSettings::Build()
{
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_panel, 1, wxALL | wxEXPAND, FromDIP(6));
	sizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0,
		wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_RIGHT, FromDIP(6));
	SetSizer(sizer);

	Bind(wxEVT_BUTTON, &ibDialogListSettings::OnOk, this, wxID_OK);
}

// The whole transaction lands atomically here; Cancel never reaches it, so the composer stays
// untouched. A panel that refuses keeps the window open on what it objected to.
void ibDialogListSettings::OnOk(wxCommandEvent&)
{
	if (m_panel != nullptr && !m_panel->Commit())
		return;
	EndModal(wxID_OK);
}

// ⭐⭐ THE MODEL'S OWN PAIR — one static door, exactly the composer window's (Max, 2026-08-23: going
// through the CONTROL was a lie, it was never lifted into a static of its own).
//
// The whole sequence, and it is the same one on both sides: take the setting in force, let the
// person change a copy of it, and on OK assign it back. A control asks for this by name; it does not
// carry the settings road inside itself.
bool ibDialogListSettings::ShowUserSettings(wxWindow* parent, const ibProtocolNode& request, ibProtocolNode& response)
{
	wxWindow* top = parent != nullptr ? parent
		: ((wxTheApp != nullptr) ? wxTheApp->GetTopWindow() : nullptr);
	const ibMetaData* metaData = nullptr;   // the server's — the fields come described

	// ⭐⭐ TAKE THE SETTING IN FORCE — a COPY of it, the one the server sent (the model's GetCurrentSettingsDesc there).
	ibSettingsDescription edited;
	{
		ibDataNode settings;
		ibReadProtocolNode(request.FindChild(ibProtocolName::Settings), settings);
		ibSettingsDescriptionMemory::ReadNode(settings, edited);
	}

	// ⭐ AND DESCRIBE WHAT THE WINDOW NEEDS TO KNOW. A dynamic list has a description of its own and
	// its query says what the fields are; anything else — a value table, a tabular section — has
	// columns, so they are stated HERE, as data. Either way what crosses is a description, never the
	// running object (Max, 2026-08-24).
	// 🛑 THE SCHEMA GOES IN CONST, and that is the guarantee — made by the compiler, not by care.
	// Opening the reader's settings cannot change what the configuration ships (Max, 2026-08-24:
	// "if you open the settings, you must guarantee the schema does not mutate"). What the reader
	// edits is `edited`, which is handed back and put on the composer.
	static const ibCompositionDescription kNoSource;   // a model that describes no source of its own
	std::vector<ibSettingsPlainField> fields;

	// 🛑 AND THE COLUMNS ARE DESCRIBED FOR EVERY MODEL, not only for the ones with no description. A
	// list whose source is a METAOBJECT carries no query text, so there is nothing to parse fields
	// out of — its fields are its columns, and leaving them undescribed showed a catalogue's settings
	// window with an empty "Available fields" pane while its filter already had a line in it
	// (Max, 2026-08-24, screenshot). The panel prefers the query when there IS one.
	// …as the server described them: its columns — name, how it reads, id, type, whether the options leave them — and
	// which of their types are references, and to what (the [+] a field gets).
	ibValueReferenceDataObject::ReadReferences(request.FindChild(ibProtocolName::References));
	fields = ibReadSettingsFields(request.FindChild(ibProtocolName::Fields));

	// A setting the server refused comes back with why — said before the window opens on it again.
	const wxString error = request.GetString(ibProtocolName::Error);
	if (!error.IsEmpty())
		wxMessageBox(error, _("List settings"), wxOK | wxICON_WARNING, top);

	ibDialogListSettings dlg(top, kNoSource, metaData, edited, std::move(fields));
	if (dlg.ShowModal() != wxID_OK)
		return false;   // …and Cancel leaves the composer exactly as it was: the copy is dropped

	// …AND ON OK IT IS THE ANSWER — the server makes it the composer's user section and has the model read again; one
	// it refuses is asked again, with why.
	ibDataNode settings;
	ibSettingsDescriptionMemory::WriteNode(settings, edited);
	ibWriteProtocolNode(settings, response.Child(ibProtocolName::Settings));
	return true;
}
