#ifndef _FRMCLIENT_QUERY_CONSTRUCTOR_H__
#define _FRMCLIENT_QUERY_CONSTRUCTOR_H__

// THE QUERY CONSTRUCTOR — the desktop's ibDialogQueryConstructor (frontend/win/dlgs/queryConstructor/queryConstructor.h):
// the designer's, over the sources and fields the configuration has. Its doors are here with the desktop's signatures,
// and they answer Cancel: the client is not the designer yet, and a window that opens one (a composition's Query tab)
// is not built for a reader.

#include <wx/string.h>

#include <vector>

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
// text untouched — which is the answer here.
inline bool ibShowQueryConstructor(wxWindow* WXUNUSED(parent), wxString& WXUNUSED(queryText),
	const ibMetaData* WXUNUSED(metaData), bool WXUNUSED(readOnly) = false, int WXUNUSED(exclude) = ibQueryExclude_None)
{
	return false;
}

// WHICH GROUPINGS AN AREA COVERS — the desktop's picker over the groupings named. It answers Cancel: `inOut` stays.
inline bool ibPickGroupingScope(wxWindow* WXUNUSED(parent), const std::vector<wxString>& WXUNUSED(groupings),
	const std::vector<wxString>& WXUNUSED(separators), wxString& WXUNUSED(inOut))
{
	return false;
}

// QUERY TEXT, PAINTED AS THE LANGUAGE — the desktop's: its words are the engine's keyword table, which is the server's.
// Here the text reads plain.
inline void ibStyleQueryText(class wxStyledTextCtrl* WXUNUSED(text))
{
}

#endif
