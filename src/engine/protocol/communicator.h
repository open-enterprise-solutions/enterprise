#ifndef __PROTOCOL_COMMUNICATOR_H__
#define __PROTOCOL_COMMUNICATOR_H__

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "connection.h"
#include "protocolNode.h"

// THE COMMUNICATOR — the client's side of the protocol, one for the thin runtime and the thin designer, for a file
// base and a server alike (the connection says which; both speak the same text). It writes each call's envelope
// (JSON-RPC) with the client and the frame it holds named in it, reads the answer back, and keeps the frame it was
// answered: a frame sent whole is kept as it is, a patch is applied to the one kept, and (protocol 2) each tab's view
// is kept the way the server keeps it, so a tab's patch is applied to its own view (docs/public/client-protocol.md,
// The frame). What it keeps is what the person sees — the main window draws it; the events of an answer (Messages,
// Clear) are not kept, they are read from the answer.
//
// What it keeps and hands out is the wire's tree, read through ibProtocolNode.
//
// One call at a time: a second waits for the first. The frame is read between calls.
class PROTOCOL_API ibCommunicator {
public:

	explicit ibCommunicator(std::unique_ptr<ibProtocolConnection> connection);
	// Logged out, when it was logged in.
	~ibCommunicator();

	// login {User, Password, Mode, Protocol} — the client's id and the protocol both speak taken, the token when the
	// server offers one, and the first frame; the answer given back as it came, for its events (a start may say
	// something, or ask).
	// `schemas` — those the client would read right after it (Read): answered beside the frame, each with its Schema; one
	// the person may not read is left out, and the client asks for it.
	bool Login(const wxString& user, const wxString& password, ibProtocol::ibLogin::ibMode mode, ibProtocolNode& result,
		ibProtocol::ibRefusal& refusal, wxString& error, const std::vector<ibProtocol::ibSchema::ibKind>& schemas = {});
	// users {} — the names the login window offers, asked before the login.
	bool Users(std::vector<wxString>& names, ibProtocol::ibRefusal& refusal, wxString& error);
	void Logout();

	// Any other call, the client and the frame held named for it (Client, Since). An answer that is a frame (it
	// carries its number, Frame) is taken into the one kept; the answer is given back as it came — the events of a
	// frame are read from it, and so are a fetch's rows, a schema's tree, a file's part.
	bool Call(ibProtocol::ibMethod method, const ibProtocolNode& params, ibProtocolNode& result,
		ibProtocol::ibRefusal& refusal, wxString& error);
	// ⭐ A STATE EVENT LEFT FOR THE NEXT CALL — an event's params (protocol 3, ibProtocol::ibEvent::IsStateEvent): kept, the latest of
	// its kind per control in place of the one before, and carried by the next call, whatever it is, as Pending, in the
	// order done. False — the server speaks no protocol that carries it: the caller sends it as its own call.
	bool Hold(const ibProtocolNode& event);

	// WHAT THE SERVER SAYS UNASKED of this client — its frame changed by itself (`changed {Client}`): `changed` is
	// called on the connection's thread, never the window's, and the client asks for its frame. Empty — told nothing.
	void Listen(std::function<void()> changed);

	// ⭐ THE DEBUGGER, ON A CONNECTION OF ITS OWN (protocol 6) — a second one opened to the same place
	// (ibProtocolConnection::OpenChannel) and bound to this client by the key debug {Channel} asks for on this one; `told`
	// is handed the params of each `debug` notification of this client, on that connection's thread. Asked right after
	// the login: the key is asked on this connection, and a call in flight would hold it up. False with why — a server
	// below 6, a client with no debugger. Empty `told` — that connection closed, nothing more told.
	bool ListenDebug(std::function<void(const ibProtocolNode& news)> told, ibProtocol::ibRefusal& refusal, wxString& error);
	// debug {Command, …} on that connection — answered at once, beside this one's calls, never with a frame. False — none
	// is bound, or refused.
	bool Debug(const ibProtocolNode& params, ibProtocolNode& result, ibProtocol::ibRefusal& refusal, wxString& error);

	bool            IsLoggedIn() const { return !m_client.IsEmpty(); }
	const ibString& GetClient() const { return m_client; }
	int             GetProtocol() const { return m_protocol; }

	// The frame drawn now, and its number — the last answer's.
	const ibProtocolNode& GetFrame() const { return m_frame; }
	long long             GetFrameNumber() const { return m_number; }

private:

	bool CallLocked(ibProtocol::ibMethod method, const ibProtocolNode& params, ibProtocolNode& result,
		ibProtocol::ibRefusal& refusal, wxString& error);
	// The socket dropped and the server offered to resume: open it again, login {Token} under a new id, and send
	// `sent` again under the id it already carries. False — the session is gone, or this connection cannot return.
	bool Resume(const ibString& sent, ibString& answer, ibProtocol::ibRefusal& refusal, wxString& error);
	// The answer's frame, whole or as a patch, taken into the one kept; an answer that is no frame leaves it. A whole one
	// is taken OUT of the answer — what it carries beside the frame stays in it.
	void Take(ibProtocolNode& answer);
	// The client is gone (logged out, or its session is): nothing of it is kept.
	void Forget();
	// debug {…} on the debugger's connection, under m_debugMutex.
	bool DebugLocked(const ibProtocolNode& params, ibProtocolNode& result, ibProtocol::ibRefusal& refusal, wxString& error);

	std::unique_ptr<ibProtocolConnection> m_connection;
	std::mutex                            m_mutex;
	// Never put back to 0. A dropped socket is the same communicator, and the next call's id is newer than every
	// id already used, including across the login {Token} that brings the session back.
	long long                             m_lastId = 0;

	ibString       m_client;
	ibString       m_token;    // the bearer, kept here. The server stores only its hash.
	bool           m_resume = false;   // the server listed `resume` — a dropped socket is asked for again
	ibProtocol::ibLogin::ibMode m_mode = ibProtocol::ibLogin::ibMode::Runtime;
	std::atomic<int> m_protocol{ 1 };   // read by Hold on the window's thread while a fetch may be in a call
	long long      m_number = 0;
	ibProtocolNode m_frame;
	// (protocol 2) the view last answered for each tab NOT active now, by the tab's id — what a patch to that tab is
	// applied to when it becomes active; the active tab's is the frame's own View. Moved, never copied.
	std::map<long long, ibProtocolNode> m_views;

	// (protocol 3) the state events held for the next call, in the order done (Hold). A lock of their own: Hold is the
	// window's, and must not wait for a fetch's exchange.
	std::mutex                  m_pendingMutex;
	std::vector<ibProtocolNode> m_pending;

	// (protocol 6) THE DEBUGGER'S CONNECTION (ListenDebug), the client it is bound for, and its calls' ids — a lock of
	// their own: a step does not wait for this connection's call. Taken after m_mutex, never before it. Last: it goes
	// before the connection it was opened beside.
	std::mutex                            m_debugMutex;
	ibString                              m_debugClient;
	long long                             m_debugLastId = 0;
	std::unique_ptr<ibProtocolConnection> m_debugConnection;
};

#endif
