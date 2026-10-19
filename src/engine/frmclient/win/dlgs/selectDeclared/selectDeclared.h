#ifndef __SELECT_DECLARED_H__
#define __SELECT_DECLARED_H__

////////////////////////////////////////////////////////////////////////////
// THE DESIGNER'S DECLARED-VALUE WINDOW — protocol DeclaredChoice
////////////////////////////////////////////////////////////////////////////
//
// Mirrors the desktop's ibShowPredefinedSelector: primitives (optional) plus one technical
// "declared value" entry, then a tree of EmptyRef / predefined / enum members per admitted
// reference type. Fills the response; cancelled leaves it empty.
////////////////////////////////////////////////////////////////////////////

#include "frmclient/frmclient.h"
#include "protocol/protocolNode.h"
#include "protocol/protocol.h"

class FRMCLIENT_API ibDialogSelectDeclared {
public:
	// Shows the picker for `request` (Kind DeclaredChoice). True when the person chose a value
	// and `response` holds Type or MetaId/Guid/Written; false when cancelled.
	static bool ShowModal(const ibProtocolNode& request, ibProtocolNode& response);
};

#endif // __SELECT_DECLARED_H__
