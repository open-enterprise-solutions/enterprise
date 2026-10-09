#ifndef _PROTOCOL_API_H__
#define _PROTOCOL_API_H__

#include <wx/defs.h>

// THE PROTOCOL'S LIBRARY — how a side that speaks the protocol talks: a node of the wire (protocolNode.h), the
// connections to a base's clients — a file base opened in this process, an application server over its port — the
// communicator that keeps the frame it was answered, and the protocol's own journal. The GUI client (frmclient) and the
// web server link it; the server's forms include only the contract (protocol.h), which depends on nothing.
#if defined(PROTOCOL_EXPORTS)
#define PROTOCOL_API WXEXPORT
#else
#define PROTOCOL_API WXIMPORT
#endif

#endif
