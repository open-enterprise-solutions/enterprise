#include "frontend/win/dlgs/settings/list/listSettings.h"
#include "frontend/win/dlgs/settings/settingsFieldTree.h"     // which fields this list has — one answer
#include "frontend/win/dlgs/settings/settingsFilterEditor.h"   // SHARED with the composer's world
#include "frontend/win/dlgs/settings/settingsSortEditor.h"     // …and so is this one
#include "frontend/win/dlgs/settings/settingsStyle.h"          // art / grid styling, spelled once
#include "frontend/win/dlgs/queryConstructor/queryConstructor.h"   // the Query tab's constructor button
#include "frontend/win/dlgs/callbackDropTarget.h"                  // the same-process drag: the source knows what moved
#include "frontend/win/dlgs/rowValueCell.h"                        // ibRowValueCellRenderer — the shared value cell
#include <wx/stc/stc.h>                                            // the arbitrary query is shown in the styled editor

#include "backend/appData.h"                        // appData->DesignerMode() — the Query tab is a developer tool
#include "backend/metadataConfiguration.h"
#include "backend/objCtor.h"

#include "backend/tabularModel.h"                   // ibValueModel + ibValueModelColumnCollection (the flat field source)
#include "backend/system/value/valueDynamicList.h"  // ibValueDynamicList — the dialog is built on it
#include "backend/srcDataObject.h"                  // ibSourceDataObject — a source describes itself

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
public:
	explicit ibGroupModel(ibListSettingsPanel* dialog) : ibDataViewVirtualListModel(), m_dialog(dialog) {}
	ibValueGroupList* GetGroup() const { return m_dialog->GetGroupList(); }
	void ResetFromList() { ibValueGroupList* g = GetGroup(); Reset(g != nullptr ? (unsigned int)g->Count() : 0u); }
	virtual void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override {
		ibValueGroupList* g = GetGroup();
		if (g == nullptr) return;
		// BOUNDS FIRST. The view paints rows it has, the list may already have fewer
		// (a Reset lands after the paint is queued), and GetKind / GetField index
		// straight into the vector — reading past the end crashed on repaint.
		if (row >= g->Count())
			return;
		if (col == eGroupField)
			variant = g->GetFieldObject(row) != nullptr ? g->GetFieldObject(row)->GetString() : g->GetField(row);
		else if (col == eGroupKind)
			variant = ibValue::CreateEnumObject<ibValueEnumGroupKind>(g->GetKind(row)).GetString();
	}
	virtual bool SetValueByRow(const wxVariant&, unsigned, unsigned) override { return false; }
};

// ===========================================================================
//  Construction, buffers, and the small shared accessors
// ===========================================================================

// The two public ctors differ ONLY in how they bind the field source (m_list set or
// not). Everything else — the tabs, load, OK binding — is identical.

ibListSettingsPanel::ibListSettingsPanel(wxWindow* parent, ibValueDynamicList* list, int pages)
	: wxPanel(parent, wxID_ANY),
	  m_model(list), m_list(list), m_settings(new ibValueListSettings()),
	  m_fields(new ibSettingsFieldTree()), m_pages(pages)
{
	BuildPages();
}

// General-model ctor — same body as the dynamic-list one, but m_list stays null (no
// arbitrary query to edit). Whether the fields come from an explorer or from flat
// columns is BindFieldSource's business, not this one's.
ibListSettingsPanel::ibListSettingsPanel(wxWindow* parent, ibValueModel* model, int pages)
	: wxPanel(parent, wxID_ANY),
	  m_model(model), m_list(nullptr), m_settings(new ibValueListSettings()),
	  m_fields(new ibSettingsFieldTree()), m_pages(pages)
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
	// Tabs are GATED twice: by what the HOST asked for (m_pages) and by the model's Features
	// (Max: "turn the flag off → the tab is hidden; the default parameters still change").
	// Default = all on. The first available tab is the selected one.
	const ibValueModel::Features feats = (m_model != nullptr) ? m_model->GetFeatures() : ibValueModel::Features{};

	// THE QUERY TAB IS A DEVELOPER TOOL, and it exists only where there is a list to edit the
	// query OF. Writing the source query is configuring the form, not using it — an end user
	// opening "Filter" has no business being offered the query text.
	if ((m_pages & Page_Query) != 0 && m_list != nullptr && appData->DesignerMode())
		notebook->AddPage(BuildQueryPage(notebook), _("Query"), true);

	// ⭐ THE TWO SHARED EDITORS. Built, not written: the composer's window builds the very same
	// pair over its own buffer, which is the whole point of them living one level up.
	if ((m_pages & Page_Filter) != 0 && feats.Has(ibValueModel::Features::Filters)) {
		m_filterEditor = new ibFilterEditor(notebook, m_settings, m_fields.get());
		notebook->AddPage(m_filterEditor, _("Filter"), notebook->GetPageCount() == 0);
	}
	if ((m_pages & Page_Sort) != 0 && feats.Has(ibValueModel::Features::Sorting)) {
		m_sortEditor = new ibSortEditor(notebook, m_settings, m_fields.get());
		notebook->AddPage(m_sortEditor, _("Sort"), notebook->GetPageCount() == 0);
	}
	// …and the list's OWN fold, which the composer does not share: its structure is a tree of
	// levels, not a flat ordered list (Max, 2026-08-20).
	if ((m_pages & Page_Group) != 0 && feats.Has(ibValueModel::Features::Grouping))
		notebook->AddPage(BuildGroupPage(notebook), _("Group"), notebook->GetPageCount() == 0);

	mainSizer->Add(notebook, 1, wxEXPAND);
	SetSizer(mainSizer);

	// Transactional open: load the edit BUFFER (m_settings) FROM the composer (the committed
	// store) so it shows the current Filter / Sort / Group; the user edits the buffer; Commit
	// puts it back, and a host that never commits leaves the composer untouched.
	if (m_model != nullptr && m_settings != nullptr)
		// THE LIVE SETTINGS TOO: a tree filter reaches the composer as one expression
		// and cannot be read back out of it — the model still holds the tree itself.
		ibLoadSettingsFromComposer(m_settings, m_model->GetModelComposer(), m_model->GetListSettings());

	LoadFromSettings();
}

ibValueGroupList* ibListSettingsPanel::GetGroupList() const
{
	return m_settings != nullptr ? m_settings->GetGroup() : nullptr;
}

// THE FIELD PICKER, forwarded to the one thing that knows which fields exist.
ibValueCompositionField* ibListSettingsPanel::ChooseField(wxWindow* parent, const wxString& currentPath)
{
	return m_fields != nullptr ? m_fields->ChooseField(parent != nullptr ? parent : this, currentPath) : nullptr;
}

// WHERE THIS PANEL'S FIELDS COME FROM. A dynamic list and a model that IS a source describe
// themselves — walk the explorer, so a reference field unfolds into its target's. Anything else
// has flat columns, and even those get their [+] when the type is a reference.
void ibListSettingsPanel::BindFieldSource()
{
	if (m_fields == nullptr)
		return;

	const ibMetaData* metaData = SourceMetaData();

	const ibSourceDataObject* source = (m_list != nullptr)
		? static_cast<const ibSourceDataObject*>(m_list)
		: dynamic_cast<const ibSourceDataObject*>(m_model);
	if (source != nullptr && source->GetSourceExplorer() != nullptr) {
		m_fields->SetSource(source, metaData);
		return;
	}

	std::vector<ibSettingsPlainField> plain;
	if (m_model != nullptr) {
		if (ibValueModel::ibValueModelColumnCollection* columns = m_model->GetColumnCollection()) {
			for (unsigned int i = 0; i < columns->GetColumnCount(); ++i) {
				const auto* col = columns->GetColumnInfo(i);
				if (col == nullptr)
					continue;
				ibSettingsPlainField field;
				field.m_name = col->GetColumnName();
				field.m_id   = static_cast<ibMetaID>(col->GetColumnID());
				field.m_type = col->GetColumnTypeValue();
				plain.push_back(std::move(field));
			}
		}
	}
	m_fields->SetPlainFields(std::move(plain), metaData);
}

// ===========================================================================
//  Page construction — the tabs this world owns
// ===========================================================================

// The FIRST tab (dynamic-list only) — arbitrary-query source: an enable flag + the query text. When off, the list
// takes its picked metaobject source; when on, this TEXT is the source (composer.FromText). Edits the list's OWN
// UseCustomQuery / CustomQuery properties (serialised), NOT the settings buffer; applied to the list on OK.
wxWindow* ibListSettingsPanel::BuildQueryPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* header = new wxBoxSizer(wxHORIZONTAL);
	m_queryUseCheck = new wxCheckBox(page, wxID_ANY, _("Arbitrary query"));
	header->Add(m_queryUseCheck, 0, wxALIGN_CENTER_VERTICAL);
	header->AddStretchSpacer();

	// THE CONSTRUCTOR OPENS ON THIS VERY TEXT and writes back into it. That is the whole round trip
	// — parse, edit, render — and wiring it here first means it is exercised against a real list
	// from the day it exists, rather than against a mock-up.
	m_queryBuild = new wxButton(page, wxID_ANY, _("Query constructor"));
	m_queryBuild->Enable(false);   // meaningful only when the flag is on, like the text
	m_queryBuild->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if (m_queryText == nullptr)
			return;
		wxString text = m_queryText->GetText();
		// A LIST FOLDS THROUGH ITS OWN SETTINGS — its arbitrary query carries no TOTALS.
		if (!ibShowQueryConstructor(this, text, SourceMetaData(), /*readOnly*/false, ibQueryExclude_Totals))
			return;
		m_queryText->SetText(text);
		ApplyQueryToList();   // a query built in the constructor changes the pickers the same way a typed one does
	});
	header->Add(m_queryBuild, 0, wxALIGN_CENTER_VERTICAL);
	sizer->Add(header, 0, wxEXPAND | wxALL, FromDIP(6));

	// THE SAME STYLED EDITOR as the query constructor's text pane: SQL lexer, the language's own
	// keyword set, the engine's font and colours, line numbers. An arbitrary query is query text
	// wherever it is shown, and showing it as grey characters HERE while it is highlighted THERE is
	// the same language wearing two faces.
	m_queryText = new wxStyledTextCtrl(page, wxID_ANY);
	ibStyleQueryText(m_queryText);
	m_queryText->Enable(false);   // meaningful only when the flag is on
	sizer->Add(m_queryText, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(6));

	// THE ENGINE'S OWN WORDS, under the text it is about. Shown only when there is something to say;
	// there is no second, softer opinion here about what a valid query is.
	m_queryError = new wxStaticText(page, wxID_ANY, wxEmptyString);
	m_queryError->SetForegroundColour(*wxRED);
	m_queryError->Hide();
	sizer->Add(m_queryError, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(6));

	// TICK IT AND A QUERY APPEARS. The list writes the starting query over its own main table
	// (SeedArbitraryQuery) — because switching this on means "let me change what is read", and a blank
	// page is not a starting point, it is a question about syntax. Unticking takes it away again: the
	// main table alone is the read, and a text left lying around would come back on the next tick
	// carrying edits nobody remembers making.
	m_queryUseCheck->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& e) {
		if (m_queryText  != nullptr) m_queryText->Enable(e.IsChecked());
		if (m_queryBuild != nullptr) m_queryBuild->Enable(e.IsChecked());
		if (m_list == nullptr)
			return;
		m_list->SetArbitraryQuery(e.IsChecked());
		if (m_queryText != nullptr)
			m_queryText->SetText(m_list->GetArbitraryQueryText());   // the seed, or empty
		ApplyQueryToList();
	});

	// AND THE PICKERS FOLLOW THE TEXT. On leaving the editor the query is applied and every field
	// tree in this dialog is rebuilt — so a field added to the query is there to filter, sort and
	// group by without closing anything. Losing focus rather than every keystroke: a half-typed query
	// does not resolve, and reporting that on every character would be noise.
	m_queryText->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
		event.Skip();
		ApplyQueryToList();
	});

	page->SetSizer(sizer);
	return page;
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
	if (m_fields != nullptr) m_fields->Populate(m_groupFieldTree);
}

// APPLY THE QUERY AND REBUILD WHAT DEPENDS ON IT. One place, because "the query changed" has exactly
// one consequence: the list reads something else, so the fields on offer are something else.
//
// The ENGINE's verdict is shown as it came. A query that cannot be described cannot be run, and
// finding that out here — in front of its author — is the whole point of describing it early.
void ibListSettingsPanel::ApplyQueryToList()
{
	if (m_list == nullptr || m_queryUseCheck == nullptr)
		return;

	if (m_queryText != nullptr && m_queryUseCheck->GetValue())
		m_list->SetArbitraryQueryText(m_queryText->GetText());
	m_list->ApplySource();

	ReloadFields();   // the fields follow the text, and every tree shows the same answer

	const wxString error = m_list->GetArbitraryQueryError();
	if (m_queryError != nullptr) {
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
				ibValueGroupList* g = GetGroupList();
				return (g != nullptr && idx < g->Count()) ? ibValue(g->GetFieldObject(idx)) : ibValue();
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				ibValueGroupList* g = GetGroupList();
				const size_t idx = GroupIndexAt(row);
				if (g == nullptr || idx >= g->Count())
					return;
				// An empty value CLEARS the line's field (same reason as the sort editor).
				// ⚠ SetLine, not Remove+Add: the second MOVES the edited line to the end, and order is the
				// meaning of a grouping list ("by Warehouse, then by Item" is not the other report).
				ibValueCompositionField* field = nullptr;
				const bool chosen = value.ConvertToValue(field) && field != nullptr;
				g->SetLine(idx, chosen ? field->GetPath() : wxString(), g->GetKind(idx));
				if (m_groupModel != nullptr) m_groupModel->ResetFromList();
			}),
		eGroupField, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT));

	m_groupView->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Kind"),
		new ibRowValueCellRenderer(this, chooser,
			[this](const ibDataViewItem& row) -> ibValue {
				ibValueGroupList* g = GetGroupList();
				const size_t idx = GroupIndexAt(row);
				return (g != nullptr && idx < g->Count())
					? ibValue::CreateEnumObject<ibValueEnumGroupKind>(g->GetKind(idx)) : ibValue();
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				ibValueGroupList* g = GetGroupList();
				const size_t idx = GroupIndexAt(row);
				if (g != nullptr && idx < g->Count()) {
					// ⚠⚠ BY PATH, NOT BY OBJECT — this line CRASHED the designer.
					//
					// A grouping list has two modes, and in FACADE mode (a live list, the composer is
					// the store) there IS no line object: GetFieldObject returns null on purpose, since
					// it used to mint one per repaint and leak it. Reading it here and handing it to
					// Add(field, kind) dereferenced null the moment somebody changed the kind of a
					// grouping on a real list — "added a field, switched it to HierarchyOnly, crash".
					//
					// GetField(idx) is the accessor BOTH modes answer, and a grouping line IS a path
					// plus a kind — so the line is rebuilt from what it is, not from an object that
					// only exists half the time.
					g->SetLine(idx, g->GetField(idx), value.ConvertToEnumValue<ibQueryDimUnfold>());
				}
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

	if (m_fields != nullptr) {
		m_fields->Populate(m_groupFieldTree);
		m_fields->Attach(m_groupFieldTree);   // unfold a reference, drag a field out
	}
	m_groupFieldTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibListSettingsPanel::OnGroupFieldActivated, this);
	rightPane->SetDropTarget(new ibCallbackDropTarget([this] {
		if (m_fields != nullptr) AddGroupForField(m_fields->GetDragItem());
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
	// Query tab (dynamic-list only) — load the list's arbitrary-query flag + text into the controls.
	if (m_list != nullptr && m_queryUseCheck != nullptr) {
		m_queryUseCheck->SetValue(m_list->IsArbitraryQuery());
		if (m_queryText != nullptr) {
			m_queryText->SetText(m_list->GetArbitraryQueryText());
			m_queryText->Enable(m_list->IsArbitraryQuery());
		}
		if (m_queryBuild != nullptr)
			m_queryBuild->Enable(m_list->IsArbitraryQuery());   // the constructor follows the text it edits
	}
	if (m_settings == nullptr)
		return;
	// Every editor binds straight to its buffer list (Filter / Order / Group) — just
	// sync them to what ibLoadSettingsFromComposer put in the buffer.
	if (m_filterEditor != nullptr) m_filterEditor->Reload();
	if (m_sortEditor   != nullptr) m_sortEditor->Reload();
	if (m_groupModel   != nullptr) m_groupModel->ResetFromList();
}

// RE-READ THE SETTINGS THEMSELVES — what the panel edits changed under it.
//
// ⭐ THIS IS WHAT A VARIANT SWITCH NEEDS (2026-08-19). The buffer is loaded once, in the constructor,
// from the model's composer; picking another variant makes the composer hold a different set, and
// the panel has to start over on it. Everything else about the panel is unchanged — same buffer
// object, same models bound to it, so nothing has to be rebuilt or re-bound.
void ibListSettingsPanel::ReloadSettings()
{
	if (m_model == nullptr || m_settings == nullptr)
		return;
	ibLoadSettingsFromComposer(m_settings, m_model->GetModelComposer(), m_model->GetListSettings());
	LoadFromSettings();
}

void ibListSettingsPanel::ApplyToSettings()
{
	// Query tab (dynamic-list only) — the flag and the text are applied AS THEY CHANGE (the field
	// pickers on the other tabs are built from them), so this is the last word rather than the only
	// one: it catches a text edited and then OK'd without the editor ever losing focus.
	ApplyQueryToList();
	// Every editor edits its buffer list IN PLACE through its model — nothing to copy back here.
	// The buffer is committed to the composer on OK (ibCommitSettingsToComposer).
}

// The config metaData that resolves reference targets — the dynamic list's own, else the ACTIVE
// config. Without a valid metaData, ConvertToMetaIds returns nothing and every field looks like a
// leaf (no [+]) — which is exactly the "flat list" bug.
const ibMetaData* ibListSettingsPanel::SourceMetaData() const
{
	if (m_list != nullptr)
		if (const ibMetaData* md = m_list->GetSourceMetaData())
			return md;
	return activeMetaData;
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

// A virtual-list row id is 1-based.
size_t ibListSettingsPanel::GroupIndexAt(const ibDataViewItem& row) const
{
	const size_t id = reinterpret_cast<size_t>(row.GetID());
	return id > 0 ? id - 1 : (size_t)-1;
}

// Add the chosen available field to the grouping list (BUFFER + model refresh).
void ibListSettingsPanel::AddGroupForField(const wxTreeItemId& item)
{
	ibValuePtr<ibValueCompositionField> field(ibSettingsFieldTree::FieldAt(m_groupFieldTree, item));
	ibValueGroupList* g = GetGroupList();
	if (field == nullptr || g == nullptr)
		return;
	g->Add(field, ibQueryDimUnfold::Elements);
	if (m_groupModel != nullptr)
		m_groupModel->ResetFromList();
	ibSelectLastSettingsRow(m_groupView, g->Count());
}

void ibListSettingsPanel::OnGroupAdd(wxCommandEvent&)
{
	ibValueGroupList* g = GetGroupList();
	if (g == nullptr)
		return;
	g->Add(wxEmptyString, ibQueryDimUnfold::Elements);
	if (m_groupModel != nullptr)
		m_groupModel->ResetFromList();
	ibSelectLastSettingsRow(m_groupView, g->Count());
}
void ibListSettingsPanel::OnGroupFieldActivated(wxTreeEvent& e) { AddGroupForField(e.GetItem()); }

void ibListSettingsPanel::OnGroupRemove(wxCommandEvent&)
{
	ibValueGroupList* g = GetGroupList();
	if (g == nullptr || m_groupView == nullptr)
		return;
	const ibDataViewItem& sel = m_groupView->GetSelection();
	if (!sel.IsOk())
		return;
	// The list removes its own line — rebuilding the whole grouping to drop one
	// row lost the field OBJECT each line carried (its type, its presentation) and
	// rebuilt a bare field from the path.
	const size_t index = reinterpret_cast<size_t>(sel.GetID());   // 1-based
	if (index == 0 || !g->Remove(index - 1))
		return;
	if (m_groupModel != nullptr)
		m_groupModel->ResetFromList();
}

void ibListSettingsPanel::MoveGroupLine(int delta)
{
	ibValueGroupList* g = GetGroupList();
	if (g == nullptr || m_groupView == nullptr)
		return;
	const ibDataViewItem& sel = m_groupView->GetSelection();
	if (!sel.IsOk())
		return;
	const size_t index = reinterpret_cast<size_t>(sel.GetID());   // 1-based
	if (index == 0 || !g->Move(index - 1, delta))
		return;
	if (m_groupModel != nullptr)
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
		ibValidateSettings(m_settings);
	}
	catch (const ibBackendException& err) {
		wxMessageBox(err.GetErrorDescription(), _("List settings"), wxOK | wxICON_WARNING, this);
		return false;   // the host stays open — nothing is committed
	}

	if (m_model != nullptr && m_settings != nullptr) {
		// THE TREE GOES BACK TO THE MODEL, not only to the composer. The composer
		// takes the filter as ONE expression, which cannot be read back out of it —
		// so a tree committed only there is applied but invisible: the next open
		// shows an empty Filter tab over a list that is very obviously filtered.
		// The model's settings are where the tree lives and what gets serialised.
		if (ibValueListSettings* live = m_model->GetListSettings())
			live->SetFilterRoot(m_settings->GetFilterRoot());
		ibCommitSettingsToComposer(m_model->GetModelComposer(), m_settings);
		m_model->NotifyReset();

		// 🛑 AND SAY THAT IT CHANGED, which is a different statement from NotifyReset (Max,
		// 2026-08-20). NotifyReset speaks to the VIEWS — "the rows you are showing are stale, fetch
		// again"; this speaks UPWARD, to whoever holds this list — "what I am has changed". In the
		// designer that holder is the form-attribute value, and it marks the form modified, which is
		// why a filter written here used to redraw the list and still be gone on the next open.
		//
		// ⚠ RAISED AT COMMIT HERE, and that is right for THIS host — the list's settings are reached
		// only through a modal OK, so a commit IS somebody's intent. The composer's panel raises it
		// where the EDIT is made instead, because its host is a designer tab where closing is
		// accepting: announcing at commit there would announce a tab that was only looked at.
		// At runtime nothing is above the list and the signal simply stops.
		//
		// Raised on m_list, not on m_model: only a DYNAMIC LIST wears the property face this signal
		// travels on (ibValueDynamicList is an ibPropertyObject; a plain model is not), and only a
		// dynamic list is ever held by something that could care — a form attribute. The panel
		// already keeps the two apart, so this needs no cast to say which case it is.
		if (m_list != nullptr)
			m_list->OnChildChanged();
	}
	return true;
}

// ===========================================================================
//  The dialog — the panel plus OK / Cancel
// ===========================================================================

ibDialogListSettings::ibDialogListSettings(wxWindow* parent, ibValueDynamicList* list)
	: wxDialog(parent, wxID_ANY, _("List settings"), wxDefaultPosition, wxSize(660, 450),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	m_panel = new ibListSettingsPanel(this, list);
	Build();
}

ibDialogListSettings::ibDialogListSettings(wxWindow* parent, ibValueModel* model)
	: wxDialog(parent, wxID_ANY, _("List settings"), wxDefaultPosition, wxSize(660, 450),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	m_panel = new ibListSettingsPanel(this, model);
	Build();
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

// ---------------------------------------------------------------------------
//  Backend hook
// ---------------------------------------------------------------------------

bool ibDialogListSettings::ShowListSettingsDialog(ibValueDynamicList* list)
{
	if (list == nullptr)
		return false;
	wxWindow* top = (wxTheApp != nullptr) ? wxTheApp->GetTopWindow() : nullptr;
	ibDialogListSettings dlg(top, list);
	if (dlg.ShowModal() == wxID_OK) {
		list->RefreshComposerSettings();   // OK -> apply the edits onto the composer (L5)
		return true;
	}
	return false;
}

bool ibDialogListSettings::ShowListSettingsDialog(ibValueModel* model)
{
	if (model == nullptr)
		return false;
	// A dynamic list IS-A ibValueModel: route it through the list overload so it keeps
	// its source-explorer field picker and composer refresh. Any other model uses the
	// flat-column field source; OnOk commits the dialog's buffer onto the model's
	// composer (ibCommitSettingsToComposer + NotifyReset).
	if (ibValueDynamicList* list = dynamic_cast<ibValueDynamicList*>(model))
		return ShowListSettingsDialog(list);

	wxWindow* top = (wxTheApp != nullptr) ? wxTheApp->GetTopWindow() : nullptr;
	ibDialogListSettings dlg(top, model);
	// OK commits the dialog's buffer onto the composer + refreshes (OnOk); Cancel leaves the composer untouched.
	return dlg.ShowModal() == wxID_OK;
}
