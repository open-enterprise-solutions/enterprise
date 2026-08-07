#ifndef __QUERY_CASE_DIALOG_H__
#define __QUERY_CASE_DIALOG_H__

////////////////////////////////////////////////////////////////////////////
// CASE WHEN — the one construction in this language that is a LIST, edited as one.
////////////////////////////////////////////////////////////////////////////
//
// `CASE WHEN p1 THEN e1 WHEN p2 THEN e2 ELSE e END` has been in the language from the start: the
// parser reads it, the renderer writes it, the lowering runs it (ibQueryColumnExpr::Case). What was
// missing is a way to WRITE one without typing it, and the palette's skeleton is not that — it drops
// five keywords into a text box and leaves the author to keep the WHENs and THENs paired by hand.
//
// It is worth its own window for a reason no other expression has: **it is an ORDERED LIST**. Every
// other expression is one thing; this is n branches that are tried in order, and the order is the
// meaning — swap two WHENs and the answer changes. A list is edited as rows with move buttons, not
// as a sentence, and the moment there are three branches the text form stops being readable.
//
// EACH CELL IS STILL AN EXPRESSION, so each opens the SAME expression editor the rest of the window
// uses — the fields on the left, the language palette beside them, the engine reading what is typed.
// This window owns the SHAPE (which branch, in what order); it owns no syntax of its own.
//
// The round trip is the same contract as everywhere else: it opens on an existing CASE and hands
// one back, so a CASE written by hand can be opened here and one built here can be edited by hand.
//
////////////////////////////////////////////////////////////////////////////

#include "frontend/frontend.h"

#include <wx/dialog.h>

#include <vector>

#include "backend/query/queryAst.h"
#include "backend/query/queryConstructorModel.h"

#include "frontend/win/ctrls/dataview/dataview.h"

class ibMetaData;

class FRONTEND_API ibDialogQueryCase : public wxDialog
{
public:
	// `existing` — the CASE being edited, or anything else (then the window opens empty and the
	// expression handed in is left alone). `fields` — what the query offers, for the cell editors.
	ibDialogQueryCase(wxWindow* parent, const std::vector<ibQueryConstructorField>& fields,
	                  const ibQueryAstExprPtr& existing,
	                  const ibMetaData* metaData = nullptr, bool readOnly = false);

	// The CASE as it now stands. Null when every branch was removed — a CASE with no WHEN is not a
	// CASE, and handing back an empty one would write `CASE END`, which the parser refuses.
	ibQueryAstExprPtr GetExpression() const;

private:
	// One branch, held as TEXT while it is being edited and parsed by the ENGINE on the way out —
	// the same rule the rest of the window follows: what the author typed is judged by the parser,
	// never by us.
	struct Branch
	{
		wxString m_when;
		wxString m_then;
	};

	void OnAdd(wxCommandEvent&);
	void OnRemove(wxCommandEvent&);
	void MoveBranch(int delta);
	void OnOk(wxCommandEvent&);
	// Open the expression editor over one cell; empty title means the window picks one.
	bool EditCell(wxString& text, const wxString& title);
	// ⚠ NOT `Refresh`, AND NOT `Move`. This is a wxDialog: both of those are wxWindow's, and a
	// member of the same name HIDES the base one for every caller and for this class itself —
	// `Refresh()` would repaint nothing and `Move(x, y)` would not compile. What these do has its
	// own name anyway, taken from what they act on.
	void ShowBranches();
	long SelectedRow() const;

	std::vector<ibQueryConstructorField> m_fields;
	const ibMetaData* m_metaData = nullptr;
	bool              m_readOnly = false;

	std::vector<Branch> m_branches;
	wxString            m_otherwise;   // the ELSE, empty when there is none

	ibDataViewCtrl*          m_grid  = nullptr;
	class ibQueryGridModel*  m_model = nullptr;
	class wxTextCtrl*        m_elseBox = nullptr;
};

// Open the CASE builder over `expression` (a CASE, or anything else to start empty). Returns true
// when the author pressed OK, and replaces `expression` with what they built.
// (FRONTEND_API, not BACKEND_API — it lives in frontend.dll. The wrong macro linked by luck here,
//  and would have exported it from the wrong library the moment anything outside asked for it.)
FRONTEND_API bool ibShowQueryCaseBuilder(wxWindow* parent,
                                        const std::vector<ibQueryConstructorField>& fields,
                                        ibQueryAstExprPtr& expression,
                                        const ibMetaData* metaData = nullptr, bool readOnly = false);

#endif // __QUERY_CASE_DIALOG_H__
