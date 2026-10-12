#ifndef _FRMCLIENT_BACKEND_QUERY_LOWERING_H__
#define _FRMCLIENT_BACKEND_QUERY_LOWERING_H__

// THE LOWERING — the engine's ibQueryLowering (backend/query/queryLowering.h): a parsed query made into the database's,
// which is the server's. What the client's windows ask of it is which aggregates a field's type admits — the offers of a
// resource cell; here it offers none, and the cell takes what is typed.

#include <vector>

#include <wx/string.h>

#include "frmclient/backend/typeDescription.h"
#include "frmclient/backend/query/queryKeywords.h"

class ibQueryLowering {
public:
	static std::vector<ibQueryKeyword> AggregatesFor(const ibTypeDescription& WXUNUSED(type)) { return {}; }
	static std::vector<wxString> AggregateCallsFor(const ibTypeDescription& WXUNUSED(type), const wxString& WXUNUSED(field)) { return {}; }
};

#endif
