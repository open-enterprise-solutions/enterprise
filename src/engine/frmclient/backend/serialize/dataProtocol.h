#ifndef _FRMCLIENT_DATA_PROTOCOL_H__
#define _FRMCLIENT_DATA_PROTOCOL_H__

// THE WIRE'S NODE AND THE ENGINE'S — the client's one seam between them. The wire's node IS the engine's (OES-RPC: a
// packet is ibDataNode's bytes), so the client reads it as it lies — one more owner of the answer's bytes — a type kept
// as a word looked up as the server looks it up. Written back entry by entry, by the JSON view's names (NodeType,
// NodeId, NodeChildren the node's own), the server reads what it wrote.

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
