#ifndef __PROTOCOL_CONNECTION_H__
#define __PROTOCOL_CONNECTION_H__

#include <functional>
#include <string>

#include <wx/string.h>

#include "protocol/protocol.h"

#include "protocol/protocolApi.h"

// A CONNECTION TO A BASE'S CLIENTS — text, as the port speaks it: one JSON-RPC request in, its answer out, UTF-8 both
// ways; false when the request did not get there or no answer came back, with why. How the text gets there is the
// connection's: a file base is opened in this very process and handed the text (ibProtocolConnectionFile), an
// application server is sent it over its port (ibProtocolConnectionServer). Above it nothing differs — the
// communicator draws a file base and a server alike, from the same text.
class PROTOCOL_API ibProtocolConnection {
public:
	virtual ~ibProtocolConnection() = default;

	virtual bool Exchange(const std::string& request, std::string& answer,
		ibProtocolRefusal& refusal, wxString& error) = 0;

	// Open this connection again after it has closed. False when there is nowhere to return to — a file base has
	// no socket. The default is false, so a connection that cannot return does not pretend to.
	virtual bool Reconnect(wxString& error) { (void)error; return false; }

	// WHAT THE SERVER SAYS UNASKED — a JSON-RPC notification (`changed {Client}`: a client's frame changed by itself),
	// handed to `notified` as text on the connection's thread, not the window's; empty — told nothing.
	virtual void Listen(std::function<void(const std::string& text)> notified) = 0;
};

#endif
