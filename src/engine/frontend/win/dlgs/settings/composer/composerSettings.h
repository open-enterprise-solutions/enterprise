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

#include "backend/query/queryConstructorModel.h"   // ibQueryConstructorField + ibQueryFieldsOfText — the fields a TEXT offers
// (NO valueDataComposition.h. The panel holds no running composition; only the modal host below
//  takes one, as a pointer, and a name is all that needs.)
class ibValueDataComposition;
#include "backend/compositionDescription.h"   // the snapshot Cancel puts back — a DESCRIPTION, not a node
// WHICH SHELF a saved setting sits on — an argument of the shelf's door below, so the same menu
// serves a report and a list. (The storage itself is the backend's; only the word travels here.)
enum class ibSettingsCategory : int;

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
// What "accept" means lives on the PANEL (Commit), because both hosts have to mean the same thing
// by it; the button that raises it is the host's own. CANCEL is the host's alone — it is the copy
// being dropped, and each host drops a different thing.

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

	// ⭐⭐ `edited` — THE DESCRIPTION THIS PANEL EDITS, BY REFERENCE, edited in place. The shape the
	// designer's other editors have (Max, 2026-08-24: "there is your reference — ibSpreadsheetEditView"):
	// the grid editor is handed the metaobject and writes its description directly; there is no live
	// object in between and nothing to copy back.
	//
	// It used to be handed a live ibValueDataComposition and keep a COPY of its description, which
	// the composition then had assigned over it, which the tab then copied into the metaobject —
	// three stores for one fact. Every one of the day's defects was a step of that chain going out of
	// step: a query text put back stale, a variant added into the composition the copy did not know
	// about, resources written where nothing saves them.
	//
	// A host that wants a TRANSACTION (the modal settings window, which offers Cancel) holds the copy
	// ITSELF and hands a reference to it — a copy plus an assignment, decided where Cancel lives.
	//
	// ⭐⭐ AND NOTHING RUNNING COMES IN — the inputs are a DESCRIPTION and a CONFIGURATION, which with
	// the metaobject are the whole of what may reach a composer (Max, 2026-08-24: "the most that can
	// leak in is the metaobject, the metadata and the composer description").
	//
	// ⚠ AND `metaData` IS THE CONTEXT THE SCHEMA RUNS IN — which configuration these names mean. It
	// may be ABSENT, and that is a legitimate state rather than a broken one: with no configuration
	// to ask, nothing can be reached — no tables, no references — and what is left is the primitive
	// types. A smaller window, not a disabled one, which is why nothing here guards on it.
	//
	// 🛑 IT USED TO BE HANDED A LIVE ibValueDataComposition. That is a runtime object with a source
	// binding, a sheet and a fetch in flight, borrowed from whoever happened to have one — so which
	// composition this window was editing depended on the host rather than on what it was given, and
	// the panel could not be opened at all where no such object existed.
	// ⭐⭐ TWO CTORS, ONE PER ROAD — the shape the list's panel already has, and the reason there is no
	// choosing inside: every page here answers one question, *what to do with the CURRENT setting*,
	// and which setting that is belongs to the HOST (Max, 2026-08-24). The panel that picks its own
	// subject is the panel that can pick wrong.
	//
	//   DESIGNER — edits the description's variants. Starts on the zeroth; clicking a variant in the
	//              list unwraps ITS setting and drives it into the pages (ActivateVariant).
	//   READER   — edits the setting it is handed, a COPY of `GetCurrentSettingsDesc()`. No variant
	//              list at all: there is no such thing as a chosen variant at runtime.
	ibComposerSettingsPanel(wxWindow* parent, ibCompositionDescription& edited,
		const class ibMetaData* metaData);
	ibComposerSettingsPanel(wxWindow* parent, ibCompositionDescription& edited,
		const class ibMetaData* metaData, ibSettingsDescription& settings);
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
	//
	// ⚠ IT WRITES THE REPORT — the structure, the resources, the parameters, the query — unless the
	// panel was handed a SETTING to edit, in which case that setting is all it touches.
	bool Commit();
	// (RestoreOpenState DELETED — Cancel is the host dropping the copy it opened on, and the body
	//  was empty. See the note at its old site in the .cpp.)

	// THE FIELD PICKER, forwarded to the settings panel below — the one place that knows which
	// fields this composition offers. Public because the structure tree reaches it through a
	// callback (the shared row-value cell takes the picker as one), and because "Add grouping"
	// on its toolbar IS this dialog.
	class ibValueCompositionField* ChooseStructureField(wxWindow* parent, const wxString& held = wxEmptyString);

	// ⭐ CAN THIS FIELD BE GROUPED BY PERIODS? Asked of the panel because it is the one that read the
	// query and knows what the fields ARE — and asked as the QUESTION rather than by handing out the
	// field list, so no caller re-implements what "a date" means.
	//
	// ⭐ IT IS "CONTAINS A DATE", NOT "IS A DATE" (Max, 2026-08-25). A composite field that may hold
	// a date can be grouped by periods on the rows where it does; refusing it would be stricter than
	// the engine. And a path this window cannot resolve answers YES: unknown means "cannot say no",
	// which is the same rule the query constructor's own gate follows.
	bool StructureFieldIsDated(const wxString& path) const;

protected:

	// ⭐⭐ THE TWO QUESTIONS A HOST ANSWERS, AND THE WHOLE OF WHAT A HOST IS. A composer is opened two
	// ways (Max, 2026-08-24: "two modes — when you have a document, and when you have the metadata"),
	// and the difference between them is not a flag on this panel: it is WHO KNOWS THESE ANSWERS.
	// This class is the metadata mode; ibComposerEditor beside it is the document mode and gets both
	// out of the document it holds, the shape ibGridEditor already has.
	//
	// ⚠ THE CONFIGURATION, NOT THE SOURCE'S. Two different questions live one letter apart:
	//   * this one — the configuration the COMPOSER ITSELF belongs to, which is what an expression is
	//     checked against and what a type is described in;
	//   * GetSourceMetaData() on the composition — the configuration the QUERY resolves names in, set
	//     in RebuildSource from whatever queryable the source turned out to be.
	// They coincide on most roads and are not the same question, so they are not merged here.
	virtual const class ibMetaData* GetEditedMetaData() const;

	// SOMETHING BELOW ME CHANGED. The metadata mode tells the live composition, which bubbles it to
	// its attach owner; the document mode tells the DOCUMENT, because that is what has a dirty bit
	// and a Save behind it (ibGridEditor calls Modify(true) for exactly this reason).
	// ⭐ AND THIS IS THE OTHER HALF OF "no runtime": the panel's own read of the query. It fills
	// m_fieldList and m_queryFault from the text and the configuration, and it is the only place either
	// is written.
	void RefreshQueryFields();
	// The red line under the query text — see the definition: said on OPEN as well as on a change,
	// because a stored query that no longer compiles is exactly the one a person needs told about
	// before they touch anything.
	void ShowQueryFault();

	virtual void MarkModified();

private:

	// The two ctors above differ in exactly two words — which setting, and whose road — so the
	// building itself is written once here.
	void BuildPanel();

	// THE BUFFER ONTO THE COMPOSITION — the whole of "accept": the copy lands in its active variant
	// and is then assigned over the composition. False = objected to, nothing written.
	bool CommitSettings();
	// WHAT IS ON SCREEN INTO THE VARIANT IT WAS WRITTEN IN — over the COPY. Every act that changes
	// which variant is in force does this first: accepting the window, switching, adding, copying.
	void CaptureIntoActiveVariant();
	// THE SETTINGS CHECK, on its own because two moments ask it: accepting the window, and LEAVING a
	// variant (which writes nothing to the composition and still must not carry a broken line away).
	bool ValidateEditedSettings();
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
	std::vector<struct ibQueryConstructorField> m_fieldList;
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
	// ADD AN OUTPUT OF THE OTHER SHAPE — a table, which opens with its two undeletable axes. Not a
	// second way to add a level: it says which SHAPE is being started, and a level is the same thing
	// on either axis of it.
	void OnStructureAddTable(wxCommandEvent&);
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

	// ⭐⭐ NO RUNTIME OBJECT AT ALL. This panel held an ibValueDataComposition until 2026-08-24 and
	// asked it four different sorts of question — three of which the DESCRIPTION already answered
	// (the query text, the variants, the resources) and one which the CONFIGURATION does
	// (ibQueryFieldsOfText: what fields the text offers, and what the parser complained about). What
	// is left is these two members, and neither of them is running.
	//
	// THE CONFIGURATION THIS COMPOSER BELONGS TO — handed in, never reached for. The document mode
	// overrides GetEditedMetaData and this stays null there, which is why every reader goes through
	// the accessor rather than touching it.
	const class ibMetaData* m_metaData = nullptr;

	// WHAT THE PARSER SAID about the query text as it stands, taken at the same moment the fields
	// were. Half-typed text offers no fields YET and is not an error to shout about; this is what
	// the Query tab's line shows when there IS something to say.
	wxString m_queryFault;
	// ⭐⭐ WHOSE ROAD THIS IS — decided by the host at construction and never again: a window cannot
	// become somebody else's halfway through. It records ONE fact — was a setting handed in — and it
	// is asked for the things that differ between a reader and a designer: the variants pane, the
	// inaccessible filter lines, what accepting means.
	//
	// 🛑 THAT FACT USED TO BE READ OFF `m_settings == nullptr`, which is why the pointer could not
	// also be used to say WHICH setting is being edited. One member answering two questions is how
	// the designer ended up unable to edit any variant but the zeroth.
	const bool m_readerRoad;

	// …AND WHAT IS BEING EDITED, always valid. A reader's own setting, or — in the designer — the
	// variant the list has selected, re-pointed as it moves (ActivateVariant).
	ibSettingsDescription* m_settings = nullptr;

	// ⭐⭐ WHAT THE FILTER / SORT / GROUPING PAGES ARE POINTED AT — the handed-in setting when there is
	// one, the report's own otherwise.
	//
	// A reader's window is a COPY, all the way through: the host takes what is in force (their own if
	// they set one, the author's if they did not), hands it here, the pages edit THAT, and on OK it
	// becomes the composer's user setting (Max, 2026-08-24). The report is never touched on that road.
	//
	// 🛑 THE PAGES USED TO BE NAILED TO `m_edited.GetCompositionSettingsDesc()` REGARDLESS. So a
	// reader opened the window on the AUTHOR's section, typed into it — scribbling over what the
	// configuration ships — and the copy they were supposed to be editing sat untouched beside it.
	// ⭐⭐ THE SETTING THIS PANEL EDITS — the one it was HANDED, and the ZEROTH otherwise. No index
	// enters the question (Max, 2026-08-24: *"you get some cursor there — it should always be zero;
	// you just take the first element, the zeroth"*).
	//
	// 🛑 IT ASKED A CURSOR, and a cursor is a second answer to "which setting am I editing" standing
	// beside the first. That is how the filter editor came to show one variant's lines while the
	// header named another.
	ibSettingsDescription& EditedSettings() { return *m_settings; }
	// …and the READING one, so a const caller needs no cast to ask the same question. (A `const_cast`
	// stood here for exactly as long as it took to be seen: it was written to make a const method
	// compile, which is never a reason for one.)
	const ibSettingsDescription& EditedSettings() const { return *m_settings; }

	// (NO VARIANT CURSOR. It stood here as "which variant this window is editing", and it was a
	//  second answer to a question `EditedSettings()` already answers: the setting handed in, or the
	//  zeroth. A reader is handed a COPY of what composes and edits that — the variants themselves
	//  are const on the runtime road and are only ever copied out of.)


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
	// ⭐⭐ THE DESCRIPTION THIS WINDOW EDITS — BY REFERENCE, in place. Everything typed here lands in
	// it: the query, the settings, the variants, the structure, the resources, the parameters.
	//
	// WHOSE description it is, is the HOST's decision and this panel never learns it. The designer's
	// tab hands over the metaobject's own (the shape ibSpreadsheetEditView has, where the grid editor
	// writes the metaobject's description directly); a modal window that offers Cancel hands over a
	// copy IT holds and assigns that copy on OK. A transaction is a copy plus an assignment, and it
	// is decided where Cancel lives.
	ibCompositionDescription& m_edited;

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

	// (No live settings object. The window's transactional buffer IS m_edited below — the copy of
	//  the composition's description — and each editor is handed the part of it it edits.)

	// ⭐⭐ THE STRUCTURE — THE ACTIVE VARIANT'S OWN, by reference. The outputs themselves (their nodes,
	// what each folds by, each node's own settings and what unfolds under it), not a flattened
	// picture: a node of several fields cannot be told from two nodes in a flat list.
	//
	// 🛑 IT WAS A COPY, loaded on open and on every variant switch and assigned back on accept — the
	// same three chores the per-node settings map was deleted for, and they fell out of step the same
	// way: a level added in the window went missing between OK and the next open, and the hunt for it
	// ran through three layers before the journal said the structure had never left the buffer
	// (2026-08-24). The window edits the description in place; this is the last piece that did not.
	//
	// ⚠ DESCRIPTIONS, not the composer's outputs. The driver an output carries is a live object and
	// has no business in a window.
	// ⭐⭐ THE OUTPUTS OF THE SETTING BEING EDITED — asked through `EditedSettings()`, which is the
	// handed-in setting on a READER's road and the cursor's VARIANT on the designer's.
	//
	// 🛑 IT WENT STRAIGHT TO A VARIANT REGARDLESS, and that is a reader editing a variant — which
	// they may not do at all (Max, 2026-08-24: *"you can only edit a variant in the designer. You go
	// into the composer in the designer, you go into each variant and set its settings, and they
	// accumulate in that list. You cannot change those variants' settings from the runtime"*). A
	// reader edits THEIR setting; the outputs are part of it, so they come along by themselves.
	std::vector<ibOutputDescription>& Structure() { return EditedSettings().m_structure; }
	const std::vector<ibOutputDescription>& Structure() const { return EditedSettings().m_structure; }
	// THE LEVEL A TREE ROW POINTS AT, in the buffer — null on the report, an output or an axis, and
	// on a row whose coordinate the buffer no longer has. Every cell editor asks through here, so
	// "which level is this row" is answered once.
	ibLevelDescription* LevelAtRow(const class ibDataViewItem& row);

	// ⭐ A NODE HAS ITS OWN PANELS (Max, on the first run: "the groupings have panels of their
	// own"). The shared filter / sort editors are re-pointed at the SELECTED node's buffer instead
	// of at one composition-wide one, which is what they were built to allow (SetFilter / SetSort).
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
	ibLevelDescription* CurrentLevel();
	// (NO PER-NODE BUFFER. A node's settings are ON the node — ibLevelDescription::m_settings, the
	//  same whole the composition has — so the editors are pointed straight at the selected node's
	//  parts and a level's filter lands as it is typed. A std::map<node, settings> used to stand
	//  beside the structure, and it cost three chores that could each fall out of step: copy in on
	//  first selection, write back at commit, and re-key every entry whenever a level was added,
	//  removed or moved.)
	void BindNodeEditors();        // point the shared editors at what is selected now

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
	// ⭐ ONE SET, AND IT IS "SELECTED" — the fields this node shows, added to what its output and the
	// composition already show (ibDataComposer::SelectedFor).
	//
	// 🛑 THERE WAS A SECOND PAGE, "AVAILABLE": what a node MAY see, over identical machinery — its own
	// list, its own toolbar, its own "Auto" switch — which is why every verb below used to carry an
	// `ibFieldSet` telling the two apart. It answered a question nobody asked (Max, 2026-08-24:
	// "available tells us nothing, it is the same thing understood in a harder way"), and it had no
	// reader on the run path at all. Removing it removed the parameter with it.
	struct ibFieldSetPage {
		wxTreeCtrl*              m_sourceTree = nullptr;   // everything the source offers, to pick FROM
		class ibDataViewCtrl*    m_view    = nullptr;
		class ibStringListModel* m_model   = nullptr;
	};

	wxWindow* BuildFieldSetPage(wxWindow* parent);
	void OnFieldSetAdd();
	void OnFieldSetRemove();
	void OnFieldSetCopy();
	void ReloadFieldSets();          // the page follows the selection
	// The selected node's OWN set — what IT adds. The report's is the bottom of the pile.
	std::vector<wxString>* CurrentFieldSet();
	int  SelectedFieldSetRow();   // the line the cursor is on, or wxNOT_FOUND
	void MoveFieldSetRow(int delta);
	// Put the field a tree row stands for into the set — double-click on the left pane.
	void AddFieldFromTree(const class wxTreeItemId& item);

	ibFieldSetPage m_selectedPage;
	// The composition-wide set, buffered like the structure — applied on accept.
	// (⛔ `m_commonSelectedBuffer` STOOD HERE — a copy of `m_edited.m_selected`, loaded on open and
	//  written back on accept. The window edits the description in place; a buffer over one field of
	//  it was the last of the three copies this panel kept.)
	// WHICH AXIS a structure command acts on, read off what is selected: a level's own axis, the
	// axis itself, an output's rows, or — with the report selected — the first output's rows. `at`
	// comes back as the selected level, or -1 when the selection names no level.
	std::vector<ibLevelDescription>* AxisForCommand(int& at);

	// WHICH FIELDS THIS COMPOSITION OFFERS, as the shared TREE both editors above read and the
	// structure pane picks through (settings/settingsFieldTree.h). Distinct from m_fieldList below,
	// which is the flat list the RESOURCES page reads from the engine — that one carries the
	// aggregate-fitting type, this one unfolds references.
	std::unique_ptr<class ibSettingsFieldTree> m_fieldSource;
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

// ------------------------------------------------------------------------------------------------
// ⭐⭐ THE COMPOSER EDITOR — the DOCUMENT mode of the panel above (Max, 2026-08-24: "two modes of
// opening a composer: when you have a document, and when you have the metadata").
//
// It is the same content: what a composer declares is one thing, and a setting cannot mean one
// thing on a designer tab and another on a form. What the document adds is WHO IS ASKED — the
// metaobject and its configuration come out of the document rather than out of a live composition,
// and "something changed" goes to the document, which is the thing that has a dirty bit and a Save
// behind it.
//
// The shape is ibGridEditor's, deliberately: that editor holds an `ibMetaDocument*` and reaches its
// metaobject through ConvertMetaObjectToType, marking the document modified when it writes. This is
// the same editor for the other kind of document, so it is built the same way.
//
// ⚠ THE DOCUMENT IS BORROWED. It outlives this panel — the view is destroyed with the tab, and the
// document is what the tab was opened ON — so nothing here owns it and a null one is a panel that
// simply answers nothing, exactly as the metadata mode does without a composition.
// ------------------------------------------------------------------------------------------------
class FRONTEND_API ibComposerEditor : public ibComposerSettingsPanel {
public:

	// ⭐ THE DOCUMENT IS THE ONLY INPUT. It answers both of the others — which description is being
	// edited and which configuration that description means — so the view that opens this tab hands
	// over the document and nothing else, exactly as ibSpreadsheetEditView hands ibGridEditor one.
	// THE DESIGNER'S TAB — always the author's road: it edits the composer metaobject's own
	// description, so there is no setting to hand in and no reader to hand one.
	ibComposerEditor(wxWindow* parent, class ibMetaDocument* document);

	class ibMetaDocument* GetDocument() const { return m_document; }

protected:

	// THE CONFIGURATION IS THE DOCUMENT'S — reached through its metaobject, which is what the tab was
	// opened on. NEVER the active one: two configurations are open at once in the designer and the
	// document knows which of them is its own (the same reason docViewComposer builds its composition
	// over `metaComposer->GetMetaData()`).
	virtual const class ibMetaData* GetEditedMetaData() const override;

	// AND THE DIRTY BIT IS THE DOCUMENT'S. On the metadata road the signal bubbles up an attach chain
	// and dies if nobody is above it; here there is somebody above it by construction.
	virtual void MarkModified() override;

private:

	class ibMetaDocument* m_document = nullptr;
};

// The MODAL host — the panel plus OK / Cancel. Kept as the door the gridbox and the property
// editor already call (ShowComposerSettings), so nothing outside had to learn about the split.
class FRONTEND_API ibDialogComposerSettings : public wxDialog {
public:

	// ⭐⭐ THE USER'S SETTINGS OF A MODEL — the GRIDBOX's road, and the LIST's own window word for
	// word (Max, 2026-08-23: "do it by analogy — you pass the model into the settings; passing the
	// model is fine").
	//
	// The whole sequence goes through the model's COMPOSER: take the setting that is in force
	// (`GetCurrentSettingsDesc`), let the person change a copy of it, and on OK assign it back
	// (`SetUserSettingsDesc`). There is no such pair on the model itself, on purpose. The report
	// itself is never written, and the caller never casts its way down to what kind of model it is
	// holding — a gridbox shows a sheet, and a sheet's model answers both questions.
	// ⭐⭐ THE READER'S ROAD — a COPY of the setting in force goes into the window, and on OK it is set
	// back on the model. Nothing is kept anywhere else: the active setting lives in the model's
	// composer, which the schema does not serialise, and what the author laid down is untouched
	// (Max, 2026-08-24).
	static bool ShowUserSettings(wxWindow* parent, class ibValueSpreadsheetModel* model);

	// ⭐⭐ THE VARIANT PICKER — a menu of what the author named, and picking one IS setting a setting
	// (Max, 2026-08-26: "you press it on a list or a report, the variants drop down, and you just
	// pick — it is the same as if you had set a user setting").
	//
	// It needs no mechanism of its own, and that is the whole point: a variant is a WRAPPER over a
	// setting, so this call is `SetUserSettingsDesc(variants[n].m_settings)` and nothing else. There
	// is no stored "active variant" to keep in step — at runtime there is no chosen variant at all,
	// only the setting that composes.
	//
	// ⚠ THE MODEL GOES IN, and the variants are reached through ITS composer (Max, 2026-08-26) — the
	// same shape `ShowUserSettings` has, so a caller hands over what it is holding and never reaches
	// into it.
	//
	// ⭐ A REPORT'S MODEL, and only that. A variant is a setting the AUTHOR named and left in the
	// configuration, and they are edited in THIS window — the composition's. A list has no such
	// door and no such thing: its setting is whatever the reader narrowed to, and there is one of
	// it (Max, 2026-08-26: *"for a report it is needed, truly"*).
	//
	// Returns true when something was picked; the CALLER decides what to do about it — a report
	// shows the sheet that was built and is re-formed when the person says so.
	static bool ShowVariantPicker(wxWindow* parent, class ibValueSpreadsheetModel* model);

	// ⭐⭐ THE READER'S OWN SHELF — the settings THIS person saved, kept in the base. One menu: what
	// they have saved (picking one restores it), and below it the three verbs that change the shelf
	// itself — save what is in force under a name, rename one, drop one.
	//
	// ⭐⭐ AND A LIST GOES THROUGH THE VERY SAME DOOR (Max, 2026-08-26: *"a list can have saved
	// settings too"*) — which is why this takes a COMPOSER and an ADDRESS rather than a report's
	// model. The two differ only in where their rows live: a report's under the composer the
	// configuration declares, a list's under the control on the form, and the CATEGORY is what keeps
	// the two vocabularies from ever meeting. Everything else — the menu, the verbs, the restore —
	// is one piece of code.
	//
	// Restoring ends in SetUserSettingsDesc, so from the composer down there is no difference
	// between a setting that came from a variant, from this shelf, or from the settings window.
	//
	// Returns true when something changed (restored, saved, renamed, removed).
	static bool ShowSavedSettings(wxWindow* parent, class ibDataComposer& composer,
	                              ibSettingsCategory category, const ibGuid& objectKey,
	                              const class ibMetaData* metaData);

	// …and RESTORE, the other half: which of the saved ones to put on. A window, because the shelf
	// is kept here too — the default entry in bold, and rename / delete / "restore on open" acting
	// on whichever is selected.
	static bool ShowRestoreSettings(wxWindow* parent, class ibDataComposer& composer,
	                                ibSettingsCategory category, const ibGuid& objectKey,
	                                const class ibMetaData* metaData);

	// (⛔ NO MODEL-SHAPED TWINS. They existed to cast a model down to a composition for its address —
	//  and the address belongs to the CONTROL, which already has both halves in hand: the composer
	//  off the base model, its own guid for the key. Max, 2026-08-26: *"why are you casting?"*)

private:
	// The picker itself — over the composer, which is all it needs.
	static bool PickVariant(wxWindow* parent, class ibDataComposer& composer);
public:

	// ⭐⭐ ONE DOOR: A PARENT, A SNAPSHOT, AND THE CONFIGURATION ITS NAMES MEAN. Nothing running goes
	// in, and there is no second road — both hosts hand over a description and differ only in whose
	// it is (Max, 2026-08-24: "we work with snapshots").
	//
	//   * a property cell hands a CLONE of its value, and sets that clone back on true;
	//   * the gridbox hands a COPY of the model's description, and keeps from it what its own pair
	//     writes back — the user setting, which travels inside the snapshot.
	//
	// True = OK and the snapshot differs; the window edits it IN PLACE, so what Cancel drops is
	// decided by whoever owns the description.
	static bool ShowComposerSettings(wxWindow* parent, ibCompositionDescription& desc,
		const class ibMetaData* metaData);

	// ⭐⭐ …AND THE AUTHOR'S ROAD, for a composition EMBEDDED ON A FORM. A form attribute typed
	// DataComposition is a small report somebody is building right there — pick a source, write the
	// query, declare the resources — and none of that is a "setting of this run": it is what the
	// composition IS, and it travels with the FORM (ibPropertyDataComposition already reads and
	// writes the description, so it has been persisted all along).
	//
	// 🛑 THE PROPERTY INSPECTOR USED TO CALL ShowUserSettings HERE, and that window shows the Query,
	// Resources and Parameters tabs but writes back ONLY the setting — so everything typed on them
	// was edited, accepted and silently dropped (Max, 2026-08-24: "it has to be able to write itself
	// too"). Two roads to one window, and the difference is WHAT IS WRITTEN.

	// ⭐⭐ ONE CONSTRUCTOR, AND NOTHING RUNNING IN IT — a DESCRIPTION and the configuration its names
	// mean, edited in place. Both roads reach this: the author's hands over a clone of a property
	// value, the user's hands over the description of the model it was opened on. A model is a thing
	// `ShowUserSettings` deals with, not this window.
	//
	// ⭐ THE MODAL HOST IS THE READER'S, always: `settings` is what it edits — a COPY of
	// `GetCurrentSettingsDesc()` the caller took — and that setting is all that is written back. The
	// report itself is left exactly as its author wrote it, and the variants are not shown at all.
	ibDialogComposerSettings(wxWindow* parent, ibCompositionDescription& desc,
		const class ibMetaData* metaData, ibSettingsDescription& settings);
	// …and the AUTHOR's, over a description edited in place — what `ShowComposerSettings` opens when
	// a composer is configured from the designer rather than read from.
	ibDialogComposerSettings(wxWindow* parent, ibCompositionDescription& desc,
		const class ibMetaData* metaData);

private:
	// (NO COPY HELD HERE ANY MORE. The window edits the description it was handed, in place — the
	//  CALLER decides whether that is a clone, and therefore what Cancel drops. A copy of its own
	//  would have been a third store beside the caller's and the object's.)
	void BuildAround();   // the frame around whichever panel a ctor built
	ibComposerSettingsPanel* m_panel = nullptr;
};

#endif // _COMPOSER_SETTINGS_H__
