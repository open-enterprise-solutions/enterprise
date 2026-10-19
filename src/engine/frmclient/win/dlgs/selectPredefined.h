#ifndef __FRMCLIENT_SELECT_PREDEFINED_H__
#define __FRMCLIENT_SELECT_PREDEFINED_H__

////////////////////////////////////////////////////////////////////////////
// THE DESIGNER'S CHOICE OF A DECLARED VALUE — the desktop's selectPredefined, asked as PredefinedSelector
////////////////////////////////////////////////////////////////////////////
//
// Mirrors the desktop's ibShowPredefinedSelector: primitives (optional) plus one technical
// "declared value" entry, then a tree of EmptyRef / predefined / enum members per admitted
// reference type. Fills the response; cancelled leaves it empty.
////////////////////////////////////////////////////////////////////////////

#include "frmclient/frmclient.h"
#include "protocol/protocolNode.h"
#include "protocol/protocol.h"

// Shows the picker for `request` (Kind PredefinedSelector). True when the person chose a value
// and `response` holds Type or MetaId/Guid/Written; false when cancelled.
FRMCLIENT_API bool ibShowPredefinedSelector(const ibProtocolNode& request, ibProtocolNode& response);

#endif // __FRMCLIENT_SELECT_PREDEFINED_H__
