#ifndef __PROTOCOL_CONNECTION_H__
#define __PROTOCOL_CONNECTION_H__

#include <functional>
#include <memory>
#include <string>

#include <wx/string.h>

#include "core/memory/memoryBuffer.h"   // ibMemoryBuffer — the packets a connection carries

#include "protocol/protocol.h"
#include "protocol/protocolNode.h"      // ibProtocolNode — the node a packet that came is

#include "protocol/protocolApi.h"

// A CONNECTION TO A BASE'S CLIENTS — packets, as the port speaks them (OES-RPC 1.0, core/serialize/dataBuilder.h):
// one request in, its answer out — the node the answer's packet is (ibProtocolNode::Read), checked ONCE, where it came
// in, and read where it lies; false when the request did not get there, no answer came back or what came is no packet,
// with why. How it gets there is the connection's: a file base is opened in this very process and handed the packet
// itself (ibProtocolConnectionFile) — nothing converted, one more owner of its bytes — and an application server is
// sent the bytes over its port (ibProtocolConnectionServer). Above it nothing differs — the communicator draws a file
// base and a server alike, from the same bytes.
class PROTOCOL_API ibProtocolConnection {
public:
	virtual ~ibProtocolConnection() = default;

	virtual bool Exchange(const ibMemoryBuffer& request, ibProtocolNode& answer,
		ibProtocol::ibRefusal& refusal, ibString& error) = 0;

	// Open this connection again after it has closed. False when there is nowhere to return to — a file base has
	// no socket. The default is false, so a connection that cannot return does not pretend to.
	virtual bool Reconnect(ibString& error) { (void)error; return false; }

	// A SECOND CONNECTION TO THE SAME PLACE (protocol 6) — the debugger's, so what it carries is not queued behind this
	// one's calls: the same server's address, the same file base. Null, with why in `error`, when there is none — the
	// default, so a connection that cannot open one does not pretend to.
	virtual std::unique_ptr<ibProtocolConnection> OpenChannel(ibString& error)
	{
		error = ibT("this connection opens no second one");
		return nullptr;
	}

	// WHAT THE SERVER SAYS UNASKED — a notification (`changed {Client}`: a client's frame changed by itself), handed to
	// `notified` as the node its packet is, on the connection's thread, not the window's; empty — told nothing.
	virtual void Listen(std::function<void(const ibProtocolNode& said)> notified) = 0;
};

#endif
