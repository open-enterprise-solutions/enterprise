#ifndef _COMPOSER_SETTINGS_H__
#define _COMPOSER_SETTINGS_H__

#include "frontend/frontend.h"

#include <wx/dialog.h>
#include <wx/treectrl.h>
#include <wx/listbox.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>   // wxGetTextFromUser — a hand-made parameter is named when it is added

#include <map>
#include <memory>
#include <vector>

#include "backend/system/value/valueDataComposition.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode — the variants are snapshotted on open

// ---------------------------------------------------------------------------
// ibDialogComposerSettings — the DATA COMPOSER's settings, a window of its own.
//
// WHY NOT THE LIST'S DIALOG. A list is browsed and a composition is composed: the
// list's window leads with the query and treats grouping as one tab among three,
// while a report's window leads with the OUTPUT — the structure the result folds
// into. Two windows, because they are two questions; sharing one and branching
// inside it is how both end up serving neither.
//
// WHAT IS SHARED IS THE MACHINERY, not the layout — and it is exactly two editors:
// the FILTER and the SORT (Max, 2026-08-20: "you may make the filter and the sort
// common — nothing more than that"). They live at the root of settings/, over a
// buffer this window owns, and the same pair is embedded in the list's window. The
// field PICKER a new grouping is chosen through is shared the same way. This window
// adds the editors only a composition has — the output STRUCTURE, the resources, the
// parameters — and the list's window keeps its own query, and its own flat grouping.
//
// ⭐ THE LAYOUT IS THE REFERENCE ONE (Max, 2026-08-19 — a DCS screenshot):
//
//   ┌──────────┬───────────────────────────────────────┐
//   │ Variants │  toolbar                              │
//   │ (toolbar │                                       │
//   │  + view) │  the OUTPUT STRUCTURE — Report -> the │
//   │          │  levels. THE CENTRAL AREA: it takes   │
//   │          │  the room, and no field list lives up │
//   │          │  here                                 │
//   │          ├───────────── splitter ────────────────┤
//   │          │  the settings of the selected node, a │
//   │          │  BAND under the centre: Selected      │
//   │          │  fields | Filter | Sort, with ONE     │
//   │          │  field list, theirs, on the left      │
//   └──────────┴───────────────────────────────────────┘
//
// ⭐ THE STRUCTURE IS THE CENTRE (Max: "the group goes on top, it is the central area"). The window
// is about the OUTPUT, so the tree gets the space and the settings sit under it as a band —
// the inner splitter's gravity is 1.0 and the variants' is 0.0, which sends every pixel a resize
// adds to the structure rather than to the two edges.
//
// 🛑 ONE FIELD LIST ON SCREEN. The structure pane above deliberately has none: a
// field for a new grouping is picked through a DIALOG raised from its toolbar.
// Two field lists in one window (the shape this had on 2026-08-19 before Max saw
// it) read as clutter because they ARE clutter — the same question answered twice,
// side by side.
//
// ⚠ The settings below the structure are the COMPOSITION's for now, not the
// selected node's: the engine still holds one filter, one sort and one set of
// totals for the whole composer. The header over the panel says which node is
// selected AND that the settings are composition-wide, so the window does not
// promise per-node settings the engine cannot keep. When totals move to the node,
// this is where they arrive — the layout already asks the question.
// ---------------------------------------------------------------------------
// ⭐ THE CONTENT IS A PANEL, THE MODAL WINDOW IS ONE OF ITS HOSTS (2026-08-20). A composer declared
// in the metadata is edited on a TAB of its own — the designer opens it like a form or a template
// (docViewComposer) — while a composition held by a form is edited modally from the gridbox. Same
// split the list settings already took: one panel, two hosts, so nothing about what a setting IS
// can differ between them.
//
// What "accept" and "cancel" mean lives on the PANEL (Commit / RestoreOpenState), because both
// hosts have to mean the same thing by them; the buttons that raise them are the host's own.

// ⭐⭐ WHERE A NODE STANDS — ONE COORDINATE, and the type ANSWERS WHAT IT IS. Four kinds of row,
// told apart by which parts are set:
//
//   the report      m_output = -1
//   an output       m_output = i,  m_axis = -1
//   an AXIS         m_output = i,  m_axis = 0|1, m_level = -1    (shown only when there are two)
//   a level         m_output = i,  m_axis = 0|1, m_level = k
//
// ⚠ ASK THE PREDICATES, never the numbers. "No level" is true of an output AND of an axis, and a
// command that tested only `m_level < 0` treated the "Columns" row as the output it hangs under —
// Delete then erased the whole output, rows included (found by audit, 2026-08-22).
//
// It lives here, and not inside the tree model, because the panel keys its per-node buffers by the
// same coordinate: one written twice is two answers to "which node is this" waiting to drift.
struct ibStructurePos
{
	int m_output = -1;
	int m_axis   = -1;   // 0 = rows, 1 = columns
	int m_level  = -1;

	ibStructurePos() = default;
	ibStructurePos(int output, int axis, int level) : m_output(output), m_axis(axis), m_level(level) {}

	bool IsReport() const { return m_output < 0; }
	bool IsOutput() const { return m_output >= 0 && m_axis < 0; }
	bool IsAxis()   const { return m_output >= 0 && m_axis >= 0 && m_level < 0; }
	bool IsLevel()  const { return m_output >= 0 && m_axis >= 0 && m_level >= 0; }

	bool operator<(const ibStructurePos& other) const {
		if (m_output != other.m_output) return m_output < other.m_output;
		if (m_axis   != other.m_axis)   return m_axis   < other.m_axis;
		return m_level < other.m_level;
	}
};

class FRONTEND_API ibComposerSettingsPanel : public wxPanel {
public:

	ibComposerSettingsPanel(wxWindow* parent, ibValueDataComposition* composer);
	~ibComposerSettingsPanel();   // out of line — the field tree is held by forward-declared pointer

	// RE-READ THE SETTINGS from the composition — what this window edits changed under it
	// (another VARIANT was activated). Same buffer object, same editors bound to it.
	void ReloadSettings();
	// RE-READ WHICH FIELDS EXIST — the query was edited on this window's own Query tab.
	void ReloadFields();
	// The field panes alone — re-filled when the query changed OR when another node was selected
	// (what a node may use narrows what they offer).
	void ReloadFieldTrees();

	// ⭐ VIEW ONLY — the tabs open, everything reads, nothing can be changed (Max, 2026-08-20: "in
	// view mode you can only look: no copying, no adding, nothing"). The house call, the one every
	// other designer editor takes (`SetReadOnly(flags == ibDOC_READONLY)` — role, interface, module,
	// visual, help); the panel used to be Enable(false)d whole instead, which greys the text you came
	// to READ and is why this exists at all.
	//
	// It closes BOTH roads to every verb — the toolbars and the context menus — plus cell editing and
	// the query text, and forwards to the shared filter / sort editors, so the list's world gets the
	// same answer from the same code.
	void SetReadOnly(bool readOnly = true);
	bool IsReadOnly() const { return m_readOnly; }

	// Put what is on screen onto the composition. False = the host must stay open (a half-written
	// setting was objected to, or a failing expression was not confirmed).
	bool Commit();
	// Put every variant back as it stood when the panel opened — what Cancel means.
	void RestoreOpenState();

	// THE FIELD PICKER, forwarded to the settings panel below — the one place that knows which
	// fields this composition offers. Public because the structure tree reaches it through a
	// callback (the shared row-value cell takes the picker as one), and because "Add grouping"
	// on its toolbar IS this dialog.
	class ibValueCompositionField* ChooseStructureField(wxWindow* parent, const wxString& held = wxEmptyString);

private:

	// THE BUFFER ONTO THE COMPOSITION — the settings half of "accept", on its own because a variant
	// switch does exactly this much. False = objected to, nothing written.
	bool CommitSettings();
	// Point the field tree at this composition (its own explorer, which is what its query resolved to).
	void BindFieldSource();

	wxWindow* BuildOutputPage(wxWindow* parent);    // variants | structure / settings
	wxWindow* BuildResourcePage(wxWindow* parent);  // WHAT THE LEVELS FOLD — the aggregates
	wxWindow* BuildQueryPage(wxWindow* parent);     // developer only — what is READ
	wxWindow* BuildParameterPage(wxWindow* parent);  // WHAT THE QUERY ASKS FOR — and how each behaves

	// ---- Parameters — the query's own asks, plus hand-made ones ---------------
	void OnParameterAdd(wxCommandEvent&);
	void OnParameterRemove(wxCommandEvent&);
	void OnParameterContextMenu(class ibDataViewEvent&);
	// The "..." behind the expression cell: the same text, with room to write it.
	bool EditParameterExpression(wxString& text);
	// The "..." behind the Type cell: the product.s type picker over this parameter.s declaration.
	bool EditParameterType(wxString& text);
	// ⭐ MAKE THE NAMES AN EXPRESSION MAY CALL EXIST — compile the module manager of the config this
	// composition belongs to, ONCE, as the window opens. Not during a check: compiling rebuilds the
	// module chain, and from inside a cell editor that rebuild destroys the renderer mid-call.
	void PrepareModuleContext();
	// COMPILE-time check of one expression, and of every parameter.s — the window asks before closing.
	static bool CheckExpression(const wxString& expression, wxString& complaint,
	                            const class ibMetaData* metaData);
	wxString CheckAllExpressions();
	void ReloadParameters();
	int  SelectedParameter() const;

	// The two panes of the output page, built separately because the lower one has to
	// exist before the upper one can read the ladder it edits (the panel owns the
	// transactional buffer both of them work on).
	wxWindow* BuildVariantPane(wxWindow* parent);
	wxWindow* BuildStructurePane(wxWindow* parent);
	wxWindow* BuildSettingsPane(wxWindow* parent);

	// One tree of available fields per page that picks one — same contents, filled by the same
	// walk, because "which fields does this composition have" has exactly one answer.
	wxTreeCtrl* CreateFieldTree(wxWindow* parent);
	void PopulateFieldTree(wxTreeCtrl* tree);
	void PopulateFieldTrees();
	// The FIELD behind the cursor — carries its type, which is what the resources page asks the
	// engine about ("which aggregates fit this?").
	const struct ibQueryConstructorField* SelectedField(wxTreeCtrl* tree) const;

	// WHAT THE QUERY OFFERS, read once per rebuild and shown by every tree. Kept because a row
	// carries an INDEX into it: the answer about a selected row comes from the field object.
	std::vector<struct ibQueryConstructorField> m_fields;
	void ReloadResources();


	void OnAddResource(wxCommandEvent&);
	void OnRemoveResource(wxCommandEvent&);
	void OnResourceFieldActivated(wxTreeEvent&);
	void OnResourceExpression(wxCommandEvent&);
	// WHAT THE RESOURCE CELL OFFERS for the row under the cursor — the ready calls the engine admits
	// over that row's field, and the editor behind "..." for everything else.
	wxArrayString ResourceChoices() const;
	bool EditResourceExpression(wxString& text);

	// ---- The output structure — the one editor this window owns ----------------
	// A LEVEL IS A FIELD **AND** HOW IT UNFOLDS, and both are edited as VALUES in the
	// tree's own cells (the shared row-value cell): the field through the panel's picker,
	// the kind through the runtime's quick choice over the GroupKind enumeration.
	// ⭐ OPEN THE GROUPING FORM and add what it made as the innermost level — a grouping of one or
	// more fields, or, with none chosen, the detail records (a node with no group, but a node).
	void OnStructureAdd(wxCommandEvent&);
	// …and the same form over an EXISTING level — what the "…" on its Field cell opens.
	void EditLevelInForm(const class ibDataViewItem& row);
	void OnStructureRemove(wxCommandEvent&);
	void MoveStructureLevel(int delta);        // order IS the nesting
	// Re-read the ladder and put the cursor on `selectLevel` (wxNOT_FOUND = the Report node).
	void ReloadStructure(int selectLevel = wxNOT_FOUND);
	// Which level the cursor stands on, or wxNOT_FOUND when it is on the Report node —
	// so a command aimed at a level does nothing there instead of acting on level zero.
	int  SelectedLevel();
	void UpdateSettingsHeader();

	// ---- Variants — the snapshots, and the one that is active -----------------
	// A VARIANT IS A SNAPSHOT of the settings (its own groupings, filter, sort). Picking one
	// reloads everything to the right of the list; there is always at least one, and the store
	// is what refuses to remove the last.
	void OnVariantAdd(wxCommandEvent&);       // a new, empty variant
	void OnVariantCopy(wxCommandEvent&);      // a copy of the active one, contents and all
	void OnVariantRemove(wxCommandEvent&);
	void ActivateVariant(size_t idx);         // capture what is on screen, then load the other one
	void ReloadVariants(int select = wxNOT_FOUND);
	int  SelectedVariant() const;

	// ---- Context menus — the right hand reaches the same verbs the toolbars do ----
	// The items carry the toolbars' ids and end in the toolbars' handlers, so what is possible
	// where is decided once.
	void OnStructureContextMenu(class ibDataViewEvent&);
	void OnVariantContextMenu(class ibDataViewEvent&);
	void OnResourceContextMenu(class ibDataViewEvent&);

	// VIEW ONLY, the cell half — bound to every grid here, so a cell refuses to open its editor
	// instead of accepting a change nothing will keep.
	void OnStartEditing(class ibDataViewEvent&);
	// The transactional buffer was edited — announce it and remember it. See the definition.
	void MarkSettingsTouched();

	// ⭐ THE TEXT LANDS AS IT IS TYPED. There is no Apply button any more (Max, 2026-08-20: "remove
	// Apply, it means nothing — what you typed is parsed straight away"): a query living both in an
	// editor and in the composition is one fact in two places, and the moment they can disagree is
	// the moment Ctrl+S saves the older one.
	//
	// Storing the text is cheap and happens on every keystroke; working out what it MEANS is not, so
	// it happens when the typing stops (OnIdleApplyQuery).
	void OnQueryTextChanged(class wxStyledTextEvent&);
	void OnIdleApplyQuery(wxIdleEvent&);
	// Re-read the source from the text that is already there, and refresh everything that follows
	// from it — fields, editors, parameters, the error line.
	void RefreshFromQueryText();
	// Force the outstanding re-read of the source. Every way OUT of the Query page goes through it —
	// the page change and accepting the window — so neither acts on a source that has not caught up
	// with the text.
	void ApplyPendingQueryText();
	// Open the query constructor ON THIS TEXT and take back what it renders — the composition's
	// source is the query, so this is where a composition is actually authored.
	void OnBuildQuery(wxCommandEvent&);

	// THE LADDER THE STRUCTURE TREE EDITS — the settings PANEL's transactional buffer, not
	// the composition's live settings. One buffer, one Commit: the panel clears and re-applies
	// Filter / Sort / Group when this window is accepted, so a level written straight onto the
	// live settings would be wiped by that very commit.
	ibValueGroupList* Levels() const;

	ibValueDataComposition* m_composer = nullptr;


	// THE VARIANTS LIVE IN THE COMPOSITION, not here: this pane is a view onto them. Where a variant
	// is chosen is what decides what "reload" means for everything to its right.
	class ibDataViewCtrl*   m_variantView = nullptr;
	class ibVariantModel*   m_variantModel = nullptr;
	class wxToolBar*        m_variantBar = nullptr;   // Add / Copy / Delete — Delete greys on the last one
	// THE OTHER THREE BANDS, held for the same reason the first one is: view-only has to reach them.
	// They were locals until then, which is exactly how a fourth one would go on being missed.
	class wxToolBar*        m_structureBar = nullptr;
	class wxToolBar*        m_resourceBar = nullptr;
	class wxToolBar*        m_parameterBar = nullptr;
	// VIEW ONLY — see SetReadOnly. Held because the context menus ask it: a toolbar can be disabled,
	// a menu has to decide not to appear.
	bool                    m_readOnly = false;
	// The text changed and the SOURCE has not been re-read for it yet — cleared by the idle pass.
	bool                    m_queryDirty = false;
	// SOMETHING IN THE TRANSACTIONAL BUFFER WAS EDITED — the filter, the sort, the structure. What
	// commit does is land that buffer, so with nothing edited there is nothing to land: without this
	// a composer tab opened and closed untouched wrote itself back and announced a change nobody
	// made (found by the final audit, 2026-08-20).
	bool                    m_settingsDirty = false;
	// EVERY VARIANT AS IT STOOD WHEN THIS WINDOW OPENED. Switching variants writes (the composer
	// holds one set of settings at a time), so Cancel restores this rather than nothing.
	ibDataNode              m_openState;

	// THE OUTPUT STRUCTURE, as a tree over the ladder: Report -> level -> level. The nesting IS
	// the order, so the tree shows what the report will actually do rather than a flat list that
	// has to be read as one. A table with rows AND columns becomes nodes here; the ladder stays
	// its degenerate case.
	class ibDataViewCtrl*            m_structureView = nullptr;
	class ibComposerStructureModel*  m_structureModel = nullptr;

	// ⭐⭐ THE PLATFORM'S FILTER AND SORT EDITORS, embedded — not a second, simpler pair written
	// here, and NOT the dynamic list's settings window either. Those are two different worlds and
	// this one is not a case of the other; what they genuinely share is exactly this pair, which is
	// why it lives one level up (Max, 2026-08-20: "you may make the filter and the sort common —
	// nothing more than that").
	class ibFilterEditor* m_filterEditor = nullptr;
	class ibSortEditor*   m_sortEditor   = nullptr;

	// THIS WINDOW'S OWN TRANSACTIONAL BUFFER. Loaded from the composition when the window opens,
	// committed onto it when the window is accepted — the same rule the list's window follows over
	// its own store. It carries the FILTER and the SORT; the structure has a buffer of its own.
	ibValuePtr<ibValueListSettings> m_settings;

	// ⭐ THE STRUCTURE BUFFER — a SNAPSHOT of the composition's outputs, edited here and applied
	// whole on accept. It is the outputs themselves (levels, their fields, their own settings), not
	// a flattened picture of them: a level made of several fields cannot be told from two levels in
	// a flat list, and papering over that with a "same level as the one above" flag would carry the
	// lie straight into a saved variant.
	std::vector<ibDataComposer::Output> m_structure;
	void LoadStructure();    // composition -> buffer, on open and on a variant switch
	void ApplyStructure();   // buffer -> composition, on accept
	// THE LEVEL A TREE ROW POINTS AT, in the buffer — null on the report, an output or an axis, and
	// on a row whose coordinate the buffer no longer has. Every cell editor asks through here, so
	// "which level is this row" is answered once.
	ibDataComposer::GroupNode* LevelAtRow(const class ibDataViewItem& row);

	// ⭐ A NODE HAS ITS OWN PANELS (Max, on the first run: "the groupings have panels of their
	// own"). The shared filter / sort editors are re-pointed at the SELECTED node's buffer instead
	// of at one composition-wide one, which is what they were built to allow (SetSettings).
	//
	// Keyed by the node's coordinate — the SAME type the tree names its rows with (ibStructurePos),
	// not a second triple of numbers beside it. Created on first selection, kept until the window is
	// accepted, then written back into the structure.
	using ibNodeKey = ibStructurePos;

	// ⭐ WHICH NODE IS SELECTED, REMEMBERED — never asked of the tree control.
	//
	// Asking it during its own selection-changed event walks a selection the control is still
	// rebuilding, and it says so: "invalid item in selection - bad internal state" (a crash on the
	// first click after this window learnt about nodes, 2026-08-21). The event carries the item; we
	// keep what it said, and every reader below works from that.
	ibNodeKey m_currentNode = ibNodeKey(-1, -1, -1);
	ibDataComposer::GroupNode* CurrentLevel();
	std::map<ibNodeKey, ibValuePtr<ibValueListSettings>> m_nodeSettings;
	class ibValueListSettings* NodeSettings(const ibNodeKey& key);
	void BindNodeEditors();        // point the shared editors at what is selected now
	void CommitNodeSettings();     // node buffers -> the structure buffer, on accept

	// ---- The GROUPING page — the fields ONE level groups by ---------------------
	wxWindow* BuildGroupingPage(wxWindow* parent);
	void OnGroupingFieldAdd(wxCommandEvent&);
	void OnGroupingFieldRemove(wxCommandEvent&);
	void MoveGroupingField(int delta);
	int  SelectedGroupingField() const;
	void ReloadGrouping(int select = wxNOT_FOUND);
	// Repaint the structure tree's text (a level's elements are shown there) WITHOUT rebuilding it:
	// a rebuild collapses every node, which is not what changing a field should do.
	void RefreshStructureText();

	// Double-click in the picker puts the field into this level's elements.
	void AddGroupingFieldFromTree(const class wxTreeItemId& item);
	// The Grouping page belongs to a GROUPING — taken off the notebook where there is none.
	void SyncGroupingPage();

	class wxNotebook*           m_settingsTabs  = nullptr;
	wxWindow*                   m_groupingPage  = nullptr;
	wxTreeCtrl*                 m_groupingFieldTree = nullptr;   // available fields, the shared tree's view
	class ibDataViewCtrl*       m_groupingView  = nullptr;
	class ibGroupingFieldsModel* m_groupingModel = nullptr;

	// ---- TWO FIELD-SET PAGES, one shape ----------------------------------------
	//
	// AVAILABLE — what the node MAY see; SELECTED — what it SHOWS. Two different questions over
	// identical machinery: a list the user fills himself, a toolbar over it, and an "Auto" switch
	// that says "take the set from the node above". So there is ONE page builder and one set of
	// handlers, told apart by which set they were asked for — a second copy of this would drift the
	// day one of the two grew a button.
	enum class ibFieldSet { Available, Selected };

	struct ibFieldSetPage {
		wxTreeCtrl*              m_sourceTree = nullptr;   // everything the source offers, to pick FROM
		class ibDataViewCtrl*    m_view    = nullptr;
		class ibStringListModel* m_model   = nullptr;
		wxCheckBox*              m_autoBox = nullptr;
	};

	wxWindow* BuildFieldSetPage(wxWindow* parent, ibFieldSet set);
	void OnFieldSetAdd(ibFieldSet set);
	void OnFieldSetRemove(ibFieldSet set);
	void OnFieldSetCopy(ibFieldSet set);
	void OnFieldSetAuto(ibFieldSet set, bool checked);
	void ReloadFieldSets();          // both pages follow the selection
	void ReloadFieldSet(ibFieldSet set);
	// The selected node's OWN set of that kind, and its Auto flag — the report has no flag, being
	// the top of the inheritance.
	std::vector<wxString>* CurrentFieldSet(ibFieldSet set, bool** autoFlag = nullptr);
	int  SelectedFieldSetRow(ibFieldSet set);   // the line the cursor is on, or wxNOT_FOUND
	void MoveFieldSetRow(ibFieldSet set, int delta);
	// Put the field a tree row stands for into the set — double-click on the left pane.
	void AddFieldFromTree(ibFieldSet set, const class wxTreeItemId& item);
	ibFieldSetPage& PageOf(ibFieldSet set) { return set == ibFieldSet::Available ? m_availablePage : m_selectedPage; }

	ibFieldSetPage m_availablePage;
	ibFieldSetPage m_selectedPage;
	// The composition-wide sets, buffered like the structure — applied on accept.
	std::vector<wxString>        m_commonAvailableBuffer;
	std::vector<wxString>        m_commonSelectedBuffer;
	// WHAT THE SELECTED NODE MAY USE — the available set of the current node, inherited upwards
	// (level, then output, then the composition). Read out of THIS window's buffers, because a
	// narrowing just made has not reached the composition yet.
	const std::vector<wxString>* AvailableForCurrentNode() const;
	// WHICH AXIS a structure command acts on, read off what is selected: a level's own axis, the
	// axis itself, an output's rows, or — with the report selected — the first output's rows. `at`
	// comes back as the selected level, or -1 when the selection names no level.
	std::vector<ibDataComposer::GroupNode>* AxisForCommand(int& at);

	// WHICH FIELDS THIS COMPOSITION OFFERS, as the shared TREE both editors above read and the
	// structure pane picks through (settings/settingsFieldTree.h). Distinct from m_fields below,
	// which is the flat list the RESOURCES page reads from the engine — that one carries the
	// aggregate-fitting type, this one unfolds references.
	std::unique_ptr<class ibSettingsFieldTree> m_fieldTree;
	// WHICH NODE the settings below belong to — and, until the engine holds settings per node,
	// that they are the whole composition's.
	wxStaticText* m_settingsHeader = nullptr;

	wxTreeCtrl*   m_resourceFieldTree = nullptr;
	// Resources on the same dataview everything else uses: two columns, field and expression.
	// The parameters page: the composition owns the list, this is the view onto it.
	class ibDataViewCtrl*     m_parameterView = nullptr;
	class ibParameterModel*   m_parameterModel = nullptr;
	class ibDataViewCtrl*  m_resourceView = nullptr;
	class ibResourceModel* m_resourceModel = nullptr;
	int SelectedResourceIndex() const;


	// THE SAME STYLED EDITOR the query constructor and the list settings use: SQL lexer, the
	// language's own keywords, line numbers. Query text is query text wherever it is shown; grey
	// characters here while it is highlighted there is one language wearing two faces.
	class wxStyledTextCtrl* m_queryText = nullptr;
	wxStaticText* m_queryError = nullptr;
};

// The MODAL host — the panel plus OK / Cancel. Kept as the door the gridbox and the property
// editor already call (ShowComposerSettings), so nothing outside had to learn about the split.
class FRONTEND_API ibDialogComposerSettings : public wxDialog {
public:

	// Open the composer's settings modally.
	static bool ShowComposerSettings(ibValueDataComposition* composer);

	ibDialogComposerSettings(wxWindow* parent, ibValueDataComposition* composer);

private:
	ibComposerSettingsPanel* m_panel = nullptr;
};

#endif // _COMPOSER_SETTINGS_H__
