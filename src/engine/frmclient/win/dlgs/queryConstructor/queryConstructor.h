#ifndef _FRMCLIENT_QUERY_CONSTRUCTOR_H__
#define _FRMCLIENT_QUERY_CONSTRUCTOR_H__

// THE QUERY CONSTRUCTOR — the desktop's ibDialogQueryConstructor (frontend/win/dlgs/queryConstructor/queryConstructor.h):
// the designer's, over the sources and fields the configuration has. Its doors are here with the desktop's signatures;
// those that need the configuration answer Cancel.
//
// ⚠ NOT HERE YET: the window and its pickers (queryConstructor*.cpp, query*Dialog, query*Model) are not ported — the
// sources and fields it shows are the server's configuration, the query it reads and writes the server's parser. A door
// a person opens SAYS so, rather than opening nothing.

#include <wx/string.h>
#include <wx/intl.h>
#include <wx/msgdlg.h>

#include <vector>

#include "frmclient/frmclient.h"   // FRMCLIENT_API

class wxWindow;
class ibMetaData;

// The tabs (and the words) a host leaves out — the desktop's bits.
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
	ibQueryExclude_Tables     = 1 << 9,
	ibQueryExclude_Roles      = 1 << 10,
};

// Open the constructor over query TEXT (a package). True on OK, with `queryText` replaced; false on Cancel, with the
// text untouched — which is the answer here, said to the person.
inline bool ibShowQueryConstructor(wxWindow* parent, wxString& WXUNUSED(queryText),
	const ibMetaData* WXUNUSED(metaData), bool WXUNUSED(readOnly) = false, int WXUNUSED(exclude) = ibQueryExclude_None)
{
	wxMessageBox(_("The query constructor is not in this client yet: the query is written by hand."),
		_("Query constructor"), wxOK | wxICON_INFORMATION, parent);
	return false;
}

// ⭐⭐ PICK THE GROUPINGS A FIGURE IS COMPUTED OVER — the ticked tree, shared by the two windows that
// ask this question: the query constructor's Totals tab and the composer's resources.
//
// One control, one gesture, one place to change them: `groupings` is what to show (a separator's
// levels come as "Separator.Level"), `separators` marks which of those lines are separators — shown
// as nodes and not tickable, since a branch does not narrow the rows and cannot be an area. `inOut`
// carries the current answer in and the new one out, comma-separated. False = cancelled.
// (queryConstructorEdit.cpp — the desktop's code as it is: it asks nothing of the configuration.)
FRMCLIENT_API bool ibPickGroupingScope(wxWindow* parent,
                                       const std::vector<wxString>& groupings,
                                       const std::vector<wxString>& separators,
                                       wxString& inOut);

// QUERY TEXT, PAINTED AS THE LANGUAGE — the desktop's (queryConstructor.cpp): its words are the language's keyword
// table (core), its colours the engine's defaults, and line numbers in the margin.
FRMCLIENT_API void ibStyleQueryText(class wxStyledTextCtrl* text);

// The indicator `&name` is painted with. An INDICATOR and not a style, because the lexer owns the
// styles and repaints them; indicators are drawn on top and survive re-lexing.
constexpr int kQueryParameterIndicator = 8;
// …and the one a word of the language is painted with where only its PLACE says it is one: `Date` after the
// AS of a CAST is a type, `YEAR` before a bracket a call — and `Doc.Date` stays a field.
constexpr int kQueryPlacedWordIndicator = 9;

// MARK WHAT THE LEXER CANNOT — call after the pane's text changes, since the marks go with the text they
// marked. Every `&name`: a parameter is a value handed in from outside, and reading exactly like a column
// of the query is how somebody spends a while wondering why "that field" cannot be found. And every word
// that is the language's by its place (kQueryPlacedWordIndicator) — ⚠ not here yet, see queryConstructor.cpp.
FRMCLIENT_API void ibMarkQueryText(class wxStyledTextCtrl* text);

#endif
