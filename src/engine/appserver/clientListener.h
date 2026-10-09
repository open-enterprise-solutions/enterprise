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
// stream — the frames that follow carry no headers. One port for all of it, and TLS, when it comes, on it.

#include <map>
#include <memory>
#include <thread>

#include <wx/string.h>

class ibClientListener {
public:

	ibClientListener();
	~ibClientListener();

	// Bound on the caller's thread, so a refusal is said here: `host` (empty — every interface) at `port`, or —
	// `choose` — at the first free one from `port` up, which `port` then holds. `hosts` — the bases served, by
	// name. False, with the reason, when nothing could be bound.
	bool Start(const wxString& host, unsigned short& port, bool choose,
		std::map<wxString, class ibClientHost*> hosts, wxString& refusal);

	// The port closed and every connection let go — before the hosts it carries to are.
	void Stop();

private:

	// cpp-httplib, kept in one translation unit.
	class ibServer;

	std::unique_ptr<ibServer> m_server;
	std::thread               m_thread;
};

// The next `count` client threads are refused as the operating system would refuse to create them.
// The listener answers false and the socket is closed, instead of the exception leaving the accept loop.
// The server does not call this; a test does, after Start, so the base threads are already there.
void ibClientListenerFailNextThreads(unsigned count);

// While `hold` is set, an idle pool thread does not take a job. A test parks the pool between two
// arrivals. The server does not call this.
void ibClientListenerHoldIdleThreads(bool hold);

#endif
