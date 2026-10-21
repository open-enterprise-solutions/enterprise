#ifndef __PROTOCOL_CONNECTION_H__
#define __PROTOCOL_CONNECTION_H__

#include <functional>
#include <memory>
#include <string>

#include <wx/string.h>

#include "core/fstring.h"   // ibString — the text a connection carries

#include "protocol/protocol.h"

#include "protocol/protocolApi.h"

// A CONNECTION TO A BASE'S CLIENTS — text, as the port speaks it: one JSON-RPC request in, its answer out; false when
// the request did not get there or no answer came back, with why. The text is the engine's both ways (ibString). How
// it gets there is the connection's: a file base is opened in this very process and handed the text itself
// (ibProtocolConnectionFile) — nothing converted, one more owner of it — and an application server is sent its
// bytes over its port (ibProtocolConnectionServer). Above it nothing differs — the communicator draws a file base and
// a server alike, from the same text.
class PROTOCOL_API ibProtocolConnection {
public:
	virtual ~ibProtocolConnection() = default;

	virtual bool Exchange(const ibString& request, ibString& answer,
		ibProtocol::ibRefusal& refusal, wxString& error) = 0;

	// Open this connection again after it has closed. False when there is nowhere to return to — a file base has
	// no socket. The default is false, so a connection that cannot return does not pretend to.
	virtual bool Reconnect(wxString& error) { (void)error; return false; }

	// A SECOND CONNECTION TO THE SAME PLACE (protocol 6) — the debugger's, so what it carries is not queued behind this
	// one's calls: the same server's address, the same file base. Null, with why in `error`, when there is none — the
	// default, so a connection that cannot open one does not pretend to.
	virtual std::unique_ptr<ibProtocolConnection> OpenChannel(wxString& error)
	{
		error = wxT("this connection opens no second one");
		return nullptr;
	}

	// WHAT THE SERVER SAYS UNASKED — a JSON-RPC notification (`changed {Client}`: a client's frame changed by itself),
	// handed to `notified` as text on the connection's thread, not the window's; empty — told nothing.
	virtual void Listen(std::function<void(const ibString& text)> notified) = 0;
};

#endif
