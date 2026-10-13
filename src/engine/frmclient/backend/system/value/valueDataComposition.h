#ifndef _FRMCLIENT_BACKEND_VALUE_DATA_COMPOSITION_H__
#define _FRMCLIENT_BACKEND_VALUE_DATA_COMPOSITION_H__

// THE COMPOSITION — the engine's ibValueDataComposition (backend/system/value/valueDataComposition.h): the server's,
// which runs it. What the client's windows ask of it is the rule that the parameters follow the texts (`&Rate` in the
// query names one) — read by the query lexer, which is the server's: here the parameters stay as they were given.

#include <wx/defs.h>

struct ibCompositionDescription;

inline void ibSyncParameters(ibCompositionDescription& WXUNUSED(composition))
{
}

#endif
