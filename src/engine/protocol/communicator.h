#ifndef __PROTOCOL_COMMUNICATOR_H__
#define __PROTOCOL_COMMUNICATOR_H__

#include <map>
#include <memory>
#include <mutex>
#include <string>

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
	bool Login(const wxString& user, const wxString& password, ibProtocolMode mode, ibProtocolNode& result,
		ibProtocolRefusal& refusal, wxString& error);
	void Logout();

	// Any other call, the client and the frame held named for it (Client, Since). An answer that is a frame (it
	// carries its number, Frame) is taken into the one kept; the answer is given back as it came — the events of a
	// frame are read from it, and so are a fetch's rows, a schema's tree, a file's part.
	bool Call(ibProtocolMethod method, const ibProtocolNode& params, ibProtocolNode& result,
		ibProtocolRefusal& refusal, wxString& error);

	// WHAT THE SERVER SAYS UNASKED of this client — its frame changed by itself (`changed {Client}`): `changed` is
	// called on the connection's thread, never the window's, and the client asks for its frame. Empty — told nothing.
	void Listen(std::function<void()> changed);

	bool            IsLoggedIn() const { return !m_client.IsEmpty(); }
	const wxString& GetClient() const { return m_client; }
	int             GetProtocol() const { return m_protocol; }

	// The frame drawn now, and its number — the last answer's.
	const ibProtocolNode& GetFrame() const { return m_frame; }
	long long             GetFrameNumber() const { return m_number; }

private:

	bool CallLocked(ibProtocolMethod method, const ibProtocolNode& params, ibProtocolNode& result,
		ibProtocolRefusal& refusal, wxString& error);
	// The socket dropped and the server offered to resume: open it again, login {Token} under a new id, and send
	// `sent` again under the id it already carries. False — the session is gone, or this connection cannot return.
	bool Resume(const std::string& sent, std::string& answer, ibProtocolRefusal& refusal, wxString& error);
	// The answer's frame, whole or as a patch, taken into the one kept; an answer that is no frame leaves it.
	void Take(const ibProtocolNode& answer);
	// The client is gone (logged out, or its session is): nothing of it is kept.
	void Forget();

	std::unique_ptr<ibProtocolConnection> m_connection;
	std::mutex                            m_mutex;
	// Never put back to 0. A dropped socket is the same communicator, and the next call's id is newer than every
	// id already used, including across the login {Token} that brings the session back.
	long long                             m_lastId = 0;

	wxString       m_client;
	wxString       m_token;    // the bearer, kept here. The server stores only its hash.
	bool           m_resume = false;   // the server listed `resume` — a dropped socket is asked for again
	ibProtocolMode m_mode = ibProtocolMode::Runtime;
	int            m_protocol = 1;
	long long      m_number = 0;
	ibProtocolNode m_frame;
	// (protocol 2) the view last answered for each tab NOT active now, by the tab's id — what a patch to that tab is
	// applied to when it becomes active; the active tab's is the frame's own View. Moved, never copied.
	std::map<long long, ibProtocolNode> m_views;
};

#endif
