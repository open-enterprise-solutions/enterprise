#ifndef _SELECT_PREDEFINED_WND_H__
#define _SELECT_PREDEFINED_WND_H__

////////////////////////////////////////////////////////////////////////////
// THE DESIGNER'S CHOICE OF A DECLARED VALUE
////////////////////////////////////////////////////////////////////////////
//
// The old desktop's, its second question: WHICH DECLARED VALUE — the empty
// reference of a type, a predefined element, an enumeration member — as a tree with a branch per admitted reference
// type. What the configuration declares, and the first question (which type, through the type list every other road
// opens), are the server's (frmserver/win/dlgs/selectPredefined.h); the tree comes as request PredefinedSelector, each
// value with the number the server knows it by.
////////////////////////////////////////////////////////////////////////////

#include <wx/window.h>

#include "frmclient/frmclient.h"
#include "protocol/protocolNode.h"

// Answers `true` when a value was chosen — its Id in `response`; `false` when the window was closed, or a type's own
// node was chosen (none is a value).
FRMCLIENT_API bool ibShowPredefinedSelector(wxWindow* parent, const ibProtocolNode& request, ibProtocolNode& response);

#endif // _SELECT_PREDEFINED_WND_H__
