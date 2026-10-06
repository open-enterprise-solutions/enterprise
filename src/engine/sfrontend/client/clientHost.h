#ifndef __CLIENT_HOST_H__
#define __CLIENT_HOST_H__

// THE CLIENTS OF ONE BASE — every client instance working with it through the protocol, by id, and the
// protocol's door into them. Beside the base's own instance, as ibApplicationHost holds the instances of the
// bases: one per base the process serves.
//
// The door is Call — the method, its parameters, the answer; no transport in it. A client in this same process (a
// file base's program, the assistant's MCP tool) calls it as it is, the nodes passing as they are; a transport (the
// port's JSON-RPC, the same envelope MCP speaks: backend/rpc) reads them off its wire and writes the answer back
// on it, the second Call. The work a request asks for is the client's session's, so it goes to that session's worker (ibSession::Submit)
// and the caller waits for it to SETTLE — done, or the person asked something (a request to the client is
// pending: the work goes on waiting for the response, which comes in by another call). Whatever the call, the
// response carries the client's frame as it stands after it.
//
// A round once a second takes down the instances that asked to go and those idle too long, and hands each
// session the forms' due idle handlers.

#include <condition_variable>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include <wx/string.h>

#include "backend/serialize/dataBuilder.h"   // ibDataNode — the frame a client was last answered, kept

#include "sfrontend/sfrontend.h"

#include "clientMethod.h"   // ibClientMethod — what a call is

class ibApplicationInstance;
class ibClientInstance;
class ibClientFrame;

class SFRONTEND_API ibClientHost {
public:

	explicit ibClientHost(ibApplicationInstance* applicationInstance);
	~ibClientHost();

	// THE CALL — the method, its parameters, the answer into `result`; false with why it was refused in `refusal`
	// (what a client branches on) and the text in `error`.
	// `connection` — the transport's connection it came by, if it holds one (a WebSocket): the clients logged in
	// through it are its own and go when it closes (Disconnect); `address` — the computer it comes from, the one
	// the client's session is told. A client in this process passes neither.
	// The methods and their parameters: ibClientMethod (clientMethod.h).
	// Every response's frame: Title, Status, ActiveTab (a tab's id), Tabs (NodeId — the tab's id, Title, Icon,
	// Locked), Request (a pending one: Id and the request), View (the active tab's view drawn: ibView::OnDraw) —
	// or, to a call naming the frame it holds (Since), the Patch to it (clientPatch.h); beside either the events,
	// Messages (Level, Text) and Clear, and the answer's number, Frame.
	bool Call(ibClientMethod method, const ibDataNode& params, ibDataNode& result, ibClientRefusal& refusal,
		wxString& error, const void* connection = nullptr, const wxString& address = wxEmptyString);

	// THE PORT'S CALL — one JSON-RPC request as text in, its response as text out (empty for a notification,
	// which wants none): the envelope read and written around the call above.
	wxString Call(const wxString& request, const void* connection = nullptr, const wxString& address = wxEmptyString);

	// The connection closed: the clients logged in through it are logged out.
	void Disconnect(const void* connection);

	// The host serving this base — what a door outside the library (an MCP tool) reaches the clients by.
	static ibClientHost* Find(const ibApplicationInstance* applicationInstance);

private:

	struct Client {
		std::shared_ptr<ibClientInstance> instance;
		// The connection it was logged in through — null when the door had none.
		const void*                       connection = nullptr;
		// The work that has not settled yet — waiting for the person's response to a request. The next call
		// that settles the client waits for it too: a response lets it go on.
		std::shared_future<void>          unsettled;
		// One call of a client at a time: a call that leaves its work waiting for a response has returned by
		// then, so the call carrying the response gets in.
		std::mutex                        lock;
		// The last frame answered — its number and itself, as sent: a call naming that number (Since) is answered
		// with the patch from it.
		s32                               frame = 0;
		ibDataNode                        sent;
	};

	std::shared_ptr<Client> FindClient(const wxString& id) const;
	void                    RemoveClient(const wxString& id);

	// The work on the client's session worker, waited for until it settles; then the frame drawn into `result` —
	// numbered, and the patch from the frame of number `since` when that is the last one the client was answered.
	bool Run(Client& client, std::function<void(ibClientFrame*)> work, s32 since, ibDataNode& result,
		ibClientRefusal& refusal, wxString& error);
	void Settle(Client& client);
	// The frame: what it is (`state`, what a patch is made of) and what happened since the last answer (`events`:
	// the messages, a clear) — drawn apart.
	static void DrawFrame(ibClientFrame* frame, ibDataNode& state, ibDataNode& events);

	void RoundBody();

	ibApplicationInstance* m_applicationInstance;

	mutable std::mutex                             m_mutex;
	std::map<wxString, std::shared_ptr<Client>>    m_clients;

	std::mutex              m_roundMutex;
	std::condition_variable m_roundSignal;
	bool                    m_stop = false;
	std::thread             m_round;
};

#endif
