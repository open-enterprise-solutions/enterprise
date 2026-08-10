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

class ibMetaData;

// ---------------------------------------------------------------------------
// The dialog. Two ways in, both below the class:
//   * ibShowQueryConstructor(parent, text)   — over query TEXT (a package): the dynamic list's
//     settings and the code editor's context menu both use this.
//   * ibShowQueryConstructorFor(parent, sel) — over ONE select, in place: how a nested table and
//     a union branch are edited (the re-entrant path).
// ---------------------------------------------------------------------------
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
	                         const ibMetaData* metaData, bool readOnly = false);

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
	};
	std::vector<Page> m_pages;
	void SyncNotebookPages();   // the tab set follows what the current statement IS

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
	void OnTableContextMenu(wxTreeEvent&);   // add / nested / rename / delete, on the table itself
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
	void OnEditTotalsDimension(wxCommandEvent&);      // the same editor, over the dimension expression
	void OnMoveTotalsDimension(int delta);            // levels apply in order — the order is the setting
	void OnRemoveTotalsLine(wxCommandEvent&);         // the selected DIMENSION line
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
	void ShowEngineVerdict();   // the status line under the text, refreshed on every edit

	// ---- helpers --------------------------------------------------------
	// Every field of every chosen table, qualified — what the Conditions / Order / Grouping /
	// Totals tabs offer. Asked of the model, which asks the sources.
	std::vector<ibQueryConstructorField> AvailableFields() const;
	// The same answer, shown on the left of each of those tabs.
	void FillFieldSources();   // fills all four field trees from the chosen tables
	static wxString SeededAggregate(const wxString& field);   // what an aggregate opens with
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
	// A TABLE NOBODY USES is dropped ON THE WAY OUT (OnOk), never during editing — see the body.
	// Nothing else is ever removed behind the author's back: a name that no longer resolves stays
	// in the query and the engine says so on the verdict line.
	void DropUnusedTables();

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

	size_t                   m_statement = 0;   // which statement of the package the tabs show
	// The select the tabs edit. Normally the current statement's own; a union branch while one is
	// being edited. Held as an index rather than a pointer so a package edit cannot leave it dangling.
	int                      m_unionBranch = -1;   // -1 = the statement itself

	bool                     m_filling = false;   // re-entrancy guard: filling widgets must not collect

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
	class ibQueryGridModel* m_totalsDimensionModel = nullptr;
	wxCheckBox*  m_grandTotals = nullptr;   // what the root of a totals tree IS — see BuildTotalsPage
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
                                         const ibMetaData* metaData, bool readOnly = false);

// Open it over ONE select, edited in place — a nested table, a union branch. This is the re-entrant
// door: the shell is not tied to a top-level statement.
FRONTEND_API bool ibShowQueryConstructorFor(wxWindow* parent, ibQuerySelectPtr& select,
                                            const ibMetaData* metaData, bool readOnly = false);

// STYLE A PANE THAT SHOWS QUERY TEXT — the SQL lexer, the keyword set taken from the LANGUAGE'S own
// table, and the ENGINE's font and colours. Shared, because there is more than one such pane (the
// constructor's text, the expression editor's) and two of them styled separately would drift into
// two different-looking editors for one language.
FRONTEND_API void ibStyleQueryText(class wxStyledTextCtrl* text);

#endif // __QUERY_CONSTRUCTOR_DLG_H__
