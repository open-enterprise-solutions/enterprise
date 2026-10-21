#ifndef _FRMCLIENT_DATA_PROTOCOL_H__
#define _FRMCLIENT_DATA_PROTOCOL_H__

// THE WIRE'S NODE AND THE ENGINE'S — the client's one seam between them. The server writes its nodes to the wire with
// its JSON (ibJsonProvider), so the client reads them back by the same reader's rule (ibJsonProvider::ReadEntry): what is
// a field, what a property and what a child is the engine's — a scalar is a field, a node is a property, NodeChildren are
// the children, NodeType and NodeId the node's own. Read off the tree the answer already is, no text written for it and
// read again. Written back the same way, the server reads what it wrote.

#include "frmclient/frmclient.h"
#include "core/serialize/dataBuilder.h"
#include "protocol/protocolNode.h"

// The wire's node read into `node` — false when it is none.
FRMCLIENT_API bool ibReadProtocolNode(const ibProtocolNode& wire, ibDataNode& node);
// …and one entry of it, `name`, into `value` — false when it has none: a control's property off its node, the children
// of the node not read with it.
FRMCLIENT_API bool ibReadProtocolValue(const ibProtocolNode& wire, const ibString& name, ibDataValue& value);

// `node` written into the wire's node `wire` — a handle, filled in place.
FRMCLIENT_API void ibWriteProtocolNode(const ibDataNode& node, ibProtocolNode wire);

#endif
