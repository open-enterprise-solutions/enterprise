#ifndef __CLIENT_HOST_H__
#define __CLIENT_HOST_H__

// THE CLIENTS OF ONE BASE — every client instance working with it through the protocol, by id, and the
// protocol's door into them. Beside the base's own instance, as ibApplicationHost holds the instances of the
// bases: one per base the process serves.
//
// The door is Call — the method, its parameters, the answer; no transport in it. A client in this same process (a
// file base's program, the assistant's MCP tool) calls it as it is, the nodes passing as they are; a transport (the
// port's packets — OES-RPC, the JSON-RPC envelope as a node: backend/rpc) hands them over as they are and is handed the
// answer back, the second Call. The work a request asks for is the client's session's, so it goes to that session's worker (ibSession::Submit)
// and the caller waits for it to SETTLE — done, or the person asked something (a request to the client is
// pending: the work goes on waiting for the response, which comes in by another call). Whatever the call, the
// response carries the client's frame as it stands after it.
//
// A round once a second takes down the instances that asked to go and those idle too long, and hands each
// session the forms' due idle handlers.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <wx/string.h>

#include "core/serialize/dataBuilder.h"   // ibDataNode — the frame a client was last answered, kept

#include "frmserver/frmserver.h"

#include "protocol/protocol.h"   // ibProtocol::ibMethod — what a call is

class ibApplicationInstance;
class ibClientInstance;
class ibClientFrame;
struct ibRpcRequest;

class FRMSERVER_API ibClientHost {
public:

	// `route` — which of its base's servers this process is (appserver --route): the tokens it hands out end in it, so the
	// cluster carries a client that comes back to this one (ibSessionToken::WithRoute). 0 — the base's only one.
	explicit ibClientHost(ibApplicationInstance* applicationInstance, unsigned route = 0);
	~ibClientHost();

	// THE CALL — the method, its parameters, the answer into `result`; false with why it was refused in `refusal`
	// (what a client branches on) and the text in `error`.
	// `connection` — the transport's connection it came by, if it holds one (a WebSocket): the clients logged in
	// through it are its own and go when it closes (Disconnect); `address` — the computer it comes from, the one
	// the client's session is told. A client in this process passes neither.
	// The methods and their parameters: ibProtocol::ibMethod (protocol/protocol.h).
	// Every response's frame: Title, Status, ActiveTab (a tab's id), Tabs (NodeId — the tab's id, Title, Icon,
	// Locked), Request (a pending one: Id and the request), View (the active tab's view drawn: ibView::OnDraw) —
	// or, to a call naming the frame it holds (Since), the Patch to it (clientPatch.h); beside either the events,
	// Messages (Level, Text) and Clear, and the answer's number, Frame.
	bool Call(ibProtocol::ibMethod method, const ibDataNode& params, ibDataNode& result, ibProtocol::ibRefusal& refusal,
		ibString& error, const void* connection = nullptr, const ibString& address = ibString());

	// THE PORT'S CALL — one request as a packet in (OES-RPC 1.0, serialize/dataBuilder.h), its answer as a packet out
	// (empty for a notification, which wants none): the envelope read and written around the call above. Nothing is
	// parsed or encoded — the request is read where its bytes lie, the answer is the bytes it was written into, and a
	// client in this process takes them as they are (one more owner of them).
	ibMemoryBuffer Call(const ibMemoryBuffer& request, const void* connection = nullptr, const ibString& address = ibString());
	// …and as JSON-RPC text — an HTTP POST's: the same call, the envelope read from the text and written back as one.
	ibString Call(const ibString& request, const void* connection = nullptr, const ibString& address = ibString());

	// The connection closed: the clients logged in through it are logged out.
	void Disconnect(const void* connection);

	// True when a client has logged in through this connection, or its debugger listens on it (protocol 6). The
	// listener uses it to tell a socket that is only holding a thread from one the half-hour round already watches.
	bool LoggedIn(const void* connection) const;

	// HOW A CONNECTION IS TOLD SOMETHING — a transport that keeps its connection open (a WebSocket, a file base's
	// program) hands the host the way it sends a packet to it; Disconnect forgets it. A connection without one (an
	// HTTP request) is never told anything: it asks.
	void SetNotifier(const void* connection, std::function<void(const ibMemoryBuffer& packet)> notify);

	// THE CLIENT'S FRAME CHANGED BY ITSELF — work it did not call settled (a report composed in the background, a
	// debugger stopping): its connection is sent a notification, `changed {Client}`, and the client asks for its frame.
	// A client whose connection has no notifier is not told.
	void NotifyChanged(const ibClientInstance* instance);
	// …and THE CLIENT'S DEBUGGER SAID SOMETHING (protocol 6) — a stop, the stack, a value asked for: sent as it is, `debug
	// {Client, Event, …}`, to the connection its debugger listens on (ibProtocol::ibDebug). None listening — not told.
	void NotifyDebug(const ibClientInstance* instance, const ibDataNode& event);

	// The host serving this base — what a door outside the library (an MCP tool) reaches the clients by.
	static ibClientHost* Find(const ibApplicationInstance* applicationInstance);

private:

	struct Client {
		std::shared_ptr<ibClientInstance> instance;
		// The connection it was logged in through. Null when the door had none, or after a thin client's
		// socket has dropped. Atomic: Disconnect and a token login write it under m_mutex, and a call reads
		// it under the client's own lock.
		std::atomic<const void*>          connection{ nullptr };
		// Set at login when the door had no connection, and never changed after. A null connection is also
		// a thin client in the middle of a drop, so the two are not the same fact.
		bool                              connectionless = false;
		// The work that has not settled yet — waiting for the person's response to a request. The next call
		// that settles the client waits for it too: a response lets it go on.
		std::shared_future<void>          unsettled;
		// Set while a call is being answered and its frame is not drawn yet: the answer carries what changed, so the
		// work's own changes are not told (NotifyChanged) as well — the client would ask for an empty frame. Cleared
		// on the worker right before the frame is drawn, so a change made after the draw is still told.
		std::atomic<bool>                 answering{ false };
		// One call of a client at a time: a call that leaves its work waiting for a response has returned by
		// then, so the call carrying the response gets in.
		std::mutex                        lock;
		// The last call this client answered, and only when it opted in (`dedupe`). The id, a digest of the
		// method and the params, and the answer written back, including a refusal. One slot for this client.
		// A login by token does not replace it. Under `lock`. `replyPending` is set by the call that owns the
		// slot and cleared only by that call; `pendingId` is that call's id.
		bool                              dedupe = false;
		bool                              hasReply = false;
		ibDataValue                       replyId;
		unsigned char                     replyDigest[32] = {};
		ibDataNode                        reply;
		bool                              replyPending = false;
		ibDataValue                       pendingId;
		std::condition_variable           replyWait;
		// The last frame answered — its number and itself, as sent: a call naming that number (Since) is answered
		// with the patch from it.
		std::int32_t                               frame = 0;
		ibDataNode                        sent;
		// The version of the protocol it speaks (the older of its and ours) — and, from 2, the view it was last sent
		// for each tab: a tab's view is patched from its own (Run).
		std::int32_t                         protocol = 1;
		std::map<std::int32_t, ibDataValue>  views;
		// SHA-256 of the bearer token. The token itself is not kept. Under m_mutex, with hasToken.
		bool                              hasToken = false;
		unsigned char                     tokenDigest[32] = {};
		// THE DEBUGGER'S CONNECTION (protocol 6) — a second one of this client's, bound by the key Channel drew: its SHA-256
		// until Listen takes it, under m_mutex with hasChannelKey; then the connection — what `debug` news goes to, and
		// what may ask the debugger beside the login one. Atomic: a call reads it without m_mutex.
		bool                              hasChannelKey = false;
		unsigned char                     channelDigest[32] = {};
		std::atomic<const void*>          debugConnection{ nullptr };
		// Armed when a thin client's connection has dropped: the moment the session ends if nobody returns.
		// 0 — a connection is attached, or this client never had one (an HTTP call, `connectionless`). Stored before
		// `connection` is cleared, so a call never observes a null connection together with a zero deadline.
		std::atomic<std::int64_t>         resumeDeadlineMs{ 0 };
	};

	// THE CALL IN ITS ENVELOPE — what both of the port's doors read their request into and write their answer from: the
	// answer as a node, its result filled where it lies; none for a notification. `size` — how long the request was.
	std::optional<ibDataNode> Call(const ibRpcRequest& request, const void* connection, const ibString& address,
		std::int64_t read, std::size_t size);

	std::shared_ptr<Client> FindClient(const ibString& id) const;
	std::shared_ptr<Client> FindByDigest(const unsigned char* digest) const;
	void                    RemoveClient(const ibString& id);
	// login {Token} and no User — the same client, no frame drawn.
	bool                    ResumeByToken(const ibDataNode& params, ibDataNode& result, ibProtocol::ibRefusal& refusal,
		ibString& error, const void* connection);
	// The password login's bearer: drawn, hashed into sys_session, digest kept, plaintext put on `result` once.
	bool                    RememberToken(Client& client, ibDataNode& result, ibProtocol::ibRefusal& refusal, ibString& error);
	// debug {Command} — the client's debugger, answered at once (ibProtocol::ibDebug): Channel and Listen bind its
	// connection here, the rest are its frame's debugger's (ibClientDebugger::Command).
	bool                    Debug(Client& client, const ibDataNode& params, ibDataNode& result, ibProtocol::ibRefusal& refusal,
		ibString& error, const void* connection);

	// The work on the client's session worker, waited for until it settles; then the frame drawn into `result` —
	// numbered, and the patch from the frame of number `since` when that is the last one the client was answered.
	bool Run(Client& client, std::function<void(ibClientFrame*)> work, std::int32_t since, ibDataNode& result,
		ibProtocol::ibRefusal& refusal, ibString& error);
	void Settle(Client& client);
	// The frame: what it is (`state`, what a patch is made of) and what happened since the last answer (`events`:
	// the messages, a clear) — drawn apart.
	static void DrawFrame(ibClientFrame* frame, ibDataNode& state, ibDataNode& events);

	void RoundBody();

	ibApplicationInstance* m_applicationInstance;
	unsigned               m_route;

	mutable std::mutex                             m_mutex;
	std::map<ibString, std::shared_ptr<Client>>    m_clients;
	// How each connection is told something (SetNotifier), under m_mutex.
	std::map<const void*, std::function<void(const ibMemoryBuffer& packet)>> m_notifiers;

	std::mutex              m_roundMutex;
	std::condition_variable m_roundSignal;
	bool                    m_stop = false;
	std::thread             m_round;
};

#endif
