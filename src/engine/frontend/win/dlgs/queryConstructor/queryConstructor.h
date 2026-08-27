#ifndef __QUERY_CONSTRUCTOR_DLG_H__
#define __QUERY_CONSTRUCTOR_DLG_H__

////////////////////////////////////////////////////////////////////////////
// The QUERY CONSTRUCTOR — the shell over an AST we already have.
////////////////////////////////////////////////////////////////////////////
//
// Not a code generator. A PROJECTION OF ONE AST ONTO TABS, in both directions:
//
//     text ──parse──▶ package ──fill──▶ tabs
//                        ▲               │ user edits
//                        └───collect─────┘
//     package ──render──▶ text ──▶ back where it came from
//
// Everything left and right of the tabs was already built — queryParser (text -> AST) and
// queryRender (AST -> text), both round-trip tested. This is the middle: a window that SHOWS an
// ibQueryPackage and takes edits.
//
// THE ONE RULE THAT DECIDES WHETHER THIS IS A TOOL OR A TOY: what the constructor produces is
// checked by the ENGINE, never by a checker of its own. Press OK (or leave the text pane) and the
// text goes through ibQueryParser::ParsePackage — the same parser the runtime uses — and whatever
// it throws is what the user is shown, verbatim. There is no second, softer opinion about what a
// valid query is, so a query the constructor accepts is a query that runs.
//
// The corollary is the other half of the round trip: a query edited BY HAND must still come back
// in. The text pane below the tabs is editable, and on losing focus it is re-parsed and the tabs
// are rebuilt from it. The moment hand-edited text stops being loadable, a constructor is in the
// way; this one cannot reach that state, because reading is the same code path as writing.
//
// RE-ENTRANT. A nested table (a subquery standing where a table would) opens THIS SAME window one
// level down over the inner select — which is why the dialog is built around a sub-AST rather than
// around "the query", and why the nested case needs no separate editor.
//
// See docs/query-constructor.md.
//
////////////////////////////////////////////////////////////////////////////

#include "frontend/frontend.h"

#include <wx/dialog.h>
#include <wx/notebook.h>
#include <wx/treectrl.h>
// (No wx/listctrl.h or wx/listbox.h: every list in this window is an ibDataViewCtrl now.)
#include <wx/checkbox.h>
#include <wx/spinctrl.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/choice.h>
#include <wx/radiobox.h>

#include <functional>
#include <map>
#include <vector>

#include "backend/query/queryAst.h"
#include "backend/query/queryConstructorModel.h"

// The links grid is an ibDataViewCtrl with its own model — cells edited IN PLACE.
#include "frontend/win/ctrls/dataview/dataview.h"
#include "queryGridModel.h"   // ibTotalsRow — which NODE a row of the totals grid belongs to

class ibMetaData;

// ---------------------------------------------------------------------------
// The dialog. Two ways in, both below the class:
//   * ibShowQueryConstructor(parent, text)   — over query TEXT (a package): the dynamic list's
//     settings and the code editor's context menu both use this.
//   * ibShowQueryConstructorFor(parent, sel) — over ONE select, in place: how a nested table and
//     a union branch are edited (the re-entrant path).
// ---------------------------------------------------------------------------

// ⭐ WHAT AN OPENING LEAVES OUT — a mask of EXCLUSIONS, and the default is zero: everything shows
// (Max, 2026-08-19: "by default null means we see it all; the flag says what to exclude — totals,
// sorting, and so on — so you never add another boolean").
//
// A host excludes what belongs to IT rather than to the query text. A composition's totals ARE its
// resources and its levels ARE its groupings; a dynamic list folds through its own settings. Both
// therefore open the constructor with Totals excluded.
//
// ⚠ EXCLUDED IS NOT MERELY HIDDEN: a clause whose tab is not on offer is DROPPED from the query on
// the way in, because what no tab can show, nobody can remove either.
enum ibQueryConstructorExclude {
	ibQueryExclude_None       = 0,
	ibQueryExclude_Totals     = 1 << 0,
	ibQueryExclude_Order      = 1 << 1,
	ibQueryExclude_Unions     = 1 << 2,
	ibQueryExclude_Advanced   = 1 << 3,
	ibQueryExclude_Index      = 1 << 4,
	ibQueryExclude_Batch      = 1 << 5,
	ibQueryExclude_Links      = 1 << 6,
	ibQueryExclude_Grouping   = 1 << 7,
	ibQueryExclude_Conditions = 1 << 8,
	// The first tab. Excluded by nobody today — it is what a query IS — but it carries a bit like
	// every other, so a host that one day opens the window on a fixed source does not need a new one.
	ibQueryExclude_Tables     = 1 << 9,
};

class FRONTEND_API ibDialogQueryConstructor : public wxDialog
{
public:
	// `metaData` — the config the query is written against, HANDED IN. The dialog never reaches for
	// the active configuration itself: which config a query belongs to is the host's knowledge (a
	// list has its source's, a module editor has its document's), and a window that went looking
	// would sooner or later find a different one than the caller meant.
	//
	// `readOnly` — open it to be READ. Every verb is off and the window says so at the top; what is
	// shown can be studied and copied, and nothing that happens in it is written back. That is what
	// a query belonging to something the user may not change looks like — visible, not hidden.
	//
	// ⚠ The parameter only ever ADDS the restriction. Read-only is ASKED OF THE METADATA first
	// (IsMetaDataReadOnly below) — a configuration opened without the right to change it already
	// says so, and a window that could be told "no, it is editable" would be a way around that.
	ibDialogQueryConstructor(wxWindow* parent, const ibQueryPackage& package,
	                         const ibMetaData* metaData, bool readOnly = false,
	                         int exclude = ibQueryExclude_None);

	// Does this configuration allow its structure to be changed? The answer lives on the metadata's
	// own tree (ibBackendMetadataTree::IsEditable) — the same question the designer asks before it
	// lets anything be edited, so the constructor inherits the answer instead of inventing one.
	// No tree at all = a runtime host with no designer surface, which does not make a query
	// unreadable — so: editable.
	static bool IsMetaDataReadOnly(const ibMetaData* metaData);

	// The edited package, rendered. Read after ShowModal() == wxID_OK.
	const ibQueryPackage& GetPackage() const { return m_package; }
	wxString GetText() const;

	// EDITING A SUB-QUERY, not a statement. A nested table and a union branch are queries standing
	// INSIDE another one, so the words that belong to a statement — INTO (which materialises a
	// package's temp table) and FOR UPDATE (which holds a statement's rows) — are not theirs to
	// write, and the package strip has nothing to show. The parser refuses both inside a source, so
	// leaving the controls on would let somebody build text their own engine then rejects.
	void SetSubQueryMode();

private:
	// ---- construction -------------------------------------------------
	// (AddButton removed — every verb in this window lives on a TOOLBAR now, and the last plain
	//  command button went with the move to tool bands.)
	// A MOVE button — the narrow glyph column that sits between two panes.
	class wxButton* AddMoveButton(wxWindow* parent, class wxSizer* sizer, const wxString& glyph,
	                              const wxString& tip, std::function<void(wxCommandEvent&)> handler);

	// THE VERBS OF A PANE LIVE ON A TOOLBAR ABOVE IT — the shape the rest of the settings windows
	// use (listSettings' filter tab), and the reason is not taste: a row of text buttons under a
	// list grows with every verb until it wraps, while a 16×16 tool band stays the same height and
	// reads as "things I can do to this list" rather than as part of the dialog's own commands.
	class wxToolBar* MakeToolBar(wxWindow* parent);
	void AddTool(class wxToolBar* bar, const wxString& label, const wxString& artId,
	             std::function<void(wxCommandEvent&)> handler);
	// THE RIGHT-CLICK ON A PANE OFFERS WHAT ITS TOOLBAR OFFERS — replayed from the toolbar's own
	// record of what was added to it, never from a second list. Call it after the bar is filled.
	void AttachContextMenu(wxWindow* target, class wxToolBar* bar);

	// One tab, and whether it describes a QUERY. A statement that drops a temp table has no query,
	// so those tabs are removed for it — a page window outlives the notebook's idea of which pages
	// are showing, which is what makes removing and re-adding them cheap and lossless.
	struct Page
	{
		wxWindow* m_window = nullptr;
		wxString  m_title;
		bool      m_queryPage = false;
		// A tab that has nothing to say with one table. A join is a relation BETWEEN two sources,
		// so with one there is not an empty list to look at — there is no such thing yet.
		bool      m_needsTwoTables = false;
		// A tab that exists only for a statement that MAKES a temp table: only such a table can be
		// indexed, so for anything else the tab would be a control over nothing.
		bool      m_needsTempTable = false;
		// …and its opposite: a tab that CANNOT apply to a statement that makes a temp table. Totals
		// is the one — a temp table is a flat table and TOTALS yields a tree, which the parser
		// refuses outright. The tab goes rather than being left to write text the engine rejects.
		bool      m_refusedByTempTable = false;
		// WHICH EXCLUSION BIT TURNS THIS TAB OFF (0 = a tab no host may exclude). Two different rules
		// remove the Totals tab now: a temp table cannot carry a tree, and a HOST may fold elsewhere
		// entirely (a composition folds through its resources). Asking by the tab.s TITLE would be
		// asking in the language of the caption.
		int       m_excludeBit = 0;
		// A tab that exists only when the PACKAGE has named results to work with (`ONTO`). Linking
		// two selections is a relation between two of them, so with fewer than two there is nothing
		// to show — the same rule the Links tab follows for tables, one tier up.
		// LAST on purpose: every existing row of the table below stops at the exclusion bit, and a
		// flag slotted in the middle would silently re-read that bit as this one.
		bool      m_needsNamedResults = false;
	};
	std::vector<Page> m_pages;
	void SyncNotebookPages();   // the tab set follows what the current statement IS

	// ---- Selection links — joining the package's NAMED results (query result links) -----
	// Shown when the package has two or more of them. ⭐ IT EDITS THE PACKAGE'S OWN LIST — no
	// statement is touched, nothing is added to anybody's FROM, and no temp table is created: a link
	// is a row saying "these two results are related, by this condition", and the package's final
	// query is assembled FROM the links at execution time (Max: "the fields we already see from the
	// selection; just run the final query through the links").
	wxWindow* BuildSelectionLinksPage(wxWindow* parent);
	void OnSelectionLinkAdd(wxCommandEvent&);
	void OnSelectionLinkRemove(wxCommandEvent&);
	size_t NamedResultCount() const;
	// Every name the package declares with `ONTO` — what the two choice cells offer.
	std::vector<wxString> NamedResults() const;
	// …minus the one standing on the other side of the row being edited: a selection linked to
	// itself is not a link.
	wxArrayString NamedResultChoices(bool leftSide) const;
	// The ready conditions for a package link — every pair of fields the two selections offer, the
	// same rule the Links tab follows for two tables.
	wxArrayString SelectionLinkConditionChoices() const;
	// The "…" behind a package link's condition — the shared expression editor over the fields of
	// the two selections the row names.
	bool EditSelectionLinkCondition(wxString& text);

	// (The two link grids share no model: the Links tab edits ONE STATEMENT'S JOINS, this one edits
	//  the PACKAGE'S OWN LINKS. Nothing has to ask which grid a cell belongs to.)

	class ibDataViewCtrl*   m_selectionLinks     = nullptr;
	class ibQuerySelectionLinkModel* m_selectionLinkModel = nullptr;   // over the PACKAGE's links, not a statement's joins

	wxWindow* BuildIndexPage   (wxWindow* parent);   // shown only for a create-temp-table statement
	wxWindow* BuildPackagePage(wxWindow* parent);    // the statement list — its own tab, last
	wxWindow* BuildTablesPage  (wxWindow* parent);
	wxWindow* BuildLinksPage   (wxWindow* parent);
	wxWindow* BuildGroupingPage(wxWindow* parent);
	wxWindow* BuildConditionsPage(wxWindow* parent);
	wxWindow* BuildOrderPage   (wxWindow* parent);
	wxWindow* BuildTotalsPage  (wxWindow* parent);
	wxWindow* BuildUnionsPage  (wxWindow* parent);
	wxWindow* BuildAdvancedPage(wxWindow* parent);

	// ---- the AST currently being edited --------------------------------
	// The dialog always edits ONE select — the statement selected in the package strip, or the
	// union branch chosen on the Unions tab. Everything below reads and writes through here, which
	// is what makes the same window work one level down over a nested table.
	ibQuerySelect* Current() const;
	// The STATEMENT's own select — what owns the union branches, the INTO, the FOR UPDATE. Distinct
	// from Current(), which is the BRANCH the tabs are showing when one is picked on the strip.
	ibQuerySelect* StatementSelect() const;

	// ---- fill (AST -> tabs) / collect (tabs -> AST) ---------------------
	// Filling is TOTAL: every tab is rebuilt from the AST, so there is never a pane showing
	// something the query no longer says. Collecting happens per edit, immediately — the AST is
	// the single copy of the truth and the widgets are only ever a view of it.
	void FillAll();
	void FillPackage();
	void FillTables();
	void FillLinks();
	void FillGrouping();
	void FillConditions();
	void FillOrder();
	void FillIndex();
	void FillTotals();
	void FillUnions();
	void FillAdvanced();
	void FillPreview();

	// ---- the source tree (left pane of the Tables tab) -------------------
	void FillSourceTree();

	// ---- verbs ----------------------------------------------------------
	void OnAddStatement(wxCommandEvent&);
	void OnRemoveStatement(wxCommandEvent&);
	void OnQueryKindChanged(wxCommandEvent&);   // the statement's KIND, and the name it then asks for
	// The kind + name written into the statement. `focusName` belongs to the RADIO path only — the
	// name box applies itself on losing focus, and taking the focus back there would trap the caret.
	void ApplyQueryKind(bool focusName);
	// A temp-table name nobody in this package is using. The kind PROPOSES one rather than refusing
	// to change until a name is typed — in this AST the kind IS the name, so an empty box meant the
	// statement never changed kind at all.
	wxString SuggestTempTableName() const;
	// A default name for a NAMED RESULT, built from what the statement reads: the main table plus
	// what it joins. Readable by construction, which "Query2" is not — and a link written against a
	// readable name says what it links.
	wxString SuggestResultName() const;
	void OnMoveStatement(int delta);
	void OnStatementSelected(class ibDataViewEvent&);
	void OnBranchSelected(wxBookCtrlEvent&);   // the union-branch tabs down the right edge
	void OnBatchSelected(wxBookCtrlEvent&);    // the batch-statement tabs, one level further out
	void ShowBranchStrip();                    // both strips: hidden where switching makes no sense

	// A TABLE'S ALIAS IS ITS LABEL, edited in the tree. `FROM Catalog.Products AS p` is what the
	// rest of the query calls that table by, and a name belongs on the thing it names. A field row
	// refuses the edit — its name is the metadata's, not the author's.
	void OnTableAliasEditBegin(wxTreeEvent&);
	void OnTableAliasEditEnd(wxTreeEvent&);
	void OnTableContextMenu(wxTreeEvent&);   // add / nested / rename / delete / parameters, on the table itself
	// THE TABLE THE CURSOR STANDS ON — null on a field row or an empty selection.
	struct ibQuerySource* SelectedSource() const;
	// VIRTUAL TABLE PARAMETERS — one row per parameter the SOURCE declares, in its order. Offered
	// only where a source declares any, and it decides both the rows and what a condition may name.
	void OnTableParameters(wxCommandEvent&);

	// THE LINK CONDITION CELL. The ready links of the selected row (a reference between the two
	// tables, the obvious key pairs) — and the "..." that opens the ordinary expression editor over
	// BOTH tables' fields, which is what writing an arbitrary link means.
	wxArrayString LinkConditionChoices() const;
	bool          EditLinkCondition(wxString& text);
	// THE TABLES THIS ROW MAY JOIN — the query's live sources, minus the one already chosen on the
	// other side. `leftSide` says which cell is asking.
	wxArrayString LinkTableChoices(bool leftSide) const;
	// ⭐ THE GROUPINGS A TOTALS FIGURE CAN BE COMPUTED OVER — this query's own levels, the ones on
	// separators QUALIFIED by the separator's name (`Splitter1.Characteristic`), and an empty line
	// first, which is the ordinary answer: the area comes from the ladder.
	wxArrayString TotalsScopeChoices() const;
	// …and the same choice AS A TREE, where the separators are nodes and their levels hang inside
	// them. Opened by the cell's "..." — a flat list can spell `Splitter1.Level` but cannot show
	// where that level lives.
	bool PickTotalsScope(wxString& text);
	// ADD A LINK BY HAND. Until now a link existed only because a table was added — so joining a
	// table already in the query, or writing a second condition between the same pair, had no verb.
	void OnAddLink(wxCommandEvent&);
	// THE CONDITION CELL — the ready shapes over the query's fields, and the "..." that opens the
	// expression editor on whatever is written there.
	wxArrayString ConditionChoices() const;
	bool          EditConditionText(wxString& text);
	void OnAddTable(wxCommandEvent&);
	// HOW A TABLE JOINS THE QUERY — the first becomes the FROM, the rest are joins with no ON
	// ("follow the reference between them"), each named as it arrives. A plain function on purpose:
	// a verb that needs a table added calls THIS, never another verb's handler. (One did, by
	// synthesising an event, and the day that handler grew a case calling back into the first verb
	// the pair recursed until the stack ran out.)
	void AddTableSources(const std::vector<std::vector<wxString>>& paths);
	void OnAddNestedTable(wxCommandEvent&);
	void OnEditNestedTable(wxCommandEvent&);
	void OnRemoveTable(wxCommandEvent&);
	void OnAddField(wxCommandEvent&);
	void OnAddFieldExpression(wxCommandEvent&);   // a SELECT list takes expressions, not only columns
	// EDIT THE FIELD STANDING HERE, in the arbitrary-expression editor. A projection is an
	// expression — a column is only the simplest one — so "edit this field" and "write an
	// expression" are the same door, and it is the same door the conditions and the joins open.
	void OnEditFieldExpression(wxCommandEvent&);
	void OnRemoveField(wxCommandEvent&);
	void OnMoveField(int delta);
	// (No OnEditFieldAlias: an output field's NAME is typed on the Unions / Aliases tab, in the map
	// that lines the branches up by it. No OnAddLink either — a link exists because a table was
	// added, which is the Tables tab's verb.)

	void OnEditLink(wxCommandEvent&);
	// Copy this link onto the first table that has none — a second link is usually the first with a
	// name changed, and retyping the condition is the work this saves.
	void OnCopyLink(wxCommandEvent&);
	void OnRemoveLink(wxCommandEvent&);
	// ONE editor for a join, reached from the list's row and the diagram's line alike.
	void EditJoinAt(size_t index);

	void OnAddGrouping(wxCommandEvent&);
	void OnRemoveGrouping(wxCommandEvent&);
	void OnAddAggregate(wxCommandEvent&);
	void OnRemoveAggregate(wxCommandEvent&);

	// ADD A CONDITION ON WHAT THE TREE HAS, without asking. The `>` button, a double-click and a
	// drop all mean "put this field in the conditions"; opening the expression editor for each of
	// them made the cheapest gesture the most expensive one. The row lands as `Field = &Field` —
	// a comparison against a PARAMETER, which is what a condition on a field almost always is —
	// and is then typed over in the cell or opened in the editor like any other row.
	void AddConditionsForSelectedFields();
	void OnAddCondition(wxCommandEvent&);   // the toolbar's Add: a blank row, written in the editor
	void OnEditCondition(wxCommandEvent&);
	void OnRemoveCondition(wxCommandEvent&);

	void OnAddIndexField(wxCommandEvent&);
	void OnRemoveIndexField(wxCommandEvent&);
	void OnAddOrder(wxCommandEvent&);
	void OnRemoveOrder(wxCommandEvent&);
	void OnToggleOrderDirection(wxCommandEvent&);
	void OnMoveOrder(int delta);

	// TOTALS has two halves and each has its own verbs — the aggregates (what is totalled) and the
	// dimensions (the levels it is totalled at, IN ORDER, which is why they move up and down).
	// A measure without a level is not a measure - the last level leaving takes the clause with it,
	// and says so. (`TOTALS SUM(x)` with no `BY` is a clause the language does not have.)
	void DropTotalsIfLevelless(ibQuerySelect& select);
	void OnAddTotalsAggregate(wxCommandEvent&);
	void OnEditTotalsAggregate(wxCommandEvent&);      // the arbitrary-expression editor, over this line
	void OnRemoveTotalsAggregate(wxCommandEvent&);
	void OnAddTotalsDimension(wxCommandEvent&);
	// ⭐ ADD A SEPARATOR — a visible node of the totals (`SPLIT`). Groupings are then hung on it the
	// same way they are hung on the hidden node every report has; it is named `Splitter1`, `Splitter2`
	// … so it can be addressed the moment it exists, and the name cell renames it.
	void OnAddTotalsSplit(wxCommandEvent&);
	void OnEditTotalsDimension(wxCommandEvent&);      // the same editor, over the dimension expression
	void OnMoveTotalsDimension(int delta);            // levels apply in order — the order is the setting
	void OnRemoveTotalsLine(wxCommandEvent&);         // the selected DIMENSION line

	// ⭐ THE PERIODICITY PANEL, both directions — the selected level fills it, and editing it writes
	// the level back. THROUGH THE LANGUAGE: the panel composes the field's TEXT and hands it to
	// ibQueryParser::ParseTotalsField, the same pair the cell above it reads and writes by. A second
	// road into `m_periods` would be a second answer to "what does this field say".
	void FillTotalsPeriods();          // level -> panel (and the panel's enabled-ness)
	// ⭐⭐ WHAT THE CARET IS ON — a level of the hidden node, a separator, or a level hung on one.
	// Asked of the TREE (a separator is a node and its groupings are its children), so there is no
	// row map to keep in step with the AST.
	ibTotalsRow SelectedTotalsAt() const;
	// WHERE A NEW GROUPING GOES — the node the caret is on, or the hidden one when nothing is picked.
	// Adding to "wherever the caret is" is what makes a separator usable with a mouse.
	int SelectedTotalsNode() const;
	// ⭐⭐ ADD THE PICKED FIELDS TO ONE NAMED NODE. The node is passed IN rather than read off the
	// caret, because a DROP knows where it landed and the caret does not follow a drop soon enough:
	// selecting the row under the pointer and then asking "which node is selected" answered with the
	// node that had been selected BEFORE, so everything dropped went into whatever separator was last
	// clicked (Max, 2026-08-27).
	void AddTotalsFieldsTo(int node);
	// ⭐ CARRIED WITH THE MOUSE — a row of this grid dragged within it. The payload is the row's
	// COORDINATE and nothing else; the mark is what tells it from a FIELD dragged in from the tree.
	static const wxChar* const kTotalsDragMark;
	static ibTotalsRow ParseTotalsDrag(const wxString& text);
	// Move one level onto the node the pointer was over. Dropped on nothing = the hidden node, which
	// is what "drag it out of the separator" means.
	void MoveTotalsLevelTo(const ibTotalsRow& from, const ibTotalsRow& onto);
	// ⭐⭐ IS THIS FIELD ALREADY REACHABLE HERE? A level is reached through the levels ABOVE it — the
	// common ladder, then the node's own — so the same field twice on one path groups nothing: every
	// heading would hold the single value it already stands under. Asked before a field is added or
	// moved in, never after.
	bool TotalsFieldAlreadyThere(int node, const wxString& name, const ibQueryTotalDim* except = nullptr) const;
	// The levels of a node, whichever node it is — the hidden one's live on the select itself.
	std::vector<ibQueryTotalDim>* LevelsOfNode(ibQuerySelect* select, int node) const;
	void ApplyTotalsPeriods();         // panel -> level
	// Only a DATE can be read by periods (Max) — a period is a scale on the calendar, and a level
	// keyed by anything else has none. Asked of the field's own type, which the model already
	// carries (ibQueryConstructorField::m_type), never of a list of field names kept here.
	bool LevelIsDated(const ibQueryTotalDim& dim) const;
	// (No OnCycleUnfold: the unfold is a registered enumeration, so its cell is a CHOICE over the
	// three words the language has — not a button pressed three times to get back where you were.)

	void OnAddUnionBranch(wxCommandEvent&);
	void OnCopyUnionBranch(wxCommandEvent&);   // a branch is usually the previous one, altered
	void OnRemoveUnionBranch(wxCommandEvent&);
	// BRANCH ORDER IS SEMANTICS: the first branch's columns ARE the union's result, and every other
	// branch is lined up against them. Moving one into first place therefore re-shapes the result,
	// which is why row 0 (this query) takes part in the move like any other branch.
	void OnMoveUnionBranch(int delta);
	// The field map's own verbs — they act on the PROJECTION the row stands for, because the map's
	// rows ARE the output fields (the first branch's projections, which is what a union returns).
	void OnRemoveUnionField(wxCommandEvent&);
	void OnMoveUnionField(int delta);
	void OnEditUnionBranch(wxCommandEvent&);

	void OnAdvancedChanged(wxCommandEvent&);

	// ---- the text pane --------------------------------------------------
	// Editing the text by hand and having it read back is the property that decides whether a
	// constructor keeps being used. Focus loss re-parses; a syntax error is REPORTED and the text
	// left alone, so nobody loses what they typed to a stray keystroke.
	void OnPreviewFocusLost(wxFocusEvent&);
	// The text at full height, in a window of its own — the pane under the tabs is gone, and this is
	// where it went. Borrows m_preview while open and hands it back, so there is still one text.
	void OnShowQueryText(wxCommandEvent&);
	void OnCheck(wxCommandEvent&);
	// Open the SELECTED fragment as a query of its own, read-only — the way to look at one nested
	// query, one union branch or one statement without the rest of the text around it.
	void OnOpenSelection(wxCommandEvent&);
	void OnOk(wxCommandEvent&);

	// Parse `text` through the ENGINE and adopt it. Returns false and shows the engine's own
	// message when it does not parse. THE one gate: nothing else in this file judges a query.
	//
	// `reportModally` — false while the user is simply typing (the verdict line already carries the
	// message, and a dialog on every click-away is noise); true for a moment they ASKED about.
	bool AdoptText(const wxString& text, bool reportModally = true);
	void ShowEngineError(const wxString& message);   // the verdict line, without re-parsing

	// ASK THE ENGINE ABOUT THE CURRENT QUERY. Returns true when it parses; `message` carries the
	// engine's own words, verbatim, when it does not. This is the ONLY thing in the dialog that
	// decides whether a query is valid — there is no checker beside it and no reinterpretation of
	// what it says, because a second opinion would be one more thing to keep in step with the
	// language, and it would be wrong the day the language moved.
	bool AskEngine(wxString& message) const;

	// THE VERDICT ITSELF — the engine's answer PLUS the one fact only this window holds (a link
	// started and left empty). Asked by the line under the tabs and by OK, so the two can never
	// disagree about whether the query is sound.
	bool Verdict(wxString& message) const;
	void ShowEngineVerdict();   // the status line under the text, refreshed on every edit

	// ---- helpers --------------------------------------------------------
	// Every field of every chosen table, qualified — what the Conditions / Order / Grouping /
	// Totals tabs offer. Asked of the model, which asks the sources.
	std::vector<ibQueryConstructorField> AvailableFields() const;
	// The same answer, shown on the left of each of those tabs.
	void FillFieldSources();   // fills all four field trees from the chosen tables

	// `SELECT *` written out as the fields it stands for — once, on opening. A star is a promise
	// about a shape nobody wrote down, and this window is where a shape is written down.
	void ExpandStars();
	// THE FOLD A FIELD OPENS AS — the engine's own list for that field's type, so a seeded row is
	// never a row the query would refuse. Null when the field cannot be folded or cannot be read.
	ibQueryAstExprPtr SeededAggregateFor(const ibQuerySelect& select, const wxString& field);
	// WHICH projections the Grouping tab's lower pane is about — the ones whose expression is an
	// aggregate call. Decided in one place, so the grid, its model and its verbs cannot disagree
	// about which projection a row stands for.
	std::vector<size_t> AggregateRows() const;
	// The row a grid has selected, or -1. Every pane asks the same way now that every pane is a grid.
	long SelectedRow(class ibDataViewCtrl* grid, class ibQueryGridModel* model) const;
	// (Naming is the ENGINE's: ibQueryEnsureUniqueName / ibQueryUniqueSourceAlias in queryRewrite.h.
	// A name generated here would have to match the one ibQueryLowering::CheckNames refuses, and
	// matching by copying is how two answers drift apart.)

	// ONE GATE FOR EVERY NAME THE USER TYPES — a projection alias, a table alias, a totals level's
	// name, a temporary table's name. Two questions, and neither is ours to answer alone:
	//
	//   * can the LANGUAGE carry it? Asked of the LEXER (ibQueryLexer::IsIdentifier), because what
	//     an identifier is is its definition. `Reference f 3` is not one — and the constructor used
	//     to write it into the text and then show its own engine's lexical error back to the user,
	//     which is a window arguing with itself.
	//   * is it FREE? A name already taken makes every use of it ambiguous.
	//
	// Refuses with the reason on the verdict line and returns false; the caller keeps what was there.
	bool AcceptName(const wxString& name, const wxString& what);

	// (Nothing chases paths down when a table is removed. Removal BREAKS them — that is what it is —
	// and the resolution pass on the next refill does not reach them, so they are neither shown nor
	// written. One rule driven by RESOLUTION, instead of a cleanup hung off every verb that can
	// break something. See ibQueryLowering::PruneUnresolved.)
	// A metatype's own picture, by its registered name. wxNOT_FOUND when the config has no such type.
	// EVERY TREE IS DRESSED THE SAME WAY — one helper, so "the catalogue has pictures and the
	// chosen tables do not" cannot happen.
	struct TreeIcons {
		class wxImageList* m_images = nullptr;
		int                m_field  = wxNOT_FOUND;   // the plain attribute picture
		std::map<wxString, int> m_byKind;             // metatype name -> index in THIS list
		// A COLUMN'S OWN PICTURE, keyed by the picture itself. There is no name to key on here —
		// the column hands over an icon, not a kind — and the icons are shared statics, so the
		// same dimension picture arrives as the same object every time and a handful of entries
		// covers a tree of any size.
		std::vector<std::pair<wxIcon, int>> m_byIcon;
	};
	// NOTHING IS EVER REMOVED behind the author's back — not a table with no link (that is a
	// PRODUCT, and it reads exactly like one somebody forgot about), and not a name that no longer
	// resolves (it stays in the query and the engine says so on the verdict line). What a table's
	// own DELETION takes with it is a different verb, and it is explicit: ibQueryDropSourceReferences.

	TreeIcons PrepareIcons(wxTreeCtrl* tree) const;
	int KindIcon(TreeIcons& icons, const wxString& kind) const;
	// THE PICTURE FOR ONE FIELD — its own if the column gave one, the plain field picture if not.
	// Nothing here reads a metatype: the column already answered (ibBackendSourceColumn::GetColumnIcon).
	int FieldIcon(TreeIcons& icons, const ibQueryConstructorField& field) const;
	// The same question for a row of ANY grid in this window: a column answers with its own
	// picture, a dot-walk with the picture of the field it ends on, an expression with none (the
	// grid's plain one is then used). Body in queryConstructorFill.cpp.
	wxIcon IconOfExpr(const ibQueryAstExprPtr& expr) const;

	// (A field row is built and unfolded by ibQueryAddFieldNode / ibQueryExpandFieldNode in
	// queryFieldTree.h — the same pair the expression editor's tree uses.)
	//
	// UNFOLD A REFERENCE — the dot walk the language has always been able to write. Bound on every
	// tree that shows fields.
	void OnFieldTreeExpanding(wxTreeEvent&);
	wxString SelectedFieldOf(class wxTreeCtrl* tree) const;
	// THE SAME SCOPE RULE, everywhere a field tree is acted on: a plain field is itself, a REFERENCE
	// is everything behind it (as dot-walk paths), a table row is every field it has. Written once,
	// because "what you act on decides the scope" is one rule and four copies of it would drift.
	std::vector<wxString> SelectedFieldsOf(class wxTreeCtrl* tree) const;
	// Ask the user for one of those fields; empty when cancelled.
	wxString ChooseField(const wxString& title);
	// The sources of the current select, FROM first then the joins — the order a query names them in.
	std::vector<ibQuerySource*> CurrentSources() const;

	// THE ONE GATE ON EVERY VERB. False in read-only mode, so a handler reached by a button, a
	// double-click or a keyboard shortcut alike stops at its first line. Disabling the buttons
	// alone would leave the double-click paths open, and a read-only window that can still be
	// edited through one of them is worse than none.
	bool CanEdit() const { return !m_readOnly; }

	ibQueryPackage           m_package;
	const ibMetaData*        m_metaData = nullptr;   // handed in; passed down to a nested constructor
	ibQueryConstructorModel  m_model;
	bool                     m_readOnly = false;
	bool                     m_subQuery = false;   // editing a nested table / a union branch
	// WHAT THIS OPENING LEAVES OUT (a mask of ibQueryConstructorExclude). A host that folds elsewhere
	// — a composition (its totals ARE its resources), a dynamic list (it folds through its settings)
	// — excludes Totals, and any TOTALS in the text it was opened on is dropped on the way in.
	int                      m_exclude = ibQueryExclude_None;

	size_t                   m_statement = 0;   // which statement of the package the tabs show
	// The select the tabs edit. Normally the current statement's own; a union branch while one is
	// being edited. Held as an index rather than a pointer so a package edit cannot leave it dangling.
	int                      m_unionBranch = -1;   // -1 = the statement itself

	bool                     m_filling = false;   // re-entrancy guard: filling widgets must not collect

	// THE TAB A PERSON CHOSE, by title. The tab SET follows the statement (a branch with one table
	// has no Links), so switching branches can take the current tab away — and the window then landed
	// on the first one and stayed there. Remembered here, it is returned to the moment it exists
	// again. Written only by a deliberate change, never by a refill.
	wxString                 m_wantedTab;

	class ibDataViewCtrl*   m_statements     = nullptr;   // the package — one row per statement
	class ibQueryGridModel* m_statementModel = nullptr;
	wxNotebook*    m_notebook   = nullptr;
	// THE QUERY TEXT, in a real code editor (wxStyledTextCtrl) with the SQL lexer. The round trip IS
	// the feature, so the text is on screen the whole time — and text that is on screen the whole time
	// should be readable: keywords apart from names, strings apart from numbers. The keyword set comes
	// from the LANGUAGE'S OWN TABLE (ibAllQueryKeywords), never a list typed here.
	class wxStyledTextCtrl* m_preview = nullptr;
	wxStaticText*  m_status      = nullptr;   // the engine's verdict on the query as it stands
	// TWO STRIPS OF TABS DOWN THE RIGHT EDGE, one per level, and both mean the same thing: "the
	// query the tabs are currently showing". A list box was the first shape for the branches and it
	// read as a separate window sitting beside the tabs; a branch — like a batch statement — is not
	// a thing you pick out of a collection, it is what you are IN. Their pages are empty on purpose:
	// the content is the main notebook next to them.
	//
	//   m_batchStrip   the BATCH's statements   — the outer level, furthest right
	//   m_branchStrip  the union branches OF the selected statement — the inner level
	wxNotebook*    m_batchStrip  = nullptr;
	wxNotebook*    m_branchStrip = nullptr;
	// Every command button and tool band, collected as made, so read-only mode greys the whole set
	// in one place. The greying is the SIGN; CanEdit() is the guarantee (a double-click reaches a
	// handler no disabled button guards).
	std::vector<class wxButton*>  m_commandButtons;
	std::vector<class wxToolBar*> m_commandBars;

	// EVERY VERB A TOOLBAR CARRIES, remembered as it is added — so the right-click can offer exactly
	// the same ones without a second list. A menu written out beside a toolbar is a copy, and the day
	// a verb is added to one the two disagree.
	struct Verb
	{
		wxString                             m_label;
		std::function<void(wxCommandEvent&)> m_handler;
	};
	std::map<class wxToolBar*, std::vector<Verb>> m_barVerbs;

	// WHICH TREE THE DRAG STARTED IN. A drop is answered by where it came FROM as much as by where
	// it landed: a field dropped on the Fields pane from the chosen tables is "select this field",
	// the same field dropped there from the CATALOGUE is "read this table and select this field" —
	// one gesture, two steps, because that is what the author meant and making them do it in two
	// moves is the constructor asking them to do its bookkeeping.
	class wxTreeCtrl* m_dragTree = nullptr;
	// Add the catalogue field standing in `m_sourceTree` — bringing its TABLE in first if the query
	// does not read it yet. Returns false when there is nothing to add.
	bool AddCatalogueFieldToSelect();

	// Drag in flight from one of the two trees (same-process; the payload is only what wx needs to
	// start a drag at all — the thing being moved is the item the tree has selected).
	void OnSourceBeginDrag(wxTreeEvent&);
	void OnTableBeginDrag(wxTreeEvent&);
	void OnFieldTreeBeginDrag(wxTreeEvent&);   // any of the four field trees

	wxTreeCtrl*  m_sourceTree = nullptr;   // the catalogue (a WALK over the factory) + the package's temps
	wxTreeCtrl*  m_tables     = nullptr;   // chosen tables, each expanding into its fields
	// The chosen fields — Field | Alias, both editable IN PLACE. Editing the field opens the
	// arbitrary-expression editor, because a SELECT list takes expressions, not only columns.
	class ibDataViewCtrl* m_fields = nullptr;
	class ibQueryGridModel* m_fieldModel = nullptr;

	class ibDataViewCtrl*    m_links     = nullptr;   // the links GRID — edited in place
	class ibQueryLinkModel* m_linkModel = nullptr;   // its model over m_joins
	class ibQueryJoinDiagram* m_diagram = nullptr;   // the picture over the same m_joins

	// EVERY TAB SHOWS WHAT THERE IS TO CHOOSE FROM, on its left. A tab that lists only what has
	// already been chosen sends the author to another tab to remember a field's name — so each of
	// these is the same available-field list, filled from the same AvailableFields().
	// THE SAME TREE, four times: the chosen tables, each expanding into its fields. A flat list of
	// qualified names was the first shape and it hid the thing a person is looking for — which table
	// a field belongs to. Every one of these is draggable onto the pane beside it.
	wxTreeCtrl*  m_groupingSource  = nullptr;
	wxTreeCtrl*  m_conditionSource = nullptr;
	wxTreeCtrl*  m_orderSource     = nullptr;
	wxTreeCtrl*  m_indexSource     = nullptr;
	wxTreeCtrl*  m_totalsSource    = nullptr;

	// EVERY LIST IN THIS WINDOW IS A GRID, on the same control with the same model class
	// (queryGridModel.h). It was six listboxes and two listctrls beside three dataview grids, and
	// that mixture is what made the window read as several dialogs: different row heights, different
	// grid lines, and — the part that matters — no way to edit a cell where it stands.
	class ibDataViewCtrl* m_indexFields = nullptr;   // INDEX BY — the temp table this statement makes
	class ibQueryGridModel* m_indexModel = nullptr;

	class ibDataViewCtrl* m_grouping      = nullptr;   // Grouping field
	class ibQueryGridModel* m_groupingModel = nullptr;
	class ibDataViewCtrl* m_aggregates    = nullptr;   // Summed field | Function
	class ibQueryGridModel* m_aggregateModel = nullptr;
	class ibDataViewCtrl*         m_conditions     = nullptr;   // # | Arbitrary | Condition
	class ibQueryConditionModel*  m_conditionModel = nullptr;   // its model over the WHERE chain
	class ibDataViewCtrl* m_order      = nullptr;   // Field | Direction
	class ibQueryGridModel* m_orderModel = nullptr;
	// TOTALS, in the two halves the clause has: the aggregates it computes, and the levels it
	// computes them AT. Each level carries its own name — see ibQueryTotalDim::m_alias.
	class ibDataViewCtrl* m_totalsAggregates = nullptr;   // Totals field | Expression
	class ibQueryGridModel* m_totalsAggregateModel = nullptr;
	class ibDataViewCtrl* m_totalsDimensions = nullptr;   // Grouping field | Totals kind | Alias
	class ibQueryTotalsTreeModel* m_totalsDimensionModel = nullptr;   // a TREE: a separator is a node, its groupings are its children
	wxCheckBox*  m_grandTotals = nullptr;   // what the root of a totals tree IS — see BuildTotalsPage
	// ⭐ THE SELECTED LEVEL'S PERIODICITY, stated with a mouse. It belongs to ONE level, so it sits
	// under the grid and follows the selection — the same place, and the same reading, a person is
	// used to: a switch, the unit, and the two bounds that may be left open.
	// ⭐ THE WHOLE STRIP IS HIDDEN where it does not apply, not greyed (Max): a level keyed by
	// something that is not a date has no periodicity to speak of, and a row of dead controls under
	// the grid says "this exists for you and is switched off", which is a different and untrue thing.
	class wxPanel* m_periodPane = nullptr;
	wxCheckBox*  m_byPeriods   = nullptr;
	class wxChoice*   m_periodUnit = nullptr;   // the ten words, taken from ibPeriodUnits() — never a second list
	class wxTextCtrl* m_periodFrom = nullptr;   // an expression: a parameter, a date. Empty = from the data
	class wxTextCtrl* m_periodTo   = nullptr;
	// ⭐ DID ANYBODY TYPE IN THEM? The bounds commit on losing focus, and a focus change is not an
	// edit — a click through the tab would otherwise rewrite the level on every pass. Asked of the
	// EDIT EVENT rather than by comparing what the panel would write against what the level says:
	// that comparison ran through two separate renderings of the same thing, and two renderings
	// drift. `ChangeValue` (how the panel fills itself) raises no event, so filling never sets it.
	bool m_periodBoundsEdited = false;
	class ibDataViewCtrl*          m_unions          = nullptr;   // the branches
	class ibQueryUnionModel*       m_unionModel      = nullptr;
	class ibDataViewCtrl*          m_unionFields     = nullptr;   // the field map across branches
	class ibQueryUnionFieldModel*  m_unionFieldModel = nullptr;

	wxCheckBox*  m_distinct   = nullptr;
	wxCheckBox*  m_allowed    = nullptr;
	wxCheckBox*  m_forUpdate  = nullptr;
	wxCheckBox*  m_useTop     = nullptr;
	wxSpinCtrl*  m_topCount   = nullptr;

	// WHAT THIS STATEMENT IS — a select, a select that materialises a temp table, or a drop of one.
	// A KIND, not a checkbox: the three are different statements, and the name is asked because the
	// kind asks for it, not because a flag was ticked. It lives on the package strip rather than on
	// a tab because it says what the statement IS, while the tabs say what its query SELECTS.
	class wxRadioBox* m_queryKind = nullptr;
	wxTextCtrl*  m_tempName   = nullptr;
};

// Open the constructor over query TEXT (a package). Returns true on OK, with `queryText` replaced
// by the rendered package; false on Cancel or in read-only mode, with the text untouched. Empty
// text opens on a blank package — building from nothing is the same road as building from something.
//
// `metaData` is REQUIRED, deliberately: the caller knows which config the query belongs to, and a
// default that quietly fell back to the active one would resolve a copied query's tables against
// somebody else's configuration.
FRONTEND_API bool ibShowQueryConstructor(wxWindow* parent, wxString& queryText,
                                         const ibMetaData* metaData, bool readOnly = false,
                                         int exclude = ibQueryExclude_None);

// Open it over ONE select, edited in place — a nested table, a union branch. This is the re-entrant
// door: the shell is not tied to a top-level statement.
FRONTEND_API bool ibShowQueryConstructorFor(wxWindow* parent, ibQuerySelectPtr& select,
                                            const ibMetaData* metaData, bool readOnly = false,
                                         int exclude = ibQueryExclude_None);

// STYLE A PANE THAT SHOWS QUERY TEXT — the SQL lexer, the keyword set taken from the LANGUAGE'S own
// table, and the ENGINE's font and colours. Shared, because there is more than one such pane (the
// constructor's text, the expression editor's) and two of them styled separately would drift into
// two different-looking editors for one language.
// ⭐⭐ PICK THE GROUPINGS A FIGURE IS COMPUTED OVER — the ticked tree, shared by the two windows that
// ask this question: the query constructor's Totals tab and the composer's resources.
//
// One control, one gesture, one place to change them: `groupings` is what to show (a separator's
// levels come as "Separator.Level"), `separators` marks which of those lines are separators — shown
// as nodes and not tickable, since a branch does not narrow the rows and cannot be an area. `inOut`
// carries the current answer in and the new one out, comma-separated. False = cancelled.
FRONTEND_API bool ibPickGroupingScope(wxWindow* parent,
                                      const std::vector<wxString>& groupings,
                                      const std::vector<wxString>& separators,
                                      wxString& inOut);

FRONTEND_API void ibStyleQueryText(class wxStyledTextCtrl* text);

// The indicator `&name` is painted with. An INDICATOR and not a style, because the lexer owns the
// styles and repaints them; indicators are drawn on top and survive re-lexing.
constexpr int kQueryParameterIndicator = 8;

// MARK EVERY `&name` — call after the pane's text changes, since the marks go with the text they
// marked. A parameter is a value handed in from outside, and reading exactly like a column of the
// query is how somebody spends a while wondering why "that field" cannot be found.
FRONTEND_API void ibMarkQueryParameters(class wxStyledTextCtrl* text);

#endif // __QUERY_CONSTRUCTOR_DLG_H__
