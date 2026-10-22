#ifndef __APP_CLIENT_LISTENER_H__
#define __APP_CLIENT_LISTENER_H__

// THE PORT THE CLIENTS COME IN BY — one per application server, for every base it serves; the base is named in
// the path. The protocol is frmserver's (ibClientHost::Call); this only carries it:
//
//   WS   /<base>/client    a connection: each text message one JSON-RPC request, answered by its response — and
//                          the clients logged in through it go when it closes
//   POST /<base>/client    one request, its response — for a tool, a test, curl
//
// HTTP only opens the door: a browser speaks nothing else, and a WebSocket is an HTTP request that became a
// stream — the frames that follow carry no headers. One port for all of it, TLS on it when backend.conf names a
// certificate (oess://), plain on this machine's own address otherwise (core/server/listener.h).
//
// The HTTP is the core's (core/net/httpServer.h): asynchronous, so a connection waiting for its client costs a buffer,
// not a thread.

#include <map>
#include <memory>

#include <wx/string.h>

#include "core/net/httpServer.h"
#include "core/server/listener.h"

class ibClientListener {
public:

	ibClientListener();
	~ibClientListener();

	// Bound on the caller's thread, so a refusal is said here: `host` (empty — every interface) at `port`, or —
	// `choose` — at the first free one from `port` up, which `port` then holds; over `tls` (ibListenerOptions).
	// `hosts` — the bases served, by name. False, with the reason, when nothing could be bound.
	bool Start(const wxString& host, unsigned short& port, bool choose, const ibTlsFiles& tls,
		std::map<wxString, class ibClientHost*> hosts, wxString& refusal);

	// The port closed and every connection let go — before the hosts it carries to are.
	void Stop();

private:

	// The fifteen seconds a socket has to log in (clientListener.cpp).
	class ibLoginRound;

	std::unique_ptr<ibHttpServer> m_server;
	std::unique_ptr<ibLoginRound> m_loginRound;
};

#endif
