#ifndef __QUERY_EXPRESSION_DIALOG_H__
#define __QUERY_EXPRESSION_DIALOG_H__

////////////////////////////////////////////////////////////////////////////
// "Arbitrary expression" — ONE editor, opened everywhere an expression is written.
////////////////////////////////////////////////////////////////////////////
//
// A condition row, a join's ON, a totals expression, an aggregate: all of them are ONE expression
// in the query language, so all of them open THIS window. Before it there was a text box on each
// of those dialogs — four places to grow a function palette into, four places for the quoting rules
// to drift, and four chances for one of them to be the one that cannot open a nested query.
//
// Three panes, and each earns its place:
//
//   * the FIELDS of the query — what an expression is usually about;
//   * the LANGUAGE: its functions and operators, built FROM THE KEYWORD TABLE
//     (`ibQueryKeywordText`), never written out here. A palette with its own spelling of `LIKE`
//     is a second dictionary, and it is wrong the day a localized table is installed;
//   * the TEXT, which is the thing being written. Double-click anything on either side and it is
//     inserted at the caret.
//
// And the one that makes it a constructor rather than a text box: **a nested query**. `IN (SELECT
// …)` is an expression whose right-hand side is a query, so the button opens the whole query
// constructor one level down and drops the rendered result in at the caret.
//
// THE ENGINE IS STILL THE ONLY JUDGE. OK hands the text to `ibQueryParser::ParseExpression` and
// shows whatever it throws, verbatim.
//
////////////////////////////////////////////////////////////////////////////

#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/listbox.h>
#include <wx/treectrl.h>
#include <wx/stc/stc.h>

#include "backend/query/queryAst.h"
#include "backend/query/queryConstructorModel.h"

#include <vector>

class ibMetaData;

class ibDialogQueryExpression : public wxDialog
{
public:
	// `existing` — the expression being edited (null when writing a new one). `fields` — what the
	// query offers, shown on the left. `metaData` / `readOnly` are carried only so a nested query
	// opens against the same configuration and under the same rights.
	// ⭐ `allowWindows` — WHICH STOREY THIS EXPRESSION BELONGS TO. A window is computed in the
	// SELECTION, over rows; TOTALS folds NODES and its parser takes aggregates only. Offering a
	// window where the engine refuses one is how a window teaches people to distrust it, so the
	// totals' editor is opened without them.
	ibDialogQueryExpression(wxWindow* parent, const wxString& title,
	                        const std::vector<ibQueryConstructorField>& fields,
	                        const ibQueryAstExprPtr& existing,
	                        const ibMetaData* metaData = nullptr, bool readOnly = false,
	                        bool allowWindows = true);

	ibQueryAstExprPtr GetExpression() const { return m_expression; }
	wxString          GetText() const;

	// Open with something already written — a head start, not a default. The Conditions tab uses it
	// to turn "the field you had selected" into `Field = ` with the caret after it, which is what
	// the old two-mode condition dialog's "simple mode" was actually for.
	void SetText(const wxString& text);

private:
	void OnOk(wxCommandEvent&);
	void OnInsertField(wxTreeEvent&);
	void OnInsertLanguage(wxTreeEvent&);
	// A field DRAGGED into the text lands where it is dropped.
	void OnFieldDrag(wxTreeEvent&);
	// A REFERENCE unfolds here exactly as it does in the constructor's own trees — asked of the
	// model when the [+] is clicked, never walked in advance.
	void OnFieldExpanding(wxTreeEvent&);
	// (The row itself is built by ibQueryAddFieldNode in queryFieldTree.h — the same one the
	// constructor's trees are built from, so the two cannot drift.)
	// And the language palette: dropping `SUM()` or the `CASE … END` skeleton writes what a
	// double-click would, at the point it was dropped.
	void OnLanguageDrag(wxTreeEvent&);
	void OnNestedQuery(wxCommandEvent&);
	// THE CHOICE BUILDER — `CASE WHEN` opened as the ordered list of branches it is, over the
	// selection, and written back in its place. See queryCaseDialog.h.
	void OnEditChoice(wxCommandEvent&);
	// OPEN THE SUBQUERY THAT IS ALREADY WRITTEN. Select it in the text and this opens the whole
	// constructor over exactly that span, putting the result back where it was — so a nested query
	// can be gone back INTO, not only created. Parentheses around the span are unwrapped for the
	// parser and re-wrapped on the way back.
	void OnEditSelection(wxCommandEvent&);

	// The language's own words, as a tree: Functions / Operators / Other. Every leaf's TEXT comes
	// from the keyword table, so this is a view of the language rather than a copy of it.
	void FillLanguageTree();
	void InsertAtCaret(const wxString& text);

	// ⭐ WHAT THE EXPRESSION NAMES, CHECKED AGAINST WHAT THE QUERY HAS — run on OK, after the text
	// parses. A parser answers "is this a sentence"; it cannot answer "is there such a field", and
	// that is the mistake a person actually makes: a field spelled from memory, a source alias that
	// belongs to another query, a period word that is not one. Left unchecked, the expression stores
	// happily and the query refuses to run LATER, somewhere else, with the position counted in a text
	// this window never showed.
	//
	// Empty return = nothing to say. Otherwise the sentence to put in front of the person, naming the
	// word that could not be resolved and what the query does offer instead.
	wxString CheckExpressionNames(const ibQueryAstExprPtr& expression) const;

	std::vector<ibQueryConstructorField> m_fields;
	ibQueryAstExprPtr m_expression;
	const ibMetaData* m_metaData = nullptr;
	bool              m_readOnly = false;
	bool              m_allowWindows = true;   // see the constructor: which storey this expression is on
	// The MODEL, so a reference can be walked into here as it is in the constructor. Built from the
	// same metadata the dialog was handed — the fields it lists came from it in the first place.
	ibQueryConstructorModel m_model;

	// A TREE, not a list. A flat list cannot show what a reference LEADS to, so every field behind
	// one was unreachable from here — you had to close the editor, unfold it in the constructor's
	// tree, and come back. Same shape as those trees: twist buttons, field pictures, draggable.
	wxTreeCtrl* m_fieldTree = nullptr;
	int         m_fieldIcon = wxNOT_FOUND;
	wxTreeCtrl* m_language  = nullptr;
	// The same styled editor the constructor's text pane uses — one language, one look.
	class wxStyledTextCtrl* m_text = nullptr;
};

#endif // __QUERY_EXPRESSION_DIALOG_H__
