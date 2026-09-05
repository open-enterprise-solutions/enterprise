////////////////////////////////////////////////////////////////////////////
//	Description : the MCP server — one listener, one exchange at a time
////////////////////////////////////////////////////////////////////////////
//
// ⭐ THE TRANSPORT IS cpp-httplib, not a hand-written one.
//
// It used to be ~230 lines over wxSocketServer: an accept loop polling every
// kPollMs so it could notice a stop flag, headers read a BYTE AT A TIME until
// the blank line, Content-Length parsed by hand, and the SSE stream held as a
// raw socket written to from other threads under a mutex.
//
// None of that was wrong; all of it was already solved. cpp-httplib is ALREADY
// in this tree (src/3rdparty/cpp-httplib) and already in production under
// wenterprise-server and wfrontend — MCP was the only thing that wrote its own
// instead of taking what was there.
//
// ⭐ AND IT IS THE ROAD TO TLS. Certificates over a raw socket are a project;
// here they are CPPHTTPLIB_OPENSSL_SUPPORT and a cert path, and the day that
// dependency is brought in it serves BOTH servers — the web one will want it
// first, being the one that faces outward. Nothing here is built for that yet;
// this change only stops standing in its way.
//
// Two things genuinely changed shape:
//
//   • THE STREAM INVERTED. httplib calls a content provider; the old code held
//     the socket and wrote INTO it from wherever. So the way out became a queue
//     with a condition variable — which also removes a race the old one had,
//     between asking IsConnected() and writing.
//
//   • THE SESSION IS BOUND PER REQUEST. There is no longer one thread to bind
//     once: httplib serves from a pool, so the binding that makes metadata and
//     rights answer as the developer happens around each call.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpServer.h"

#include "backend/appData.h"                        // GetSettingsStorage — where a person's settings live
#include "backend/logger/logger.h"                  // …and ibLog — the registration journal
#include "backend/diagnostics/journal.h"            // ibJournalInfo — the ENGINE's journal, `mcp` channel
#include "backend/backend_exception.h"              // an engine refusal arrives as an exception
#include "backend/query/queryException.h"           // …and the query family says WHERE
#include "backend/debugger/debugClient.h"           // the bridge list — we ride along on a session
#include "backend/mcp/mcpDebugBridge.h"
#include "backend/mcp/mcpMessage.h"
#include "backend/mcp/mcpTool.h"
#include "backend/metaCollection/metaIntrospect.h"  // ibMetaTypeResolver
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "backend/session/workerPool.h"             // the queue that reaches the main thread
#include "backend/settings/settingsStorage.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

// The transport. Header-only and large, so it is included in exactly this one
// translation unit — the same way wenterprise-server includes it in main.cpp.
//
// ⚠ AND AFTER wx, WITH THE GUARD ALREADY SET. wx aliases ssize_t and so does
// cpp-httplib; MSVC then sees two incompatible definitions and refuses
// (C2371 at httplib.h:233). The header's own MSVC guard is _SSIZE_T_DEFINED, so
// declaring it here tells httplib the alias is already made. This is not a
// discovery — wenterprise-server/main.cpp hit it first and wrote the cure down;
// it is copied rather than re-derived.
#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#	define _SSIZE_T_DEFINED
#endif

#include "httplib.h"

namespace {

// The protocol revision this server speaks. Announced in `initialize`; a client
// that wants another one is told what we have rather than being guessed at.
// WHAT THIS SERVER SPEAKS — and it is what it has been speaking for a while, said out loud at
// last. The constant read `2024-11-05` until 2026-09-05, which is the FIRST MCP revision and the
// deprecated HTTP+SSE transport; the server has meanwhile been Streamable HTTP all along — one
// endpoint, POST answered as JSON or as an SSE stream, GET offering a listen stream, server-sent
// requests on it for `sampling/createMessage`. That is the 2025-03-26 … 2025-11-25 shape exactly,
// and the last of those is the one to name: sessions are OPTIONAL there (this server is
// deliberately stateless), resumability is optional, and a server MAY send requests on a stream —
// so nothing here is claimed that is not done.
//
// ⚠ NOT the current revision, and that is a decision rather than an oversight. 2026-07-28 removed
// the GET stream and protocol-level sessions and moved server-to-client interaction to MRTR
// (the server returns an InputRequiredResult and the client re-calls) — which is a rebuild of how
// the assistant window asks the model anything, not a version bump. Naming 2025-11-25 states the
// truth today; the dual-era work is its own piece.
const wxChar* const kProtocolVersion = wxT("2025-11-25");

// The revisions a caller may name and still be served. They differ, on THIS server's side, only
// in what the client is obliged to send — the header below arrived in 2025-06-18 — so one
// implementation answers all three honestly. A version outside this list is refused rather than
// answered as if it were ours: see IsProtocolVersionAcceptable.
const wxChar* const kSupportedProtocolVersions[] = {
	wxT("2025-11-25"),
	wxT("2025-06-18"),
	wxT("2025-03-26"),
	nullptr
};

// ⭐ WHAT A VERSION REFUSAL OWES THE CALLER, built from the array above and from nowhere else.
// A refusal that does not say what IS on offer leaves the client to guess, which is the same as
// no answer; the specification therefore puts the list in `data.supported`.
//
// AND IT IS A LIST. The first version numbered the keys of a child node — `{"0":"2025-11-25",…}` —
// which a client reads as an object, so `supported.includes(v)` finds nothing and the refusal
// stops being answerable. The node tree has carried an Array value all along (dataBuilder.h).
ibDataNode SupportedVersions(const wxString& asked)
{
	std::vector<ibDataValue> versions;
	for (const wxChar* const* v = kSupportedProtocolVersions; *v != nullptr; ++v)
		versions.push_back(ibDataValue::String(wxString(*v)));

	ibDataNode data;
	data.AddField(wxT("supported"), ibDataValue::Array(versions));
	if (!asked.IsEmpty())
		data.SetValue(wxT("requested"), asked);

	return data;
}

// A request is a JSON document, not a file upload. Anything past this is a
// client that has lost its place — refused before it is allocated, the way the
// debug server refuses an absurd packet length.
const unsigned long kMaxRequestBytes = 8ul * 1024ul * 1024ul;

} // namespace

//---------------------------------------------------------------------------
// the listener
//---------------------------------------------------------------------------
class ibMcpServer::ibMcpListener {
public:

	ibMcpListener(ibMcpServer* owner, const ibMcpSettings& settings, ibSession* session)
		: m_owner(owner), m_settings(settings), m_session(session)
	{
	}

	~ibMcpListener()
	{
		Stop();
	}

	// BOUND ON THE CALLER'S THREAD, deliberately. A person pressed a button and
	// is owed an answer now: "that port is taken" must come back from Start(),
	// not appear in a log a second later while the button says it worked.
	//
	// httplib splits exactly this way — bind_to_port answers here, and
	// listen_after_bind is what the thread then runs.
	bool Bind(wxString& refusal)
	{
		m_server.set_payload_max_length(kMaxRequestBytes);
		m_server.set_tcp_nodelay(true);

		Route();

		const std::string host = std::string(m_settings.m_address.utf8_str());

		if (!m_server.bind_to_port(host, (int)m_settings.m_port)) {
			refusal = wxString::Format(
				_("Port %u on %s is already taken"),
				(unsigned)m_settings.m_port, m_settings.m_address);
			return false;
		}

		return true;
	}

	// Nothing to report: the bind already happened and was already answered for. Starting the
	// thread that serves the bound socket cannot half-succeed.
	void Run()
	{
		m_thread = std::thread([this] { m_server.listen_after_bind(); });
	}

	// STOPPED IN THE ORDER THAT LETS EVERYTHING LEAVE.
	//
	// The stream's provider is asleep on the queue and would never notice the
	// server closing, so it is woken FIRST; then listen_after_bind returns; then
	// the thread is joined, so the socket is closed before a restart on the same
	// port can race the old listener — the property the old poll loop had, kept.
	void Stop()
	{
		{
			std::lock_guard<std::mutex> lock(m_streamMutex);
			m_stopping = true;
		}
		m_streamWake.notify_all();

		m_server.stop();

		if (m_thread.joinable())
			m_thread.join();
	}

	// One event out. Silent when nobody is listening — which is the ordinary
	// state, not a failure: the platform talks to the designer's window whether
	// or not an assistant is connected.
	//
	// ⭐ A QUEUE, NOT A SOCKET. This used to write into a held wxSocketBase from
	// whatever thread called it, having first asked IsConnected() — with a race
	// between the asking and the writing that only stayed harmless because one
	// window was the only writer. Now the caller leaves the line here and the
	// provider, which is the only thing that touches the connection, takes it.
	void WriteToStream(const wxString& json)
	{
		{
			std::lock_guard<std::mutex> lock(m_streamMutex);

			if (!m_streamOpen)
				return;   // nobody is listening — drop it rather than grow forever

			m_streamQueue.push_back(json);
		}
		m_streamWake.notify_one();
	}

private:

	// ⭐ THE ONE QUESTION ASKED OF EVERY REQUEST. `Authorization: Bearer <token>`, compared whole.
	//
	// ⚠ AN EMPTY TOKEN REFUSES EVERYTHING. It is tempting to read "no token configured" as "no
	// checking wanted", and that reading is how a security setting ends up switched off by
	// omission on the one installation nobody looked at. The token is minted when the settings are
	// configured, so blank means something went wrong rather than something was chosen — and the
	// honest answer to that is no.
	bool IsAuthorised(const httplib::Request& req) const
	{
		const wxString expected = m_settings.m_token;
		if (expected.IsEmpty())
			return false;

		const std::string header = req.get_header_value("Authorization");
		if (header.empty())
			return false;

		const wxString given = wxString::FromUTF8(header.c_str(), header.size());

		// The scheme is part of it: a client that sends the bare token is misconfigured in a way
		// worth telling apart from one that sends the wrong one.
		if (!given.StartsWith(wxT("Bearer ")))
			return false;

		return given.Mid(7).Trim(true).Trim(false) == expected;
	}

	// ⭐ WHERE THE CALLER IS SPEAKING FROM — a DIFFERENT question from who they are, and the
	// transport requires both. A browser on any web page can POST to 127.0.0.1; DNS rebinding
	// makes that page's own origin look local to the network stack, and nothing about the packet
	// says it came from a page rather than from a tool. The Origin header is what the browser
	// attaches and cannot be forged by script, so refusing an unknown one is the only defence a
	// local server has against being driven by a site the person merely visited.
	//
	// PRESENCE IS THE DISCRIMINATOR: an ordinary client (a CLI, an editor plugin) sends no Origin
	// at all, and the spec asks for a refusal only when the header IS present and wrong — so this
	// costs nothing for the callers that are not browsers. A loopback origin is allowed because
	// that is a page served by this machine's own tooling.
	//
	// The token already refuses an unauthorised caller; this refuses an unauthorised PLACE, one
	// step earlier, without reading the body.
	static bool IsOriginAllowed(const httplib::Request& req)
	{
		const std::string origin = req.get_header_value("Origin");
		if (origin.empty())
			return true;   // not a browser — nothing to check

		const wxString value = wxString::FromUTF8(origin.c_str(), origin.size()).Lower();
		return value.StartsWith(wxT("http://127.0.0.1"))
			|| value.StartsWith(wxT("https://127.0.0.1"))
			|| value.StartsWith(wxT("http://localhost"))
			|| value.StartsWith(wxT("https://localhost"))
			|| value == wxT("null");   // a file:// page, which is local by construction
	}

	// WHICH REVISION THIS CALLER SPEAKS, and whether we speak it too.
	//
	// The header arrived in revision 2025-06-18 and is REQUIRED of clients from then on; a client
	// older than that sends none, and the transport says to read its absence as 2025-03-26 rather
	// than as an error. What is NOT allowed is to answer an unknown revision as if it were ours —
	// the spec asks for 400 there, so the client learns to negotiate instead of guessing why the
	// answers look wrong.
	static bool IsProtocolVersionAcceptable(const httplib::Request& req)
	{
		const std::string header = req.get_header_value("MCP-Protocol-Version");
		if (header.empty())
			return true;   // pre-2025-06-18 client — read as 2025-03-26, per the transport

		const wxString asked = wxString::FromUTF8(header.c_str(), header.size()).Trim(true).Trim(false);
		for (const wxChar* const* v = kSupportedProtocolVersions; *v != nullptr; ++v) {
			if (asked == *v)
				return true;
		}
		return false;
	}

	// ⭐ THE VERSION REFUSAL, WRITTEN ONCE. It is decided in three places — the header check on
	// each verb, and the `_meta` check in the body — and it used to be ANSWERED in three ways:
	// `-32000` with the list of versions typed out by hand in an English sentence, `-32000` with
	// no list at all, and `-32022` with `data.supported`. Three answers to one refusal, and the
	// hand-typed sentence sat two screens from the array it was copied from, so the next version
	// added would have been announced in one of them and not the others.
	//
	// The code is the one the specification allocates (`-32022`), and the list is READ FROM THE
	// ARRAY, which is the only way it cannot drift from what the server actually accepts.
	static std::string RefuseVersion(const wxString& asked)
	{
		const ibDataNode data = SupportedVersions(asked);

		// No id: the refusal happens before the body has been read, so there is no call to
		// answer — which is the shape JSON-RPC allows for exactly this.
		const wxString text = ibMcpWriteError(ibDataValue(), ibMcpError::UnsupportedVersion,
			wxT("Unsupported protocol version"), &data);

		const wxScopedCharBuffer utf8 = text.utf8_str();
		return std::string(utf8.data(), utf8.length());
	}

	// What the transport saw in the header, for the refusal to quote back.
	static wxString AskedVersion(const httplib::Request& req)
	{
		const std::string header = req.get_header_value("MCP-Protocol-Version");
		return wxString::FromUTF8(header.c_str(), header.size()).Trim(true).Trim(false);
	}

	// The two verbs of the protocol, on one path.
	void Route()
	{
		// ⚠ HOW LONG A REQUEST MAY TAKE, SAID OUT LOUD — because httplib's own answer is FIVE
		// SECONDS (CPPHTTPLIB_SERVER_READ_TIMEOUT_SECOND / _WRITE_TIMEOUT_SECOND), and almost
		// nothing this server does is finished in five seconds.
		//
		// A verb that touches metadata is handed to the MAIN THREAD and waits its turn there
		// (RunTool). Creating a metaobject, saving a configuration, applying one — each takes
		// longer than that whenever the designer is busy. httplib then closed the socket while the
		// work was still queued, so the caller was told the session had ended WHILE THE OPERATION
		// WENT ON TO SUCCEED. Measured 2026-09-04: `metadata_create` and `config_save` both
		// answered "session expired" and both had done their work — and the assistant, believing
		// the refusal, repeated the call and left three unnamed common modules behind in somebody's
		// configuration. A false refusal is worse than a slow answer: it makes a caller act.
		//
		// Matched to the wait in RunTool, which gives the main thread five minutes before it
		// answers on its own, plus a margin so the transport never gives up FIRST — whoever times
		// out should be the one that can explain why.
		m_server.set_read_timeout(std::chrono::minutes(6));
		m_server.set_write_timeout(std::chrono::minutes(6));

		// ⚠ AND HOW LONG A CONNECTION MAY SIT IDLE, which is the half that actually bit. httplib
		// closes a kept-alive connection after FIVE SECONDS of silence
		// (CPPHTTPLIB_KEEPALIVE_TIMEOUT_SECOND) — and the pause between two calls from an assistant
		// is a pause for THINKING, routinely longer than that. The socket was then closed under a
		// client that had every intention of using it again, and the next call read as a dropped
		// session rather than as a reconnect. It explains what the read timeout above does not: the
		// refusal arrived INSTANTLY, on a call that takes 371 ms.
		//
		// This server exists to be addressed occasionally and thought about in between. Idle is its
		// normal state, not a sign the caller has gone.
		m_server.set_keep_alive_timeout(std::chrono::minutes(10));
		// A POST IS A QUESTION: one JSON-RPC message in, one answer out.
		m_server.Post("/", [this](const httplib::Request& req, httplib::Response& res) {

			// ⭐ WHO IS SPEAKING, ASKED FIRST AND ASKED HERE. Before a message is parsed, before a
			// tool is looked up — an unauthorised caller must not be able to learn what exists,
			// let alone reach it.
			//
			// The address is not an authorisation: a loopback port is open to every process on the
			// machine, and this one edits a configuration and runs code as the developer whose
			// session it lives in. "Only local" answers where a caller is, never who.
			if (!IsAuthorised(req)) {
				res.status = 401;
				res.set_content("{\"error\":\"unauthorized\"}", "application/json");
				return;
			}

			// WHERE FROM, and IN WHICH REVISION — both are the transport's questions, both are
			// answered before the body is read. A JSON-RPC error with no `id` is what the spec
			// allows for a refusal that happens before a message exists to answer.
			if (!IsOriginAllowed(req)) {
				res.status = 403;
				res.set_content("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32000,"
					"\"message\":\"Origin not allowed\"}}", "application/json");
				return;
			}
			if (!IsProtocolVersionAcceptable(req)) {
				res.status = 400;
				res.set_content(RefuseVersion(AskedVersion(req)), "application/json");
				return;
			}

			// ⭐ IN WHOSE NAME — per request, because there is no longer one thread to bind once.
			// httplib serves from a pool, so the session that started the server is bound around
			// each call and released after: metadata, rights and the active configuration then
			// answer exactly as they would for the developer at the keyboard. The old code did
			// this once in the thread body; the pool made that impossible, and the binding follows
			// the work rather than the thread.
			// ibSessionScope is the tree's own RAII for this, and it restores through a weak_ptr
			// so a session destroyed while the scope is open cannot be resurrected into the
			// thread map on the way out.
			const ibSessionScope inThisSession(m_session);

			// WHAT THE TRANSPORT SAW, handed down so the protocol layer can hold the body to it.
			// Reading them here and nowhere else keeps httplib out of everything below.
			ibMcpServer::ibMcpWireHeaders wire;
			const auto headerOf = [&req](const char* name) -> wxString {
				const std::string v = req.get_header_value(name);
				return v.empty() ? wxString() : wxString::FromUTF8(v.c_str(), v.size());
			};
			wire.m_protocolVersion = headerOf("MCP-Protocol-Version");
			wire.m_method          = headerOf("Mcp-Method");
			wire.m_name            = headerOf("Mcp-Name");

			const wxString request = wxString::FromUTF8(req.body.data(), req.body.size());

			// 🛑 THE VERDICT IS ASKED FOR, NOT RECOGNISED. The first version read the status out
			// of the emitted JSON by looking for `"code":-32020` — and got 200 on every refusal,
			// because the writer puts a space after the colon. Searching your own output for a
			// fact you already had is a guess about the writer's formatting dressed up as a
			// decision; the layer that decided the refusal is the one that says so.
			int refusalCode = 0;
			const wxString answer = m_owner->Answer(request, wire, &refusalCode);

			// ⭐ THE HTTP STATUS IS PART OF THE REFUSAL, not decoration on it. The modern
			// transport asks a client to tell a version refusal from a header refusal from an
			// unknown method WITHOUT parsing — and that is how a dual-era client decides whether
			// to retry with another version or fall back to `initialize` entirely. A refusal
			// answered 200 reads as agreement to a client that only looks at the status.
			switch ((ibMcpError)refusalCode) {
				case ibMcpError::UnsupportedVersion:                       // retry with another version
				case ibMcpError::HeaderMismatch: res.status = 400; break;  // the two sources disagree
				case ibMcpError::MethodNotFound: res.status = 404; break;  // nothing here answers to that name
				default: break;                                            // 200: the body carries the whole answer
			}

			// A notification is answered with no body, as the protocol says — but
			// the transport still needs a reply, or the client waits for one.
			if (answer.IsEmpty()) {
				res.status = 202;
				return;
			}

			const wxScopedCharBuffer utf8 = answer.utf8_str();
			res.set_content(utf8.data(), utf8.length(), "application/json");
		});

		// A GET IS AN OFFER TO LISTEN. It asks nothing — it hands the server
		// somewhere to speak, and is kept open until one side goes away.
		m_server.Get("/", [this](const httplib::Request& req, httplib::Response& res) {

			// ⚠ THE STREAM IS GUARDED TOO, and it matters more here than on the other verb: this
			// one hands over everything the platform says from the moment it is opened — what is
			// being built, what failed, what a person typed into the assistant window. An
			// unauthorised listener would need to ask for nothing at all.
			if (!IsAuthorised(req)) {
				res.status = 401;
				res.set_content("{\"error\":\"unauthorized\"}", "application/json");
				return;
			}

			// The same two transport questions as on POST — a listen stream hands over MORE than
			// any single call does, so it is the last place to relax either of them.
			if (!IsOriginAllowed(req)) {
				res.status = 403;
				res.set_content("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32000,"
					"\"message\":\"Origin not allowed\"}}", "application/json");
				return;
			}
			if (!IsProtocolVersionAcceptable(req)) {
				res.status = 400;
				res.set_content(RefuseVersion(AskedVersion(req)), "application/json");
				return;
			}

			// ONE LISTENER AT A TIME, still. A second offer replaces the first
			// rather than joining it: two clients each half-hearing a conversation
			// is worse than one hearing all of it, and the person at the designer
			// is talking to ONE assistant. The generation is how the older
			// provider learns it has been replaced — it wakes, sees a newer number
			// and leaves.
			unsigned long mine = 0;
			{
				std::lock_guard<std::mutex> lock(m_streamMutex);
				mine = ++m_streamGeneration;
				m_streamOpen = true;
				m_streamQueue.clear();
			}
			m_streamWake.notify_all();

			res.set_chunked_content_provider("text/event-stream",
				[this, mine](std::size_t /*offset*/, httplib::DataSink& sink) {

					wxString line;
					{
						std::unique_lock<std::mutex> lock(m_streamMutex);

						m_streamWake.wait(lock, [this, mine] {
							return m_stopping
								|| m_streamGeneration != mine
								|| !m_streamQueue.empty();
						});

						if (m_stopping || m_streamGeneration != mine) {
							// Only the CURRENT stream owns the flag; a replaced one
							// must not clear it on its way out.
							if (m_streamGeneration == mine)
								m_streamOpen = false;
							sink.done();
							return false;
						}

						line = m_streamQueue.front();
						m_streamQueue.pop_front();
					}

					const wxScopedCharBuffer utf8 = line.utf8_str();

					std::string frame("data: ");
					frame.append(utf8.data(), utf8.length());
					frame.append("\n\n");

					return sink.write(frame.data(), frame.size());
				});
		});
	}

	ibMcpServer*  m_owner = nullptr;
	ibMcpSettings m_settings;
	ibSession*    m_session = nullptr;

	httplib::Server m_server;
	std::thread     m_thread;

	// The way out. Filled by whoever has something to say (the UI thread, a tool)
	// and drained by the one provider that holds the connection.
	std::mutex              m_streamMutex;
	std::condition_variable m_streamWake;
	std::deque<wxString>    m_streamQueue;
	bool                    m_streamOpen = false;
	unsigned long           m_streamGeneration = 0;
	bool                    m_stopping = false;
};

//---------------------------------------------------------------------------
// the subsystem
//---------------------------------------------------------------------------
ibMcpServer::ibMcpServer(ib::AppDataCtorToken)
{
	// Empty, and still out of line — see the note in the header: the listener is
	// incomplete there, and an inline constructor would need its destructor.
}

// ============================================================================
//  ibMcpMetaBridge — the assistant's subscription to the metadata
// ============================================================================
//
// ⭐⭐ IT WATCHES ALONGSIDE, exactly as ibMcpDebugBridge watches a debugging session somebody else
// is driving. The designer's navigator draws these same stages; this one forwards them, so an
// assistant learns that a catalog was created the moment the tree does, instead of asking again
// and diffing what came back.
//
// ⭐ FIRST TO HEAR (IsAssistant). Whoever FILLS a gap has to be told before whoever would ASK a
// person about it — see ibMetaDataNotifier::IsAssistant. Today it only listens; the ordering is
// what makes it able to answer, later, the questions a tool has the arguments for and a dialog
// would otherwise put to somebody who is not there.
//
// ⚠ ONE LINE PER STAGE AND NOTHING KEPT. A bridge that remembered would be a second copy of the
// configuration, going stale the moment anything changed; the configuration is right there to be
// asked. This is a LOG (Max, 2026-09-01), and its whole value is that it arrives unasked.
class ibMcpMetaBridge : public ibMetaDataNotifier {
public:

	explicit ibMcpMetaBridge(ibMcpServer* server) : m_server(server) {}

	~ibMcpMetaBridge() { Watch(nullptr); }   // off the list while this object is still whole

	virtual bool IsAssistant() const override { return true; }

	void Watch(ibMetaData* metaData)
	{
		if (m_watched == metaData)
			return;

		if (m_watched != nullptr)
			m_watched->RemoveNotifier(this);

		m_watched = metaData;

		if (m_watched != nullptr)
			m_watched->AddNotifier(this);
	}

	virtual void MetaObjectChanged(ibMetaStage stage, ibValueMetaObject* object) override
	{
		if (m_server == nullptr)
			return;

		// ⭐⭐ A REBUILT OBJECT IS NOT A DELETED ONE. Writing a form's FormType announces `Removed`
		// and then `Created` for the SAME object, back to back in one call — the form is being
		// rebuilt, and that pair is exactly right for the tree, which shuts the old editor on the
		// first and draws the new on the second (metaFormObjectProperty.cpp).
		//
		// 🛑 IN A LOG IT IS A LIE. It read as `removed Form 'Form1'` about a form nobody removed,
		// and a reader acting on that would go looking for what else had been deleted (2026-09-01,
		// reading this bridge's own first output). The two are adjacent BY CONSTRUCTION, so the
		// pair is recognised rather than guessed at: a Removed is held, and a Created for the same
		// object turns the two of them into one true word.
		// ⭐⭐ ONE ACT IS ONE LINE, and two stages about the same object in a row are usually one
		// act. Both cases are adjacent BY CONSTRUCTION, so they are recognised rather than guessed:
		//
		//   • Removed → Created is a REBUILD. Writing a form's FormType announces exactly that pair
		//     (metaFormObjectProperty.cpp), and it is right for the tree — the old editor shuts on
		//     the first, the new row is drawn on the second. In a log it read as `removed Form
		//     'Form1'` about a form nobody removed, which would send a reader looking for what else
		//     had been deleted.
		//
		//   • Renamed → Created is ONE CREATE. An object is made under a generated name and given
		//     the asked-for one before it is announced, so the rename happens to something nothing
		//     has been told about yet. The engine already silences this during a PASTE, for the
		//     same reason and in those words — but that mark also routes a form's controls through
		//     PasteNode, which is too much to disturb for the sake of a line here.
		//
		// ⚠ Held as the finished LINE, not as the object: by the time a lone removal is flushed the
		// object it was about may be past answering anything.
		if (object != nullptr) {

			if (stage == ibMetaStage::Removed || stage == ibMetaStage::Renamed) {
				m_heldId   = object->GetMetaID();
				m_heldWord = stage == ibMetaStage::Removed ? _("removed") : _("renamed");
				m_heldLine = Line(m_heldWord, object);
				return;
			}

			const bool paired =
				stage == ibMetaStage::Created && m_heldId == object->GetMetaID();

			if (!m_heldLine.IsEmpty() && !paired)
				m_server->Note(m_heldLine);   // it stood on its own after all

			const bool rebuilt = paired && m_heldWord == _("removed");

			m_heldId = 0;
			m_heldLine.clear();
			m_heldWord.clear();

			if (rebuilt) {
				m_server->Note(Line(_("rebuilt"), object));
				return;
			}
		}

		const wxString said = Word(stage);
		if (said.IsEmpty())
			return;   // a stage with nothing to report — see below

		m_server->Note(Line(said, object));
	}

private:

	// ⭐⭐ THE ADDRESS TRAVELS WITH THE NEWS. The kind and the name read well and are not enough to
	// act on: told only *"created Template 'Template1'"*, the next question is "which one" — and a
	// template is NESTED, so metadata_list and metadata_get by name answer nothing about it
	// (2026-09-01, watching exactly that happen the first time this fired). The id is what every
	// metadata_* argument is written in, and the owner is what makes the line readable.
	static wxString Line(const wxString& said, const ibValueMetaObject* object)
	{
		if (object == nullptr)
			return wxString::Format(wxT("metadata: %s"), said);

		const ibValueMetaObject* const owner = object->GetParent();

		return owner != nullptr
			? wxString::Format(wxT("metadata: %s %s '%s' in '%s' (id %i)"),
				said, object->GetClassName(), object->GetName(), owner->GetName(), (int)object->GetMetaID())
			: wxString::Format(wxT("metadata: %s %s '%s' (id %i)"),
				said, object->GetClassName(), object->GetName(), (int)object->GetMetaID());
	}

	// ⭐ EVERY STAGE BUT ONE. A stage is not only news, it is the ANSWER TO A COMMAND: ask for a
	// save and `Saved` is how you learn it happened, without asking again and comparing (Max,
	// 2026-09-01). So the default is to forward.
	//
	// ⚠ `Edited` IS THE EXCEPTION, and only because of its rate: it arrives on every keystroke in a
	// property field. Forwarding it would bury the ones that answer something, which is the one way
	// a log stops being read.
	static wxString Word(ibMetaStage stage)
	{
		switch (stage) {
		case ibMetaStage::Created:  return _("created");
		case ibMetaStage::Renamed:  return _("renamed");
		case ibMetaStage::Removed:  return _("removed");
		case ibMetaStage::Loaded:   return _("loaded");
		case ibMetaStage::Run:      return _("running");
		case ibMetaStage::Saved:    return _("saved");
		case ibMetaStage::Closed:   return _("closed");
		case ibMetaStage::Applying: return _("applying to the database");
		case ibMetaStage::Applied:  return _("applied to the database");
		case ibMetaStage::Reverted: return _("rolled back to the database");
		case ibMetaStage::Edited:   return wxEmptyString;   // a keystroke, not an event
		}
		return wxEmptyString;
	}

	ibMcpServer* const m_server;
	ibMetaData*        m_watched = nullptr;

	// The stage not yet said, waiting to learn whether it stands alone — see MetaObjectChanged.
	ibMetaID           m_heldId = 0;
	wxString           m_heldWord;
	wxString           m_heldLine;

	wxDECLARE_NO_COPY_CLASS(ibMcpMetaBridge);
};

void ibMcpServer::WatchMetadata()
{
	if (m_metaBridge == nullptr)
		m_metaBridge = std::make_unique<ibMcpMetaBridge>(this);

	// The open configuration, or nothing — a server outlives any one configuration and must not
	// keep a subscription on one that has been closed.
	m_metaBridge->Watch(activeMetaData != nullptr && activeMetaData->IsConfigOpen()
		? activeMetaData : nullptr);
}

ibMcpServer::~ibMcpServer()
{
	Stop();
}

namespace {

// THE SERVER'S OWN ADDRESS in the settings store. A literal, because what is
// configured here is the PLATFORM — it has no metaobject whose identity it
// could borrow. Minted once; changing it orphans everything already saved.
const wxChar* const kMcpSettingsObject = wxT("6d637073-6572-7665-7200-000000000001");

ibSettingsKey KeyFor(const ibSession* session)
{
	// The user half is what makes the setting theirs. Empty (a base with open
	// access, no users at all) is a legitimate address too — it means "shared by
	// everybody", which is exactly right when there is nobody to tell apart.
	const ibGuid userKey = session != nullptr
		? ibGuid(session->GetUserInfo().m_strUserGuid) : ibGuid();

	return ibSettingsKey(ibSettingsCategory::Mcp, ibGuid(kMcpSettingsObject), wxNullGuid, userKey);
}

} // namespace

bool ibMcpServer::LoadSettings(ibSession* session)
{
	ibSettingsStorage* storage = ibApplicationData::GetSettingsStorage();
	if (storage == nullptr)
		return false;

	ibDataNode node;
	if (!storage->Restore(KeyFor(session), node))
		return false;   // a cold start — the defaults stand

	ibMcpSettings settings;
	node.GetValue(wxT("enabled"), settings.m_enabled);
	node.GetValue(wxT("address"), settings.m_address);

	s32 port = settings.m_port;
	node.GetValue(wxT("port"), port);
	settings.m_port = (unsigned short)port;

	node.GetValue(wxT("token"), settings.m_token);

	// 🛑 A MINTED TOKEN THAT NOBODY WRITES DOWN IS A NEW TOKEN EVERY MORNING. Configure mints one
	// when the settings carry none — which is right — but minting is a CHANGE, and a change that
	// is not saved happens again on the next start. Every client configured yesterday is refused
	// today, and the refusal looks exactly like the server being down.
	//
	// Asked BEFORE Configure, because afterwards there is always a token and the question cannot
	// be asked any more.
	const bool minted = settings.m_token.IsEmpty();

	Configure(settings);

	if (minted)
		SaveSettings(session);

	return true;
}

bool ibMcpServer::SaveSettings(ibSession* session) const
{
	ibSettingsStorage* storage = ibApplicationData::GetSettingsStorage();
	if (storage == nullptr)
		return false;

	ibDataNode node;
	node.SetValue(wxT("enabled"), m_settings.m_enabled);
	node.SetValue(wxT("address"), m_settings.m_address);
	node.SetValue(wxT("port"), (s32)m_settings.m_port);
	node.SetValue(wxT("token"), m_settings.m_token);

	return storage->Save(KeyFor(session), node);
}

void ibMcpServer::Configure(const ibMcpSettings& settings)
{
	// A RUNNING SERVER IS NOT MOVED. Stopping and starting is the caller's
	// decision to make out loud; silently rebinding would leave the page saying
	// one address while a client talks to another.
	m_settings = settings;

	// ⭐ A TOKEN IS MINTED RATHER THAN ASKED FOR. Nobody invents a good secret at a settings page,
	// and a field that starts empty stays empty — so the first time these settings are configured
	// without one, the platform makes it. A guid is what this tree already uses for values that
	// must not collide and must not be guessed; two of them, because one is 122 bits of randomness
	// and this is the only thing standing between a local port and somebody else's configuration.
	//
	// Never regenerated behind a person's back: a token that changes on its own silently breaks
	// every client that was told the old one, and the breakage looks like the server being down.
	if (m_settings.m_token.IsEmpty())
		m_settings.m_token = wxNewUniqueGuid.str() + wxNewUniqueGuid.str();
}

bool ibMcpServer::Start(ibSession* session, wxString& refusal)
{
	if (IsRunning()) {
		refusal = _("The server is already running");
		return false;
	}

	if (!m_settings.m_enabled) {
		// Says WHERE to turn it on, not just that it is off: a refusal that
		// leaves a person hunting through menus has only done half its job.
		// ⚠ ASCII ONLY in a narrow literal a person will read. The file carries a
		// BOM, so the COMPILER reads it correctly — but the literal is still
		// converted to the execution code page (1251 here), where an arrow does
		// not exist. It compiled with a warning and would have shown as rubbish.
		refusal = _("Assistant access is switched off.\n\n"
			"Turn it on in Tools > Options > Assistant access.");
		return false;
	}

	if (!m_settings.IsOk()) {
		refusal = _("The address or the port is not set");
		return false;
	}

	if (session == nullptr) {
		refusal = _("There is no session to work in");
		return false;
	}

	// ⭐ THE INVARIANT BELONGS WHERE IT IS RELIED ON. Configure mints the token, but START is what
	// binds the port — and a server listening with an empty one refuses every single request while
	// looking perfectly healthy from outside. Anything reaching here without having gone through
	// the settings page (a cold start, a restore that dropped the field) would do exactly that,
	// and the symptom is indistinguishable from the server being down.
	//
	// ⚠ AND WRITTEN DOWN IMMEDIATELY, for the same reason as in LoadSettings: a secret that lives
	// only in this process is a different secret after every restart. Placed AFTER the session
	// check because saving needs one — the settings are keyed by who is asking.
	if (m_settings.m_token.IsEmpty()) {
		m_settings.m_token = wxNewUniqueGuid.str() + wxNewUniqueGuid.str();
		SaveSettings(session);
	}

	std::unique_ptr<ibMcpListener> listener(new ibMcpListener(this, m_settings, session));
	if (!listener->Bind(refusal))
		return false;

	listener->Run();

	m_session  = session;
	m_listener = std::move(listener);
	m_running.store(true);

	// ⭐ WATCHING FROM THE MOMENT IT IS UP, not from the first question somebody asks it. The
	// subscription is also refreshed at every dispatch, because a configuration can be opened and
	// closed while the server runs — but relying on the dispatch ALONE means everything the person
	// did before the assistant's first call happened unobserved (2026-09-01, watching exactly that:
	// two empty lists after a template had been created).
	WatchMetadata();

	// The other half of the pair in Stop. A window that was told it went quiet has to be told when
	// it stops being quiet, or the last thing it ever said stays wrong on screen.
	//
	// ⭐ AND IT IS THE SAME TEXT THE WINDOW SHOWS WHEN IT OPENS. Greeting() was written for two
	// moments and was being used in one — the header said "published when the server starts" and
	// nothing did it, so a person watching an already-open window got a thinner line than someone
	// who opened it a second later. One text, both moments, and the promise in the header is now
	// kept by the code rather than by the comment.
	Publish(wxT("did"), Greeting(), wxEmptyString);

	// LISTEN TO THE DEBUGGER TOO, from here on. The bridge is a list, so this
	// takes nothing away from the IDE's own windows — the developer debugs as
	// they always did and the same stop is visible on this side. The debug client
	// owns what it is given, which is why this is a bare `new` and why letting go
	// is a call to it rather than a delete of our own.
	if (debugClient != nullptr && m_debugBridge == nullptr) {
		ibMcpDebugBridge* bridge = new ibMcpDebugBridge();
		if (bridge->Attach())
			m_debugBridge = bridge;
		else
			delete bridge;
	}

	return true;
}

void ibMcpServer::Stop()
{
	if (!IsRunning())
		return;

	// ⭐ SAID BEFORE IT GOES, AND SAID BY THE SERVER. Whoever is watching this - the assistant
	// window above all - keeps standing there afterwards looking exactly as it did while it
	// worked, and anything typed into it would join a queue no client can ever collect. That
	// reads as being ignored, which is the worst thing a window can do.
	//
	// Published HERE rather than by whoever pressed the button, because the server is the only
	// one that knows it stopped: it also stops when the session ends and when the process goes,
	// and a caller-side announcement would cover exactly one of the three.
	Publish(wxT("did"),
		_("Assistant access has been switched off. Nothing is listening, so nothing written here "
		  "will be collected until it is switched on again in Tools > Options > Assistant "
		  "access."),
		wxEmptyString);

	m_running.store(false);

	// Detach DESTROYS the bridge — the debug client's list owns it — so the
	// pointer is dropped in the same breath.
	if (m_debugBridge != nullptr) {
		m_debugBridge->Detach();
		m_debugBridge = nullptr;
	}

	if (m_listener) {
		// Stop() wakes the stream, closes the server and JOINS — so the port is
		// free before we return and a restart on it cannot race the old listener.
		// The destructor calls it too; asking here keeps the order visible at the
		// place that cares about it.
		m_listener->Stop();
		m_listener.reset();
	}

	m_session = nullptr;
}

wxString ibMcpServer::GetEndpoint() const
{
	if (!IsRunning())
		return wxEmptyString;

	return wxString::Format(wxT("http://%s:%u/"), m_settings.m_address, (unsigned)m_settings.m_port);
}

void ibMcpServer::AddNotifier(ibMcpNotifier* notifier)
{
	if (notifier == nullptr)
		return;

	for (const ibMcpNotifier* registered : m_notifiers)
		if (registered == notifier)
			return;

	m_notifiers.push_back(notifier);
}

void ibMcpServer::RemoveNotifier(ibMcpNotifier* notifier)
{
	m_notifiers.erase(
		std::remove(m_notifiers.begin(), m_notifiers.end(), notifier),
		m_notifiers.end());
}

// ⭐ THE LEVELS AS AN ORDER, which is the only thing a gate needs of them. The wire spells them as
// words (the syslog set the protocol adopted); comparing words is comparing spellings, and the
// question being asked is "is this at least as important as what they asked for".
//
// ⚠ An unknown word ranks as the LOWEST, so a client that sends something we do not recognise
// keeps receiving everything rather than silently losing the stream — the failure that would look
// exactly like the server having stopped talking.
static int ibMcpLevelRank(const wxString& level)
{
	if (level.IsSameAs(wxT("emergency"), false)) return 7;
	if (level.IsSameAs(wxT("alert"),     false)) return 6;
	if (level.IsSameAs(wxT("critical"),  false)) return 5;
	if (level.IsSameAs(wxT("error"),     false)) return 4;
	if (level.IsSameAs(wxT("warning"),   false)) return 3;
	if (level.IsSameAs(wxT("notice"),    false)) return 2;
	if (level.IsSameAs(wxT("info"),      false)) return 1;

	return 0;   // debug, and anything we do not know
}

// What Say() sends on the stream. Named so the gate reads as a comparison, not as a number.
static const int kChatNoteLevel = 1;   // info

void ibMcpServer::Say(const wxString& text)
{
	if (text.IsEmpty())
		return;

	{
		std::lock_guard<std::mutex> lock(m_saidMutex);

		// TWO LISTS, ONE BREATH. The queue is emptied by whoever collects it; the conversation is
		// not, because "what is new" and "what was said" stop being the same question the moment
		// anybody collects anything.
		m_said.push_back(text);
		m_conversation.push_back({ true, text });
	}

	// Shown in the window straight away, before anyone has collected it. A
	// message that only appears once somebody answers looks like it was lost.
	Publish(wxT("you"), text, wxEmptyString);

	// AND THE LISTENER IS TOLD, so nobody has to poll. This is the whole point
	// of holding a stream open: the assistant learns that something was said at
	// the moment it is said, instead of asking every few seconds and filling the
	// window with the asking.
	// ⚠ AND ONLY AS LOUDLY AS THEY ASKED. A client that sent `logging/setLevel` above info is
	// saying it does not want this frame; declaring the capability and then ignoring the switch
	// would be advertising something we do not honour. The WINDOW is unaffected — a person
	// watching their own designer is not the audience a client's log level is about.
	if (m_listener && kChatNoteLevel >= m_logLevel.load()) {
		ibDataNode note;
		note.SetValue(wxT("jsonrpc"), wxString(wxT("2.0")));
		note.SetValue(wxT("method"), wxString(wxT("notifications/message")));

		ibDataNode& params = note.Child(wxT("params"));
		params.SetValue(wxT("level"), wxString(wxT("info")));
		params.SetValue(wxT("logger"), wxString(wxT("chat")));
		params.SetValue(wxT("data"), text);

		m_listener->WriteToStream(ibMcpRenderNode(note));
	}
}

void ibMcpServer::Note(const wxString& text)
{
	if (text.IsEmpty())
		return;

	// ⭐⭐ FOR THE ASSISTANT, NOT FOR THE WINDOW. Say() puts a line in two places — the queue the
	// assistant drains AND the conversation the pane draws — because a person typing a message
	// wants to see it appear. A metadata stage is the other kind of line: the person DID the thing,
	// so telling them they did it fills their own window with their own actions (Max, 2026-09-01:
	// *"I do not need it written in the list that I created something — that is information for
	// you"*).
	//
	// So: its OWN list. Same wake-up, no turn in the conversation, nothing published to the pane —
	// and collected apart from the person's messages, because only those are a request.
	{
		std::lock_guard<std::mutex> lock(m_saidMutex);
		m_noted.push_back(text);
	}

	// The listener still learns of it at once — see the note in Say() about the log level.
	if (m_listener && kChatNoteLevel >= m_logLevel.load()) {
		ibDataNode note;
		note.SetValue(wxT("jsonrpc"), wxString(wxT("2.0")));
		note.SetValue(wxT("method"), wxString(wxT("notifications/message")));

		ibDataNode& params = note.Child(wxT("params"));
		params.SetValue(wxT("level"), wxString(wxT("info")));
		params.SetValue(wxT("logger"), wxString(wxT("metadata")));
		params.SetValue(wxT("data"), text);

		m_listener->WriteToStream(ibMcpRenderNode(note));
	}
}

std::vector<wxString> ibMcpServer::TakeNoted()
{
	std::lock_guard<std::mutex> lock(m_saidMutex);

	std::vector<wxString> taken;
	taken.swap(m_noted);
	return taken;
}

std::vector<wxString> ibMcpServer::TakeSaid()
{
	std::lock_guard<std::mutex> lock(m_saidMutex);

	// TAKEN, not read: two clients asking would otherwise both answer the same
	// question, and the person would get two replies to one message.
	std::vector<wxString> taken;
	taken.swap(m_said);
	return taken;
}

void ibMcpServer::Reply(const wxString& text)
{
	{
		std::lock_guard<std::mutex> lock(m_saidMutex);
		m_conversation.push_back({ false, text });
	}

	Publish(wxT("assistant"), wxEmptyString, text);
}

wxString ibMcpServer::Greeting() const
{
	if (!IsRunning())
		return _("Assistant access is not running. Start it in "
			"Tools > Options > Assistant access.");

	wxString out = wxString::Format(
		_("Assistant access is running at %s (build %u)."), GetEndpoint(), GetBuildId());

	// WHAT IS OPEN, because that is what an assistant would be working on and what makes the
	// line worth reading rather than a status light.
	if (activeMetaData != nullptr && activeMetaData->IsConfigOpen())
		if (const ibValueMetaObject* root = activeMetaData->GetCommonMetaObject())
			out += wxString::Format(_(" Configuration '%s' is open."), root->GetName());

	// ⚠ AND WHETHER ANYBODY IS ACTUALLY THERE. "Running" is not "connected", and a person waiting
	// for an answer from an assistant that never joined would otherwise have no way to tell the
	// two apart — the window would look exactly the same.
	out += m_clientCanSample.load() || m_greeted
		? _("\n\nAn assistant is connected.")
		: _("\n\nNothing is connected yet - the key and the setup line are on the same settings "
			"page.");

	return out;
}

size_t ibMcpServer::PendingCount() const
{
	std::lock_guard<std::mutex> lock(m_saidMutex);
	return m_said.size();
}

std::vector<ibMcpServer::Turn> ibMcpServer::GetConversation() const
{
	std::lock_guard<std::mutex> lock(m_saidMutex);
	return m_conversation;   // a COPY — the caller reads it on its own thread, at its own pace
}

//---------------------------------------------------------------------------
// asking the client's model
//---------------------------------------------------------------------------
//
// ⭐ THE LONG ROAD, AND WHY IT IS WORTH THE PLUMBING. Say() drops a message in a queue and rings a
// bell; whoever is connected still has to come and take it with a tool. If nobody is looking, the
// message waits — which is exactly the complaint this exists to answer: text typed into the
// designer sat there until the person went and prodded the assistant from somewhere else.
//
// `sampling/createMessage` inverts it. The server asks; the client runs the completion and posts
// the answer back to the same endpoint, where Answer() recognises it as a RESPONSE (it names no
// method) and hands it to Reply. Nobody polls, and nobody waits on a socket.
//
// ⚠ AND THE PLATFORM STILL HAS NO MODEL, which is the part worth keeping. No key is stored here
// and no outbound connection is made: the completion runs wherever the client's already does, on
// the same assistant that is already working on this configuration.

bool ibMcpServer::CanAskModel() const
{
	// BOTH HALVES. Running is not enough — a client that never declared `sampling` in its
	// handshake will do nothing with the request, and a button enabled on hope is worse than one
	// that is honestly grey.
	return IsRunning() && m_clientCanSample.load();
}

bool ibMcpServer::AskModel(const wxString& question, wxString& refusal)
{
	if (question.IsEmpty()) {
		refusal = ibMcpText("There is nothing to ask.");
		return false;
	}

	if (!IsRunning()) {
		refusal = ibMcpText("Assistant access is not running.");
		return false;
	}

	if (!m_clientCanSample.load()) {
		refusal = ibMcpText("The connected assistant cannot be asked - it did not offer that when it "
			"connected. What is typed here still reaches it, but only when it next looks.");
		return false;
	}

	if (!m_listener) {
		refusal = ibMcpText("Nobody is connected.");
		return false;
	}

	// OUR OWN NUMBERING. A client's id 1 and ours are different conversations, and the response
	// that comes back is matched by this value — mixing the two ranges is how an answer gets
	// attached to the wrong question.
	const long id = m_outgoingId.fetch_add(1);

	ibDataNode ask;
	ask.SetValue(wxT("jsonrpc"), wxString(wxT("2.0")));
	ask.AddField(wxT("id"), ibDataValue::Int((s64)id));
	ask.SetValue(wxT("method"), wxString(wxT("sampling/createMessage")));

	ibDataNode& params = ask.Child(wxT("params"));

	// ONE MESSAGE, THE PERSON'S OWN. The shape is the protocol's: a list of messages, each with a
	// role and a content block. Nothing is prepended to what was typed — a server that quietly
	// wraps a person's words in instructions of its own is answering a question they did not ask.
	std::shared_ptr<ibDataNode> message = std::make_shared<ibDataNode>();
	message->SetValue(wxT("role"), wxString(wxT("user")));

	ibDataNode& content = message->Child(wxT("content"));
	content.SetValue(wxT("type"), wxString(wxT("text")));
	content.SetValue(wxT("text"), question);

	params.AddField(wxT("messages"), ibDataValue::Array({ ibDataValue::Child(message) }));

	// A ceiling, because an unbounded request is one a client may refuse outright — and because
	// what comes back is shown in a window, not saved to a file.
	params.AddField(wxT("maxTokens"), ibDataValue::Int(4096));

	m_listener->WriteToStream(ibMcpRenderNode(ask));

	// Shown as asked the moment it is asked. The answer arrives later, through Answer() → Reply,
	// and a window that showed nothing in between would look like the question was swallowed.
	Publish(wxT("you"), question, wxEmptyString);

	return true;
}

// ⭐⭐ A TOOL RUNS WHERE A MOUSE CLICK RUNS.
//
// 🛑 Until this, every tool executed ON THE SOCKET THREAD — httplib's — and reached straight into
// the designer's tree and its widgets. wx allows exactly one thread near a widget, so what came
// back was not a refusal but a crash: `app_run` took the whole designer down on the spot, creating
// a form opened a modal and died on "only the main thread may do this", and a value edited from
// here raced the UI over a refcount ("DecRef: invalid ref data count"). None of it was diagnosable
// from the answer, because there was no answer — the process was gone.
//
// ⚠ AND IT IS A CLASS, NOT A LIST OF VERBS. Any door that asks a person something, or touches a
// control three layers down, fails the same way and only ever at run time. Marking the tools that
// are "safe without a person" would be the same convention this replaces, kept by the attention of
// whoever adds the next one.
//
// ⭐ NOTHING HAD TO BE BUILT. ibGUISession already holds an ibWorkerPoolGUI whose Submit hands the
// task to wxTheApp::CallAfter — the main loop, which is where a click arrives — and it runs a task
// INLINE when the caller is already on that thread, so nothing that worked before pays for this.
// The pool is asked of the SESSION, so a headless host (daemon, tests) answers null and the tool
// runs here, which is correct: there is no UI thread to reach.
//
// ⚠ THE STATE IS SHARED, NOT CAPTURED BY REFERENCE. A timeout does not cancel the task — it only
// stops US waiting — so the task may still run after this function has returned and its stack is
// gone. Everything it touches therefore lives in the shared block, and the arguments are COPIED
// into it rather than pointed at.
bool ibMcpServer::RunTool(const class ibMcpTool* tool, const ibDataNode& arguments,
	ibDataNode& payload, wxString& refusal)
{
	// ⭐ ON THE OPEN CONFIGURATION'S LIST, checked here because this is the one place every call
	// passes and a configuration can be opened, closed and reopened while the server runs. A
	// pointer compare when nothing has changed, which is almost always.
	WatchMetadata();

	// ⚠ …UNLESS THE TOOL MUST NOT BE THERE. See ibMcpTool::NeedsMainThread: a verb that WAITS for
	// something the main thread delivers cannot be run on it, and the debugger's evaluation is one.
	ibWorkerPool* pool = m_session != nullptr && tool->NeedsMainThread()
		? m_session->GetWorkerPool() : nullptr;

	if (pool == nullptr)
		return tool->Call(arguments, payload, refusal);

	struct ibMcpPendingCall {
		const ibMcpTool* m_tool = nullptr;
		ibDataNode       m_arguments;
		ibDataNode       m_payload;
		wxString         m_refusal;
		bool             m_ok = false;
	};

	auto call = std::make_shared<ibMcpPendingCall>();
	call->m_tool      = tool;
	call->m_arguments = arguments;

	// ⭐ WRITTEN DOWN, BECAUSE WHEN THIS GOES WRONG THERE IS NOTHING ELSE TO READ. The journal
	// carried no `mcp` channel at all until 2026-09-04, so a call that reached the main thread and
	// never came back left the technology journal without a single line about it — the one place
	// somebody would look. Two lines per main-thread call: what was asked, and what came of it
	// with how long it took. Enough to tell "slow" from "never ran" without a debugger.
	const wxString toolName = tool->GetName();
	ibJournalInfo(wxT("mcp"), wxT("%s: handed to the main thread"), toolName);
	const wxLongLong startedAt = wxGetUTCTimeMillis();

	std::future<void> done = pool->Submit(m_session, [call]() {
		call->m_ok = call->m_tool->Call(call->m_arguments, call->m_payload, call->m_refusal);
	});

	// ⚠ FINITE, AND THE NUMBER IS A COMPROMISE STATED OUT LOUD. The wait is not there to bound the
	// WORK — applying a configuration legitimately takes minutes and must not be cut off — it is
	// there so a MODAL DIALOG cannot wedge the server for the rest of the day: the main thread is
	// then busy until a person clicks, and nobody is watching this socket. Long enough for real
	// work, finite so a forgotten dialog is answered instead of hung on.
	if (done.wait_for(std::chrono::minutes(5)) != std::future_status::ready) {
		ibJournalError(wxT("mcp"), wxT("%s: the main thread did not take it within five minutes - "
			"the call is still queued and will run"), toolName);
		refusal = ibMcpText("The designer did not get to this within five minutes - it is most likely "
			"showing a dialog and waiting for a person. The call was not abandoned: it will run "
			"when the window is answered. Nothing here was left half-done.");
		return false;
	}

	// ⭐ THE EXCEPTION COMES BACK THROUGH THE FUTURE, and is thrown HERE — inside the caller's try,
	// where every catch that turns an engine refusal into words already stands. Not one of them had
	// to change: the tool still raises on this thread, it merely runs on another.
	done.get();

	payload = call->m_payload;
	refusal = call->m_refusal;

	// The figure is the point: a caller that was told the connection dropped can read here whether
	// the work took four seconds or four minutes, and whether it succeeded anyway.
	ibJournalInfo(wxT("mcp"), wxT("%s: %s in %lld ms"), toolName,
		call->m_ok ? wxT("done") : (call->m_refusal.IsEmpty() ? wxT("refused") : call->m_refusal),
		(wxGetUTCTimeMillis() - startedAt).GetValue());

	return call->m_ok;
}

void ibMcpServer::Publish(const wxString& method, const wxString& request, const wxString& answer)
{
	for (ibMcpNotifier* notifier : m_notifiers) {
		try {
			notifier->OnMcpExchange(method, request, answer);
		}
		catch (...) {
			// A watcher that throws does not get to break the exchange it was
			// only watching — the same rule the diagnostics bus keeps.
		}
	}
}

//---------------------------------------------------------------------------
// an argument the tool never declared
//---------------------------------------------------------------------------
//
// ⭐ ASKED OF THE SCHEMA THE TOOL ALREADY PUBLISHES. Every tool describes its input for the
// caller's benefit (DescribeInput → the `properties` node); that description is also the complete
// list of names it will read, so it can be the gate as well, with nothing new to write down and
// nothing that can drift from what the caller was told.
//
// Only NAMES are checked. A type is the tool's own business — it is the one that knows whether a
// string is a kind, a word from a closed set, or a path — and a check here would be a second
// opinion in a place with less information.
//
// Returns the first name nobody declared, or empty when every name is known.
//
static wxString ibMcpUndeclaredArgument(const ibMcpTool* tool, const ibDataNode& arguments)
{
	if (tool == nullptr)
		return wxEmptyString;

	ibDataNode schema;
	tool->DescribeInput(schema);

	const ibDataNode* properties = schema.FindChild(wxT("properties"));

	// A tool that declares no properties takes no arguments BY DESCRIPTION, but that is also what
	// a tool looks like before anyone wrote its schema — so silence here is read as "no opinion"
	// and lets the call through, rather than refusing every argument of an undocumented verb.
	if (properties == nullptr)
		return wxEmptyString;

	// ⚠ BOTH AREAS. A node keeps plain values in its FIELDS and composite ones in its PROPERTIES,
	// and an incoming argument lands in whichever its JSON shape calls for — `id: 5` is a field,
	// `text: {…}` is a property. Walking one of them would let exactly the other kind through
	// unchecked, which is the shape that started this: an object handed to an argument that wanted
	// a string.
	//
	// The schema side is asked with FindChild, which reads the property area — ibMcpSchemaArgument
	// declares every argument as a sub-node there.
	for (const auto& field : arguments.Fields()) {
		if (properties->FindChild(field.first) == nullptr)
			return field.first;
	}

	for (const auto& prop : arguments.Properties()) {
		if (properties->FindChild(prop.first) == nullptr)
			return prop.first;
	}

	return wxEmptyString;
}

// ⭐⭐ AND THE REFUSAL SAYS WHAT IT DOES TAKE. "Check the tool's schema" is a round trip that ends
// where the caller already was: they built the call FROM the schema and still got the name wrong,
// so sending them back to re-read it is the one thing known not to work.
//
// Measured on this server, 2026-09-02, two names in one call: `parent` for `parent_id` — a spelling
// away, guessable from a list — and `comment` for `note`, which no similarity would ever find
// because the two words differ only in what they are FOR. Both are answered by simply saying the
// names, which costs one line and ends the guessing.
//
// The near name is said FIRST when there is one, because a typo and a wrong concept are different
// mistakes and the first is worth pointing straight at.
// (`closest`, not `near`: `near` is still a macro in the Windows headers.)
static wxString ibMcpArgumentsOnOffer(const ibMcpTool* tool, const wxString& given, wxString& closest)
{
	if (tool == nullptr)
		return wxEmptyString;

	ibDataNode schema;
	tool->DescribeInput(schema);

	const ibDataNode* properties = schema.FindChild(wxT("properties"));
	if (properties == nullptr)
		return wxEmptyString;

	const wxString wanted = given.Lower();
	wxString names;

	for (const auto& declared : properties->Properties()) {

		if (!names.IsEmpty())
			names += wxT(", ");
		names += declared.first;

		// NEAR ENOUGH TO BE A TYPO: one name inside the other (`parent` in `parent_id`, `value` in
		// `values`). Deliberately not a distance measure — this is a nudge, and a wrong nudge is
		// worse than none when the full list is right beside it.
		const wxString candidate = declared.first.Lower();

		if (closest.IsEmpty() && wanted.length() >= 3
			&& (candidate.Find(wanted) != wxNOT_FOUND || wanted.Find(candidate) != wxNOT_FOUND))
			closest = declared.first;
	}

	return names;
}

// (⛔ THE MISSING-ARGUMENT GATE MOVED OUT OF THIS FILE — to mcpTool.cpp, beside ibMcpArgumentFault.
//  By its own note it is asked OF THE ARGUMENTS rather than of the published schema, and
//  ibMcpArgument is where `required` is stated: it was living in the transport by accident of
//  where it was first needed. Two consequences, one of them the reason it moved: a test could not
//  call it — `static` in a .cpp — so the suite's third question could only compare the schema with
//  itself while its NAME promised it checked the refusal. A test whose body does not do what its
//  name says is the same defect the suite exists to catch, one floor up.)

//---------------------------------------------------------------------------
// the protocol
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// what a client is told the moment it connects
//---------------------------------------------------------------------------
//
// ⭐ THE ORIENTATION IS THE PROTOCOL'S OWN FIELD, and it was empty. A client arrived, saw two
// tools, and had to work out by trial that the rest are FOUND rather than announced — and had no
// way at all to learn that this configuration carries written findings about itself.
//
// ⭐⭐ AND THE NOTES ARE INLINED, not pointed at. A pointer is a suggestion, and a suggestion is
// followed by whoever happens to think of it; the point of writing down why something is the way
// it is, is that the NEXT person does not have to ask. Inlining costs the same characters every
// connection and saves the round trip that half of callers would never make.
//
// ⚠ THE HELP TEXT IS NOT HERE. It is written for the person USING the application, and mixing the
// two audiences is how engineering prose ends up on somebody's F1 screen.
//
//---------------------------------------------------------------------------

void ibMcpDescribePlatform(ibDataNode& into)
{
	into.AddField(wxT("build"), ibDataValue::Int((s64)GetBuildId()));

	const ibUserInfo& who = appData->GetUserInfo();

	if (!who.IsOk()) {

		// ⭐ NO ACCOUNT IS A STATE, NOT A GAP — a base with none opens without asking who you are,
		// which is an ordinary situation and not a failure to report one. Said out loud because a
		// MISSING field is unreadable: a caller cannot tell "there is no account system here"
		// from "this was never implemented" from "something went wrong".
		//
		// ⚠ And it is the most PERMISSIVE state, not the least: with nobody logged in there are no
		// roles to fold, so every right answers with its own declared default. That reads
		// backwards at first glance, which is exactly why it is spelled out.
		into.AddField(wxT("account"), ibDataValue::Bool(false));
		into.SetValue(wxT("accountNote"),
			ibMcpText("No accounts exist in this base, so nobody is logged in - it opens without asking. "
			  "That is not a restriction: with no roles to fold, every right answers with its "
			  "declared default. `metadata_rights` says what those are."));
	}
	else {

		into.AddField(wxT("account"), ibDataValue::Bool(true));

		ibDataNode& user = into.Child(wxT("user"));
		user.SetValue(wxT("name"), who.m_strUserName);

		if (!who.m_strUserFullName.IsEmpty())
			user.SetValue(wxT("fullName"), who.m_strUserFullName);

		std::vector<ibDataValue> roles;

		for (const ibUserInfo::ibUserRole& role : who.m_roleArray)
			roles.push_back(ibDataValue::String(
				role.m_mode == ibRoleCompositionMode_Intersection
					? role.m_strRoleName + wxT(" (restricting)")
					: role.m_strRoleName));

		user.AddField(wxT("roles"), ibDataValue::Array(roles));

		if (who.IsSetLanguage())
			user.SetValue(wxT("language"), who.m_strLanguageName);
	}

	ibMetaDataConfigurationBase* metaData = activeMetaData;

	if (metaData == nullptr || !metaData->IsConfigOpen()) {
		into.AddField(wxT("configurationOpen"), ibDataValue::Bool(false));
		return;
	}

	into.AddField(wxT("configurationOpen"), ibDataValue::Bool(true));

	if (ibValueMetaObject* root = metaData->GetCommonMetaObject()) {

		into.SetValue(wxT("configuration"), root->GetName());
		if (!root->GetSynonym().IsEmpty())
			into.SetValue(wxT("synonym"), root->GetSynonym());

		// ⭐ AND ITS ID, because the root is where the direction is written and nothing else
		// reports it: it appears in no listing (it is not of a listable KIND) and in no answer
		// about anything else. Telling a caller to write on the root without this is telling them
		// to address an object they have no way to name.
		into.AddField(wxT("configurationId"), ibDataValue::Int((s64)root->GetMetaID()));
	}

	// ⭐⭐ THE ONE A CALLER MUST RE-ASK. Everything else here drifts slowly; this can be flipped
	// between two calls, and code written in the other dialect is rejected by the compiler while
	// reading perfectly to a human.
	into.SetValue(wxT("dialect"), wxString(
		ibConfigurationWritesInWords(metaData) ? wxT("word-fenced") : wxT("c-style")));

	std::vector<ibDataValue> languages;

	for (const wxString& language : ibListMetaObjectNames(metaData, wxT("Language")))
		languages.push_back(ibDataValue::String(language));

	into.AddField(wxT("languages"), ibDataValue::Array(languages));

	// 🛑 A NUMBER IS NOT AN ANSWER HERE. This reported 1001, which reads as a build or a revision
	// and is neither — it is the enumeration member `version_oes_last`, whose label is "Don't use
	// compatibility". So the figure hid the very fact it was supposed to carry: that compatibility
	// mode is switched OFF. A reader given 1001 could not have guessed it and had nowhere to look.
	//
	// ⭐ ASKED OF THE PROPERTY, which is the one thing that knows its own words — the same road
	// metadata_properties takes for every enumerated property. Not decoded here from the integer:
	// that would be this file keeping a private copy of a list the enumeration already owns, and
	// the copy would be wrong the first time a member was added.
	into.AddField(wxT("compatibilityVersion"), ibDataValue::Int((s64)metaData->GetVersion()));

	if (ibValueMetaObject* root = metaData->GetCommonMetaObject()) {

		// ⭐ ASKED BY NAME. This walked every property of the root comparing names — which is what
		// GetProperty(name) does, on the object that owns them.
		if (const ibProperty* property = root->GetProperty(wxT("Version"))) {

			// ⭐⭐ THE PROPERTY HANDS OVER ITS RUNTIME VALUE, AND THE VALUE SPEAKS FOR ITSELF.
			// `GetDataValue` is pure virtual on the base — EVERY property has it — and what comes
			// back for an enumerated one is the enum member itself, which already knows its own
			// word. So there is no matching of ids against labels here, no cast to the enum
			// property, and nothing that would need extending when a member is added.
			//
			// 🛑 The first version of this walked GetEnumList comparing ids — the same hand-rolled
			// shape that exists in several other places in this server. It works, and it is a
			// private re-implementation of what the value does for itself.
			ibValue held;

			if (property->GetDataValue(held))
				into.SetValue(wxT("compatibility"), held.GetString());
		}
	}

	// ⭐⭐ TWO QUESTIONS THAT ONE WORD WOULD HIDE. "Saved" sounds like one state and is two, with
	// different consequences and different cures:
	//
	//   unsavedEdits  - changed since it was last STORED. Close the designer now and this is
	//                   gone. `config_save` is the cure, and it is cheap.
	//   inDatabase    - the same as the copy the DATABASE holds. False means the running
	//                   application is still on the old configuration however much was saved.
	//                   `config_apply` is the cure, and it writes DDL.
	//
	// Reporting only one of them is how work is lost: today a note written through this server
	// survived every check a caller could make and died with the process, because nothing
	// answered the first question at all.
	// ⚠ THIS IS NOT "UNSAVED", THOUGH IT WAS REPORTED AS THAT AND THEN, WRONGLY, AS "TOUCHED SINCE
	// OPENED". The flag means "differs from the copy the DATABASE holds", and it is maintained at
	// all three points that can change that answer:
	//
	//   LOAD    — `Modify(!CompareMetadata(m_configMetadata))`, a real comparison
	//             (ibMetaDataConfigurationStorage::LoadDatabase)
	//   EDIT    — set true by whatever changed something
	//   APPLY   — set FALSE once the configuration blob is committed
	//             (metadataConfigurationQuery.cpp, inside the transaction guard)
	//
	// ⭐ AND IT STAYS TRUE AFTER A PLAIN SAVE, CORRECTLY. `config_save` persists the configuration
	// so it survives a re-login; it does NOT touch the database's copy, so the difference it
	// describes is still there. Nothing is failing to reset — the condition has not changed.
	//
	// What genuinely has no answer anywhere is whether that diskette save has happened, and
	// `storedUnknown` below says so rather than guessing.
	const bool differs = metaData->IsModified();

	into.AddField(wxT("differsFromDatabase"), ibDataValue::Bool(differs));

	// ⭐ ASKED OF THE CONTAINER, and there is nothing to recognise first. The ACTIVE metadata is a
	// configuration by definition — an external report or data processor is a container of the BASE
	// metadata class and never this one — so the cast to the storage class tested something that is
	// always true and then reported `inDatabase` only when it happened to hold.
	//
	// ⚠ AND NOT `GetConfiguration() != nullptr` EITHER, which was the other thing tried here: that
	// answers "is there a designer-side BASELINE", and it is null in the RUNTIME — precisely where
	// this field matters most. IsConfigSave is virtual on the base and is the question itself.
	const bool applied = metaData->IsConfigSave();

	into.AddField(wxT("inDatabase"), ibDataValue::Bool(applied));

	// ⭐⭐ THE STATE AS A WORD, not two flags for the caller to combine. Two booleans carry three
	// meaningful situations, and leaving them to be folded outside is how one of the three gets
	// forgotten — the reader tests the flag they thought of and calls it done. The flags stay,
	// because a caller may want either half; the WORD is what the answer is about.
	// 🛑 AND THE WORD READ ONLY ONE OF THE TWO. Three situations, and this folded `differs` away:
	// a configuration that HAS been applied and has been edited since answered `inStep`, "nothing
	// is pending against it", in the same breath as `differsFromDatabase: true` two lines above.
	// A caller reading its own answer found it arguing with itself and had no way to tell which
	// half to believe (measured over MCP, 2026-09-03).
	if (!applied) {
		into.SetValue(wxT("state"), wxString(wxT("storedNotApplied")));
		into.SetValue(wxT("meaning"),
			ibMcpText("Stored - it survives closing the designer - but the DATABASE still holds an older "
			  "configuration, so the running application does not have any of it. "
			  "`database_diff` says what differs; `config_apply` writes it."));
	}
	else if (differs) {
		into.SetValue(wxT("state"), wxString(wxT("editedSinceApplied")));

		// ⚠ AND A DIFFERENCE IS NOT ALWAYS A SCHEMA DIFFERENCE, which is why this says which
		// question `database_diff` answers. A comment, a note, a module's text all change the
		// configuration and change no table, so the diff can legitimately answer "0" while this is
		// true — and a reader who takes the diff for the whole answer concludes the flag is broken.
		into.SetValue(wxT("meaning"),
			ibMcpText("The database's copy is behind this one: it was applied, and edited since. The "
			  "running application still works from what was applied. `database_diff` says what "
			  "of it reaches the SCHEMA - a comment, a note or a module changes the configuration "
			  "and no table, so an empty diff here means the edits are not structural, not that "
			  "there are none. `config_apply` puts them in."));
	}
	else {
		into.SetValue(wxT("state"), wxString(wxT("inStep")));
		into.SetValue(wxT("meaning"),
			ibMcpText("The database holds this configuration - nothing is pending against it."));
	}

	// ⚠ THE ONE THING NOTHING HERE CAN ANSWER, said rather than left as a gap for a caller to
	// fill with an assumption. `config_save` is cheap and doing it again costs nothing, which is
	// the only safe advice when the question itself has no answer.
	if (differs)
		into.SetValue(wxT("storedUnknown"),
			ibMcpText("This differs from the database's copy - but whether it has been STORED to survive "
			  "closing the designer is a different question, and nothing tracks it: a plain save "
			  "leaves this difference exactly as it is. If it matters that the edits outlive the "
			  "process, call `config_save`: it is cheap and harmless to repeat."));

	// ⭐ AND WHETHER ANYTHING IS BEING WATCHED. Breakpoints are set against a runtime, not against
	// a configuration: with nothing attached they are set and never reached, which looks exactly
	// like code that does not run. `debug_sessions` gives the list; this is the count, so a
	// caller learns from ONE call whether that whole road is open.
	if (debugClient != nullptr) {

		int seen = 0;
		int attached = 0;

		for (const auto* connection : debugClient->GetListConnection()) {

			if (connection == nullptr || !connection->IsConnected())
				continue;

			seen++;

			if (connection->GetConnectionType() == ConnectionType_Debugger)
				attached++;
		}

		ibDataNode& debugger = into.Child(wxT("debugger"));
		debugger.AddField(wxT("runtimes"), ibDataValue::Int((s64)seen));
		debugger.AddField(wxT("attached"), ibDataValue::Int((s64)attached));

		if (seen > 0 && attached == 0)
			debugger.SetValue(wxT("note"),
				ibMcpText("Something is running but nothing is attached - breakpoints will not be "
				  "reached. `debug_attach` takes it."));
	}
}

namespace {

// A ceiling, because this is read IN FULL at every connection. Generous enough that an ordinary
// configuration is never cut, small enough that a runaway one cannot make connecting expensive.
const size_t kOrientationNotesLimit = 20000;


wxString BuildOrientation()
{
	wxString out;

	out << wxT("You are connected to a running OES Enterprise DESIGNER (build ")
		<< (unsigned)GetBuildId()
		<< wxT("), working inside the session of the person at it, with exactly their rights. ")
		<< wxT("What you change is their live configuration.\n\n");

	// ⭐ WHOSE HANDS THESE ARE. "With their rights" was true and useless without a name: an
	// assistant that does not know who it is acting as cannot tell a refusal it caused from a
	// permission it never had, and cannot warn that something is about to be recorded against a
	// particular person. It is also simply the truth of the arrangement — there is no service
	// account here, no separate identity to hide behind.
	//
	// ⚠ THE PASSWORD FIELD IS IN THE SAME STRUCTURE and is deliberately not touched. Nothing about
	// what a caller may do needs it.
	// ⚠ Through `appData`, not the class: GetUserInfo is an instance method — who is logged in is
	// a fact about a RUNNING application, not about the type.
	const ibUserInfo& who = appData->GetUserInfo();

	if (who.IsOk()) {

		out << wxT("YOU ARE ACTING AS ") << who.m_strUserName;

		if (!who.m_strUserFullName.IsEmpty() && who.m_strUserFullName != who.m_strUserName)
			out << wxT(" (") << who.m_strUserFullName << wxT(")");

		out << wxT(".");

		if (who.m_roleArray.empty()) {
			// NO ROLES IS NOT NO RIGHTS. With none assigned every right falls back to its own
			// declared default, which is usually permissive — worth saying, because "no roles"
			// reads like "can do nothing" and the opposite is nearer the truth.
			out << wxT(" No roles are assigned, so every right answers with its declared default ")
				<< wxT("- `metadata_rights` says what those are.");
		}
		else {
			out << wxT(" Roles: ");

			bool first = true;

			for (const ibUserInfo::ibUserRole& role : who.m_roleArray) {
				if (!first)
					out << wxT(", ");
				out << role.m_strRoleName;
				if (role.m_mode == ibRoleCompositionMode_Intersection)
					out << wxT(" (restricting)");
				first = false;
			}

			out << wxT(". `role_rights` says what they let through.");
		}

		out << wxT("\n\nEverything you do is written to the registration journal under that name, ")
			<< wxT("beside what they did themselves. There is no separate identity for an ")
			<< wxT("assistant, by design: an actor that can change a configuration leaves the ")
			<< wxT("same trail a person does. (Reading it back is `journal_read` - see WRITE DOWN ")
			<< wxT("WHAT YOU FIND below.)\n\n");
	}
	else {
		// The same absence, said in the same breath as it would have been reported. Skipping the
		// paragraph would have left "with exactly their rights" above pointing at nobody.
		out << wxT("THERE ARE NO ACCOUNTS in this base yet, so nobody is logged in - it opens ")
			<< wxT("without asking. Ordinary, and not a restriction: with no roles to fold, every ")
			<< wxT("right answers with its own declared default, which is the permissive end. ")
			<< wxT("`user_list` and `metadata_rights` are where that stops being a guess.\n\n");
	}

	out << wxT("FINDING THE TOOLS. Only two are listed: `mcp_search` finds by words in a name or ")
		<< wxT("description, `mcp_call` {tool, arguments} invokes anything it found. The rest are ")
		<< wxT("deliberately not announced - the whole set costs some 75 KB of schemas, and most ")
		<< wxT("of it is never used. `mcp_search` with no query lists everything.\n")

		// ⭐ SAID HERE TOO, not only in the argument's own description. This paragraph is what a
		// caller reads BEFORE its first call; the schema is read after, or not at all. The finder
		// has answered patterns since it was written, and the one place that said so was the last
		// place anyone looks.
		<< wxT("A query that carries | \\ [ ] ^ $ or .* is read as a REGULAR EXPRESSION rather ")
		<< wxT("than as words - `lock|block`, `report.*print` - and that holds for BOTH searches ")
		<< wxT("this server does: the tools, and the pattern corpus that answers questions of the ")
		<< wxT("trade. Ordinary punctuation does not switch modes, so a sentence with brackets in ")
		<< wxT("it stays a sentence.\n\n");

	// ⭐⭐ HIGH UP BECAUSE THIS TEXT IS DELIVERED WHOLE OR NOT AT ALL. A client puts `instructions`
	// into its prompt and several of them CUT IT — measured 2026-09-03, the copy in a live session
	// ended mid-sentence around "what a PERSON sees", losing everything after it, which is where
	// this paragraph and the notes discipline used to stand. So the order is by SECOND ROAD, not by
	// importance: what a caller can find another way goes lower.
	//
	// The patterns have one — every mcp_search answer carries `places` from the corpus beside the
	// tools — but it is worth the lines here, and the FRAMING is what those lines buy.
	//
	// 🛑 IT SAID "BEFORE CHOOSING A SHAPE" and nothing else (Max, 2026-09-03: *"он должен знать,
	// что у него есть возможность подглянуть в паттерны, как это всё пишется"*). That names ONE
	// moment, so a model outside that moment — writing a query, laying out a form, wondering how a
	// document chains — has no reason to think the door applies to it. The corpus is sixty-seven
	// entries covering the whole trade, and the sentence was selling one of them. What a reader
	// needs first is the RANGE; the metatype advice is a line inside it, not its title.
	out << wxT("HOW THINGS ARE DONE HERE IS WRITTEN DOWN - `pattern_read`. It is the practice of ")
		<< wxT("building on this platform, nearly seventy entries of it, and it is there to be ")
		<< wxT("LOOKED AT rather than deduced: which metatype a request is asking for (a period, a turnover, ")
		<< wxT("a balance, an accountant's correspondence), how a document posts and what it may ")
		<< wxT("touch, prices and discounts, payroll, VAT, lots and serial numbers, settlements, ")
		<< wxT("closing a period, how queries are written here, how names are chosen, how a form is ")
		<< wxT("laid out, what shapes a report takes, printing, roles and row-level access.\n")
		<< wxT("`pattern_read` with no argument LISTS the topics - one call, and you know what is ")
		<< wxT("covered; with a `name` it gives one in full; with a `query` it searches inside them. ")
		<< wxT("They are recommendations, not rules, and they are written for exactly the moment ")
		<< wxT("before you invent something: look first, then decide. The choice of shape in ")
		<< wxT("particular is the one decision that is expensive to revisit once there is data in ")
		<< wxT("it.\n\n");

	// ⭐⭐ THE DISCIPLINE TRAVELS WITH THE SERVER, not with whoever happens to remember it: every
	// client that connects is told to keep the record up, so the next one arrives better informed
	// than this one did. That is the only way a record like this survives more than one person.
	//
	// ⚠ AND IT HAS NO SECOND ROAD — which is why it moved here from the far end of this text.
	// mcp_search points at the patterns in every answer; NOTHING points at note_write at the moment
	// a finding appears, so a session that got a cut copy simply does not know the discipline
	// exists. Highest-value paragraph, therefore first.
	out << wxT("WRITE DOWN WHAT YOU FIND - a configuration records what was built and never WHY, so ")
		<< wxT("what you worked out dies with your session unless you put it somewhere. Four ")
		<< wxT("surfaces, different readers:\n")
		<< wxT(" * `note_write` {id: <root>, target: \"notes\"} - what is true of the WHOLE ")
		<< wxT("configuration: how it is put together, what was decided against, where to look ")
		<< wxT("next. Read first, before any object.\n")
		<< wxT(" * `note_write` {id, target: \"notes\"} on EACH OBJECT - why it exists, which shape ")
		<< wxT("was chosen, what was rejected. On the object, not on the root: nobody re-reads the ")
		<< wxT("root looking for them. `note_read` gives them back.\n")
		<< wxT(" * `target: \"help\"` - what the person USING the application reads on F1.\n")
		<< wxT(" * `Comment` (metadata_set) - one line on any node, WHY THIS EXISTS AT ALL. It ")
		<< wxT("rides along with metadata_tree, so one read of the map shows what everything is for.\n")
		<< wxT("Markdown, so `[Goods](oes:1005)` names another object: the link survives a rename. ")
		<< wxT("And what you DID is kept for you - `journal_read` says what was actually done, and ")
		<< wxT("tells your changes from somebody else's.\n\n");

	ibMetaDataConfigurationBase* metaData = activeMetaData;

	if (metaData == nullptr || !metaData->IsConfigOpen()) {
		out << wxT("NO CONFIGURATION IS OPEN in this designer yet.\n");
		return out;
	}

	// WHAT KIND OF PLACE THIS IS, in a few lines. A caller that knows there are documents and
	// registers but no charts of accounts asks better first questions.
	std::map<wxString, int> census;
	std::vector<std::pair<wxString, wxString> > notes;   // object path, its notes

	for (ibValueMetaObject* object : metaData->GetAnyArrayObject()) {

		if (object == nullptr || object->IsDeleted())
			continue;

		census[object->GetClassName()]++;

		const wxString written = object->GetNoteContent();
		if (!written.IsEmpty())
			notes.push_back(std::make_pair(
				object->GetClassName() + wxT(".") + object->GetName(), written));
	}

	// ⭐⭐ THE ROOT IS NOT ONE MORE OBJECT WITH A NOTE ON IT — it is where the configuration says
	// what it is FOR: the direction of the whole thing, the concept, what is being built and why.
	// Everything below is a detail of that, so it goes FIRST and it is named as the starting
	// point, rather than being sorted in among the catalogs.
	//
	// (It is also not in the array, the same absence that made role_grant silently skip the
	// administration rights.)
	wxString direction;
	wxString called;

	if (ibValueMetaObject* root = metaData->GetCommonMetaObject()) {

		direction = root->GetNoteContent();

		// ⭐ WHAT IT IS CALLED, WHICH IS WHERE READING IT STARTS. "Warehouse" already narrows
		// every later question; the synonym is what a person calls it, the name is what code
		// writes, and both are worth having before the first one is asked.
		called = root->GetName();

		const wxString synonym = root->GetSynonym();
		if (!synonym.IsEmpty() && synonym != called)
			called += wxT(" (") + synonym + wxT(")");
	}

	if (!called.IsEmpty()) {

		out << wxT("THIS CONFIGURATION IS CALLED ") << called;

		// The id, said where the name is said. The root appears in no listing — it is not of a
		// kind that can be listed — so this is the only place a caller learns how to address the
		// object it is told below to write the direction on.
		if (ibValueMetaObject* root = metaData->GetCommonMetaObject())
			out << wxT(", and its root has NodeId ") << (int)root->GetMetaID()
				<< wxT(" (it is in no listing; that is how you address it)");

		out << wxT(".\n\n");
	}

	// ⭐⭐ TWO FACTS THAT DECIDE WHETHER WHAT YOU WRITE COMPILES AT ALL, and neither can be guessed
	// from anything else. The DIALECT is a genuine fork - the same platform reads `If … Then …
	// EndIf` or `if (…) { … }` depending on what this configuration declared, and handing somebody
	// the other one produces code that reads correctly and is rejected. The LANGUAGES decide where
	// a caption goes: a synonym is an array BY language, so writing one without knowing the codes
	// puts it under the wrong heading or nowhere.
	// ⚠ EVERYTHING BELOW IS A SNAPSHOT, and this is the one place that says so. `instructions` is
	// read ONCE, at the handshake; the dialect is a property a person can change from the
	// configuration's own settings at any moment, and after that this paragraph is confidently
	// wrong. There is no "instructions changed" in the protocol — so instead of pretending the
	// snapshot stays true, it names the tool that answers the same question live.
	out << wxT("HOW THIS CONFIGURATION IS WRITTEN - true when you connected. `platform_state` ")
		<< wxT("answers all of it again at any moment. Ask it at two points, not one:\n")
		<< wxT(" BEFORE WRITING CODE - the dialect can be switched while you are connected, and ")
		<< wxT("the other one is rejected by the compiler while reading perfectly.\n")
		<< wxT(" BEFORE SAYING SOMETHING IS DONE - `unsavedEdits` means it dies when the ")
		<< wxT("designer closes, `inDatabase: false` means the running application never got it. ")
		<< wxT("Both are silent, and \"done\" is false in each of them for a different reason ")
		<< wxT("with a different cure.\n");

	out << wxT("Script dialect: ");

	if (ibConfigurationWritesInWords(metaData))
		out << wxT("WORD-FENCED (`If ... Then ... EndIf`, `Procedure ... EndProcedure`).");
	else
		out << wxT("C-STYLE (`if (...) { ... }`, braces and semicolons).");

	out << wxT(" Write in that one - the other is a different language to the compiler, and ")
		<< wxT("`script_check` is how you find out before anyone runs it.\n");

	const std::vector<wxString> languages = ibListMetaObjectNames(metaData, wxT("Language"));

	if (!languages.empty()) {

		out << wxT("Languages declared: ");

		for (size_t index = 0; index < languages.size(); ++index)
			out << (index == 0 ? wxT("") : wxT(", ")) << languages[index];

		out << wxT(".\n");
	}

	// ⭐⭐ THE LINE BETWEEN WHAT IS WRITTEN AND WHAT IS SHOWN, and it is not a style preference —
	// it is where the configuration stops being portable. A NAME travels into script, into query
	// text, into the database schema; written in anything but English it makes every module that
	// touches it unreadable to the next person and unquotable in a query. A SYNONYM is what a
	// person sees, and it belongs in their language.
	//
	// Said in the orientation because a model working with a Russian-speaking developer will
	// otherwise mirror their language into the names, helpfully and wrongly, and the mistake is
	// expensive to undo once modules reference it.
	out << wxT("NOTE: THE CONFIGURATION IS WRITTEN IN ENGLISH. Names of objects, attributes, tabular ")
		<< wxT("sections and modules - and the code inside them - are English, always: a name goes ")
		<< wxT("into script, into query text and into the database schema, and it is what the next ")
		<< wxT("person has to read.\n")
		<< wxT("What is NOT English is what a PERSON sees: synonyms (`metadata_set` on Synonym, ")
		<< wxT("with `language` to fill one at a time), help text, and anything you say in the ")
		<< wxT("assistant window. Answer them in ")
		<< wxT("their language; name things in English.\n");

	// ⭐⭐ AND THE LANGUAGES ARE A THING TO SET UP, NOT A SETTING TO READ. A fresh configuration is
	// created with English alone, so a person working in Russian or Ukrainian has a base whose
	// interface can only ever speak English — and every synonym written before their language
	// EXISTS is written into English, silently, because `language` had nowhere else to put it.
	// Then adding the language later leaves every caption they see empty.
	//
	// The order is the whole of it: DECLARE THE LANGUAGE FIRST, then write captions per language
	// from that moment on. Said here because the cost of the wrong order is paid once per object,
	// by hand, months later.
	// 🛑 WRITTEN AS THREE STEPS BECAUSE THE RULE ALONE DID NOT WORK. It stood here as a sentence -
	// "a Synonym without `language` goes to the configuration's own" - and the very next assistant
	// to read it (the one that wrote it, 2026-09-03) still put Russian captions into the ENGLISH
	// slot on fourteen objects and on the Russian language itself. A rule is read; a STEP is done.
	out << wxT("CAPTIONS AND LANGUAGES, IN THREE STEPS - the third is where everybody slips:\n")
		<< wxT(" 1. ASK which language the people USING this read. Not the developer's by default, ")
		<< wxT("and possibly several.\n")
		<< wxT(" 2. DECLARE IT FIRST - `metadata_create` {kind: \"Language\", name: \"Russian\"}, an ")
		<< wxT("English name like any other, then set its `Code` (`ru`, `uk`, `en`).\n")
		<< wxT(" 3. WRITE EVERY CAPTION WITH `language`. Omitting it is not \"all languages\": it ")
		<< wxT("writes into the configuration's own - English in a fresh base - so a Russian caption ")
		<< wxT("is stored as the English one and its reader sees nothing.\n\n");

	if (who.IsOk() && !who.m_strLanguageName.IsEmpty())
		out << wxT("The person is working in ") << who.m_strLanguageName
			<< wxT(" - answer them in it, whatever language this text is in.\n");

	// ⚠ THE VERSION A CONFIGURATION DECLARES is not decoration: it is what the platform gates
	// behaviour on, both in what code may say and in what schema is produced. Raising it IS the
	// migration, and it is the PERSON's step to take - so it is reported and never touched.
	out << wxT("Compatibility version: ") << (int)metaData->GetVersion()
		<< wxT(". It gates what the platform allows; raising it is the developer's own decision ")
		<< wxT("and is itself the migration - never change it on their behalf.\n\n");

	// ⛔ THE CENSUS USED TO BE SPELLED OUT HERE — "8 Catalogs, 3 Documents, 2 Registers…" — and it is
	// the clearest example of what does NOT belong in a text paid for on every connection: it is
	// CONSULTABLE. `metadata_tree {depth: 1}` answers it exactly, on demand, in the caller's own
	// moment of needing it. What stays is the pointer, because knowing WHERE to ask is the part
	// nobody can look up.
	out << wxT("WHAT IS IN IT: `metadata_tree` with depth 1 is the map - the objects and their ids, ")
		<< wxT("without the predefined attributes that make up most of a deeper walk.\n\n");

	// THE STARTING POINT, before any detail. A reader who has this needs far less of the rest.
	if (!direction.IsEmpty()) {
		out << wxT("WHAT THIS CONFIGURATION IS FOR - written on its root, and the place to begin. ")
			<< wxT("It is the concept: what is being built, which way it is going, what it is ")
			<< wxT("deliberately not. Everything below is a detail of it.\n\n")
			<< direction << wxT("\n\n");
	}

	// ⭐ AN EMPTY ROOT IS A TASK, NOT A FACT ABOUT THE PLACE. It means nobody wrote the direction
	// down yet — a configuration just started, or one that grew by objects being copied in — and
	// the useful thing to do about it is not to note the absence and carry on. Reconstruct it from
	// what is actually there and write it, marking that it was reconstructed rather than declared,
	// so the person can correct it instead of inheriting your guess as their own statement.
	if (direction.IsEmpty()) {
		out << wxT("THE ROOT SAYS NOTHING about what this configuration is for. That is ordinary ")
			<< wxT("- a new base, or one that grew by objects being copied in - and it is ")
			<< wxT("something to FIX rather than to note: read the tree, work out what is being ")
			<< wxT("built, and write it on the root with `note_write`. Say plainly that you ")
			<< wxT("reconstructed it, so the person corrects it instead of inheriting your guess ")
			<< wxT("as their own words. It is the first thing the next arrival reads.\n\n");
	}

	if (notes.empty() && direction.IsEmpty()) {
		out << wxT("Nothing is written on the individual objects either. You are the first here; ")
			<< wxT("see the standing instruction below.\n\n");
	}
	else if (!notes.empty()) {
		// ⛔ THE NOTES THEMSELVES ARE NO LONGER PASTED IN. They were, up to a limit, with a "[...]
		// more than fits here" when they overflowed — which is the shape of something that does not
		// belong in a handshake: it is paid for by every connection, it is TRUNCATED anyway, and a
		// verb already answers it in full at the moment of asking. What cannot be looked up is that
		// they EXIST and are worth reading before proposing anything, so that is what stays.
		out << wxT("WHAT WAS LEARNED BUILDING IT: ") << (int)notes.size()
			<< wxT(" object(s) carry technical notes - why a thing exists, which shape was chosen, ")
			<< wxT("what was rejected. `note_read` gives them, whole. Read them before proposing ")
			<< wxT("anything: a configuration records what was built and never why.\n\n");
	}

	// (Where each kind of text belongs is stated near the top, above the cut. Not repeated here —
	// a second copy of a discipline is a second thing to keep true.)

	// ⭐⭐ POINTED AT, NOT INLINED — and this is the line that decides how big this text can get.
	//
	// Guidance on HOW TO READ A REQUEST — which shape a described need asks for, how to take a
	// printed form apart — grows with every conversation with someone who knows the domain, and it
	// is the fastest-growing material there is. Inlined here it would be paid for on every
	// connection by every client, needed or not.
	//
	// ⭐⭐ THE ONE THING A CONNECTING MODEL IS RELIABLY WRONG ABOUT. It arrives fluent in languages
	// that are not this one and will write something that READS correctly and does not compile —
	// the failure that costs the most, because it looks like success until it is run. The platform
	// documents itself: the reference the syntax helper shows is the same one these tools read,
	// and the compiler can be asked without running anything. Said here so nobody has to be told
	// it by a person, which is what happens today.
	out << wxT("THE PLATFORM IS NOT ONE YOU KNOW. Its script language, query language and type ")
		<< wxT("system are its own - do not carry over the shape of anything familiar. It ")
		<< wxT("documents itself, and every one of these reads the same reference the designer's ")
		<< wxT("syntax helper shows:\n")
		<< wxT(" `syntax_search` / `syntax_get` - the language reference, function by function\n")
		<< wxT(" `type_list` / `type_members` - what a value IS and what it can do\n")
		<< wxT(" `linq_methods` - the query pipeline operations\n")
		<< wxT(" `metadata_list` / `metadata_get` - the configuration tree, kind by kind\n")
		<< wxT(" `pattern_read` - how things are USUALLY done here, in the words of the trade; the ")
		<< wxT("one above that is about practice rather than syntax\n")
		<< wxT(" `script_check` / `query_check` - COMPILE without running. Ask them before you ")
		<< wxT("believe anything you wrote; a script that parses in your head is not evidence.\n\n");

	// ⭐⭐ THE TWO THINGS A NEWCOMER DOES NOT FIND BY SEARCHING, measured on one (2026-09-03, an
	// assistant building a stock area from an empty base):
	//
	// A RELATIONSHIP IS NOT A PROPERTY, and `metadata_set` says so by refusing — but the refusal
	// does not name the verb that CAN do it. Searching for the job ("recorder", "movements",
	// "which register this document writes") found nothing, and the conclusion drawn was that the
	// platform could not express it at all: an hour, and a wrong report to the person.
	//
	// AND "BUILT" IS NOT "SAVED". Everything above answers happily while the configuration is
	// unsaveable — the check that says so is one call and nobody thinks to make it.
	// ⭐⭐ AND THE ONE THAT MAKES THE DIFFERENCE BETWEEN BUILDING AND KNOWING IT WORKS. An assistant
	// can create every metaobject and still never see the thing RUN — measured 2026-09-03: the
	// verbs were searched for with the words of the job ("post a document", "write data") and
	// app_run does not answer to those, so the whole runtime half stayed invisible for a day.
	out << wxT("AND YOU CAN RUN THE APPLICATION YOURSELF - `app_run` starts it (with the debugger ")
		<< wxT("attached by default), `debug_breakpoint` puts a stop where you want to look, ")
		<< wxT("`debug_state` says when it is standing there, and `debug_sandbox` then runs whole ")
		<< wxT("statements INSIDE it - create data, post a document, read a register back. It runs ")
		<< wxT("in a transaction that is ALWAYS rolled back, so it costs nothing to try. This is ")
		<< wxT("the only way to find out that what you built actually works: metadata that saves ")
		<< wxT("and applies can still post nothing.\n")
		// 🛑⭐ THE TRAP THAT COSTS THE MOST, because it does not look like one: the answer arrives,
		// it is simply the OLD answer. Measured twice in one evening (2026-09-04) — a module was
		// rewritten, the run was asked again, and it replied from the version it started with.
		<< wxT("A RUNNING APPLICATION KEEPS THE BYTECODE IT CAME UP WITH, so after changing a ")
		<< wxT("module start it again with `app_run` and `restart: true` - otherwise it answers ")
		<< wxT("from the old version and nothing tells you so.\n")
		// ⭐ AND MEASURING IS POSSIBLE AT ALL, which nobody guesses: the language's clock is a
		// BUSINESS clock (a date, to the second), so code cannot time itself and the obvious move —
		// running the same thing ten times from outside and dividing — measures the socket too.
		<< wxT("`debug_sandbox` also answers with `microseconds`, how long the code took, timed ")
		<< wxT("inside the process that ran it. That is the platform's stopwatch: use it instead ")
		<< wxT("of repeating something to time it from out here.\n")
		// ⭐⭐ THE ONE NOBODY WOULD GUESS AT, and the one that answers the commonest report of all.
		// Named here because a new session has no memory of this conversation and would never think
		// to ask a tool for a PICTURE (Max, 2026-09-04).
		<< wxT("AND YOU CAN SEE WHAT THEY SEE - `screen_capture` asks the running application for a ")
		<< wxT("picture of its window. That is the answer to \"the numbers do not add up\" and \"the ")
		<< wxT("list is wrong\" from somebody who cannot say WHICH form, WHICH period or WHICH ")
		<< wxT("column: they click on the thing, and it comes back ringed and named. Ask for ")
		<< wxT("`area: \"focus\"` and you get just that part, which is smaller and already about the ")
		<< wxT("right thing. It needs the run to be GOING, not stopped - a parked window draws ")
		<< wxT("nothing - and in a shipped build the person is asked first and may say no, which is ")
		<< wxT("an answer rather than a fault. What it CANNOT tell you is why a number is what it ")
		<< wxT("is: use it to find the question, then `trace_read` and `debug_sandbox` to answer it.\n")
		// ⭐ AND WHEN THE ANSWER IS WRONG RATHER THAN ABSENT, the engine has already written down
		// why. Named here because it is a FILE and therefore covers the run being debugged as well
		// as this one — deducing from a wrong number what SQL produced it is hours of work that a
		// single read replaces.
		<< wxT("When a run answers something you did not expect, `trace_read` has the engine's own ")
		<< wxT("commentary on it - the SQL exactly as sent, the road the query took, the keys a ")
		<< wxT("join was stitched on - for the application under the debugger as well as for this ")
		<< wxT("designer. (`journal_read` is the other journal: what the INSTALLATION did - logins, ")
		<< wxT("writes, postings - the record an auditor reads.)\n\n");

	out << wxT("THREE VERBS NOTHING LEADS YOU TO, and each is a step you will otherwise miss:\n")
		<< wxT(" `metadata_bind` - WHAT POINTS AT WHAT: which registers a document writes to, ")
		<< wxT("which catalog owns another, what is entered on the basis of what. They hold ")
		<< wxT("metaobjects, so `metadata_set` cannot say them. A document bound to no register ")
		<< wxT("posts nothing, and a register with no recorder will not save.\n")
		<< wxT(" `metadata_accepts` - what may live inside a kind, and what an object still ")
		<< wxT("lacks. Ask before creating, not by trying kinds.\n")
		<< wxT(" THE FIRST ONE IS THE DEFAULT - the first composer, the first form of each kind. A ")
		<< wxT("later one does not take over: move it with `metadata_set` on `DefaultComposer` / ")
		<< wxT("`DefaultFormObject` / …, which are ordinary lists. A form's MAIN ATTRIBUTE has its ")
		<< wxT("own verb, `form_attribute`.\n")
		<< wxT(" `config_check` -> `config_save` -> `config_apply` - half-built / outlives the ")
		<< wxT("designer / reaches the running application. Three different questions; none ")
		<< wxT("implies the others.\n\n");

	out << wxT("THE CONVERSATION. A person may be typing to you in the designer's assistant ")
		<< wxT("window: `chat_take` collects new messages (and EMPTIES the queue), ")
		<< wxT("`chat_history` reads the whole exchange back without collecting, `chat_say` ")
		<< wxT("answers into their window.\n\n");

	// ⭐⭐ SAY HELLO, AND SAY IT FIRST. There is a person sitting in front of this window, and from
	// their side a connection is INVISIBLE unless something says so — an assistant that reads the
	// whole configuration in silence and waits to be addressed looks exactly like one that never
	// arrived. Greeting is not manners here; it is the only evidence they get.
	//
	// ⚠ AND IT SAYS WHAT WAS SEEN, not just that it happened. "Hello" tells them nothing they
	// could act on; naming the configuration and what is in it proves the connection works AND
	// hands them the first thing to correct if it is wrong.
	// ⭐⭐ THE ROUTE, NOT THE REFERENCE. Everything above says what EXISTS; a newcomer with no memory
	// of this project needs to know in what ORDER to use it, and that is the one thing no verb can
	// answer. Written as a sequence and kept to a few lines, because it is paid for on every
	// connection — and because a route that has to be read twice is not one.
	out << wxT("HOW WORK GOES HERE, IN ORDER - the whole shape of it:\n")
		<< wxT(" 1. SAY HELLO and say what you see (below). 2. `metadata_tree` depth 1 - the map. ")
		<< wxT("3. `pattern_read` 'intake' and 'where-to-start' - the questions to ask and the ")
		<< wxT("order things are built in. 4. PUT THE FORKS BACK to the person in their own words, ")
		<< wxT("with a recommendation. 5. Build in the recipe's order, `metadata_accepts` before ")
		<< wxT("creating and `metadata_bind` after. 6. `script_check` / `query_check` before ")
		<< wxT("believing any code. 7. `config_check`, then `config_save`. Write down WHY as you ")
		<< wxT("go - notes on the objects, one line of `Comment` each.\n")
		<< wxT("AND SPEND YOUR READING WHERE IT PAYS. `pattern_read` in layers (no argument lists ")
		<< wxT("the topics; `name` gives one; `topic` gives it in full) - not the whole corpus. ")
		<< wxT("`mcp_search` in the words of the job rather than by guessing a verb. Ask the ")
		<< wxT("configuration about ITSELF (`metadata_tree`, `metadata_get`) rather than reading ")
		<< wxT("everything: a deep walk of a full base is mostly predefined attributes.\n\n");

	out << wxT("FIRST THING, AND NOTHING ELSE RUNS UNTIL IT IS DONE - this is a door, not a ")
		<< wxT("request. Until `chat_say` has happened, every other verb is refused; what runs ")
		<< wxT("before it is exactly what the greeting is made of: `chat_history`, ")
		<< wxT("`platform_state`, `metadata_tree`, `mcp_search`, `chat_say`. A person is sitting ")
		<< wxT("in front of this designer and cannot see that anything connected.\n")
		<< wxT("So: read `chat_history` - you may be joining something ")
		<< wxT("already in progress - and then `chat_say` a greeting with four things in it:\n")
		<< wxT(" WHO YOU ARE. Name yourself - which assistant and which model. The handshake ")
		<< wxT("tells them the client's name and nothing else; what is actually reading their ")
		<< wxT("configuration is something only you can say, and they are entitled to know it.\n")
		<< wxT(" WHAT YOU SEE. The configuration by name and what is in it, in a line or two. ")
		<< wxT("This is also the proof the connection works - a greeting that describes their ")
		<< wxT("base cannot be a stale window.\n")
		<< wxT(" ANYTHING ALREADY WORTH SAYING. Objects in no section, nothing written on the ")
		<< wxT("root, unsaved edits - whatever you noticed on the way in.\n")
		<< wxT(" WHAT NOW. Ask what they want to do, and say you are ready.\n")
		<< wxT("Do NOT wait to be spoken to: from where they sit, an assistant reading everything ")
		<< wxT("in silence is indistinguishable from one that never arrived.\n");

	return out;
}

} // namespace

wxString ibMcpServer::Answer(const wxString& request, const ibMcpWireHeaders& headers, int* outErrorCode)
{
	// THE VERDICT THE TRANSPORT WILL ASK FOR — recorded where the refusal is decided,
	// which is the only place that knows. Reading it back out of the emitted JSON was
	// the first attempt and it failed on a space: the writer emits `"code": -32020`
	// and the search looked for `"code":-32020`.
	if (outErrorCode != nullptr) *outErrorCode = 0;
	const auto refuse = [&](const ibDataValue& id, ibMcpError code, const wxString& text,
		const ibDataNode* data = nullptr) -> wxString {
		if (outErrorCode != nullptr) *outErrorCode = (int)code;
		return ibMcpWriteError(id, code, text, data);
	};
	// ⭐ IT MAY NOT BE A CALL AT ALL. Once the server can ask the client something, the same
	// endpoint starts receiving both — and they are told apart by one fact: a request names a
	// method, a response does not (mcpMessage.h). This has to be asked FIRST, because reading a
	// response as a request finds no method and answers "no method named ''" to what was actually
	// an answer to our own question.
	{
		ibDataValue answeringId;
		wxString    payload;
		bool        wasError = false;

		if (ibMcpParseResponse(request, answeringId, payload, wasError)) {

			// THE MODEL ANSWERED. It goes where every other side of this conversation goes — into
			// the window, through Reply — rather than back to whoever asked, who by design did not
			// sit waiting on a socket for it (see AskModel in the header).
			Reply(wasError
				? wxString::Format(_("The assistant could not answer: %s"), payload)
				: payload);

			// A response is answered with nothing: it IS the answer.
			return wxEmptyString;
		}
	}

	ibMcpRequest parsed;
	wxString     error;

	if (!ibMcpParseRequest(request, parsed, error)) {
		// A malformed message IS worth a line — but the line says what went
		// wrong, not what the bytes were.
		Publish(wxT("did"), wxString::Format(_("unreadable message: %s"), error), wxEmptyString);
		return ibMcpWriteError(ibDataValue(), ibMcpError::Parse, error);
	}

	// ⭐⭐ WHICH ERA IS SPEAKING, decided before the method is looked at — because the answer
	// changes what a refusal must look like, not merely what is done.
	//
	// The specification calls them LEGACY (an `initialize` handshake opens a session; revisions
	// through 2025-11-25) and MODERN (no handshake at all; every request carries its own version
	// in `_meta`, 2026-07-28 onward). A dual-era server is allowed to serve both on one endpoint,
	// and it tells them apart by how the caller opened: metadata present means modern, an
	// `initialize` means legacy. Nothing else is consulted — the era is a property of the
	// REQUEST, and the same client may be either on different days.
	const ibDataNode* meta = parsed.m_params.FindChild(wxT("_meta"));
	const ibDataValue* metaVersion = meta != nullptr
		? meta->FindField(wxT("io.modelcontextprotocol/protocolVersion")) : nullptr;
	const bool isModern = metaVersion != nullptr && metaVersion->Kind() == ibDataKind::String;

	if (isModern) {
		const wxString asked = metaVersion->AsString();

		// THE HEADER AND THE BODY MUST AGREE. Two readers, two sources: an intermediary routes on
		// the header, this server executes on the body. A request where they differ is the one
		// that gets past the first and is carried out by the second.
		if (!headers.m_protocolVersion.IsEmpty() && headers.m_protocolVersion != asked) {
			return refuse(parsed.m_id, ibMcpError::HeaderMismatch,
				wxString::Format(wxT("MCP-Protocol-Version header says '%s' and the body says '%s'"),
					headers.m_protocolVersion, asked));
		}
		if (!headers.m_method.IsEmpty() && headers.m_method != parsed.m_method) {
			return refuse(parsed.m_id, ibMcpError::HeaderMismatch,
				wxString::Format(wxT("Mcp-Method header says '%s' and the body calls '%s'"),
					headers.m_method, parsed.m_method));
		}

		// AND THE VERSION MUST BE ONE WE SPEAK — answered with the list, so the caller can pick
		// another rather than work out why the replies look wrong. This server is legacy-era: it
		// speaks 2025-11-25 and below, so a modern caller is told exactly that and retries as a
		// legacy client, which is the fallback the transport describes.
		bool known = false;
		for (const wxChar* const* v = kSupportedProtocolVersions; *v != nullptr; ++v) {
			if (asked == *v) { known = true; break; }
		}
		if (!known) {
			// The SAME refusal the header check answers with, from the same list — the only
			// difference is that here there is a call to answer, so it carries the id.
			const ibDataNode data = SupportedVersions(asked);
			return refuse(parsed.m_id, ibMcpError::UnsupportedVersion,
				wxT("Unsupported protocol version"), &data);
		}
	}

	// EVERY SERVER MUST ANSWER THIS ONE — the modern era's way of asking "who are you and what do
	// you speak", in a single call and without a handshake. It says the same things `initialize`
	// says, which is why it is answered from the same material rather than from a second one; the
	// difference is that it opens nothing and may be asked at any time, including first.
	if (parsed.m_method == wxT("server/discover")) {

		ibDataNode result;
		result.SetValue(wxT("resultType"), wxString(wxT("complete")));

		ibDataNode& versions = result.Child(wxT("supportedVersions"));
		int at = 0;
		for (const wxChar* const* v = kSupportedProtocolVersions; *v != nullptr; ++v, ++at)
			versions.SetValue(wxString::Format(wxT("%d"), at), wxString(*v));

		// The same capabilities `initialize` reports: tools, and the logging that carries what a
		// person types in the assistant window.
		ibDataNode& capabilities = result.Child(wxT("capabilities"));
		capabilities.Child(wxT("tools"));
		capabilities.Child(wxT("logging"));

		ibDataNode& resultMeta = result.Child(wxT("_meta"));
		ibDataNode& serverInfo = resultMeta.Child(wxT("io.modelcontextprotocol/serverInfo"));
		serverInfo.SetValue(wxT("name"), wxString(wxT("OES Enterprise")));
		serverInfo.SetValue(wxT("version"), wxString::Format(wxT("build %d"), GetBuildId()));

		// The same orientation text initialize hands over - one source, two doors.
		result.SetValue(wxT("instructions"), BuildOrientation());

		return ibMcpWriteResult(parsed.m_id, result);
	}

	wxString answer;

	// THE OUTCOME OF A TOOL CALL, carried out to where the record is written. The call itself is
	// made deep inside the branch below; the journal line is written once, at the bottom, for every
	// tool — so the verdict has to travel that far rather than each branch writing its own.
	bool     toolRan = false;
	bool     toolOk = false;
	wxString toolRefusal;
	wxString toolKind;

	// A POLL THAT TOOK NOTHING. Decided where the answer node is alive and carried out to the
	// journal, because the node is the only thing that knows — see the note at the bottom.
	bool     toolFruitless = false;

	// 🛑 WHAT WAS ACTUALLY CALLED, resolved ONCE. The announcement at the bottom used to re-read
	// the name and the arguments off the request for itself — and so it never saw through an
	// mcp_call envelope, which the executor unwraps. The window then said "calling a tool" for
	// every single call, fifteen times in a row, naming none of them: the tool's own GetActivity
	// was being asked of the ENVELOPE, and the envelope has nothing to say about the work.
	//
	// Two roads to one fact, and only one of them unwrapped. Now there is one.
	wxString   toolName;
	ibDataNode toolArguments;

	if (parsed.m_method == wxT("initialize")) {

		// ⭐ WHAT THE CLIENT SAID IT CAN DO, remembered here because this is the only time it is
		// said. `sampling` is the one that matters: it means the server may ask the client's model
		// a question of its own, which is what turns the assistant window from something that has
		// to be POLLED into something that answers when a person types in it.
		//
		// Read as PRESENCE, not as a value — the protocol declares a capability by naming it, and
		// what is inside the object is the capability's own options.
		if (const ibDataNode* capabilities = parsed.m_params.FindChild(wxT("capabilities")))
			m_clientCanSample.store(capabilities->FindChild(wxT("sampling")) != nullptr);
		else
			m_clientCanSample.store(false);

		ibDataNode result;

		// ⭐ ANSWER IN THE REVISION THAT WAS ASKED FOR, when it is one we speak. Negotiation is
		// the point of this field: the client names what it wants, and a server that always
		// answers with its own newest leaves the client to work out whether that is agreement or
		// a refusal it has to survive. Naming the asked-for version back IS the agreement.
		//
		// A version we do not speak — or none at all, which is what a pre-2025-03-26 client
		// sends — gets ours, and the client decides whether it can live with it. That is the
		// lifecycle's own rule, and it is why this cannot simply refuse.
		wxString agreed(kProtocolVersion);
		if (const ibDataValue* asked = parsed.m_params.FindField(wxT("protocolVersion"))) {
			if (asked->Kind() == ibDataKind::String) {
				const wxString wanted = asked->AsString();
				for (const wxChar* const* v = kSupportedProtocolVersions; *v != nullptr; ++v) {
					if (wanted == *v) { agreed = wanted; break; }
				}
			}
		}
		result.SetValue(wxT("protocolVersion"), agreed);

		// We offer tools and nothing else yet. Saying so plainly is what keeps a
		// client from asking for prompts or resources and getting silence.
		ibDataNode& capabilities = result.Child(wxT("capabilities"));
		capabilities.Child(wxT("tools"));

		// ⚠ AND LOGGING, WHICH WAS BEING DONE WITHOUT BEING DECLARED. Say() writes
		// `notifications/message` on the stream the moment a person types — that IS the logging
		// capability being exercised, and a client is entitled to know it may arrive and to turn
		// it down with `logging/setLevel`. Sending it undeclared meant a strict client had every
		// right to treat the frame as unsolicited.
		capabilities.Child(wxT("logging"));

		ibDataNode& info = result.Child(wxT("serverInfo"));
		info.SetValue(wxT("name"), wxString(wxT("OES Enterprise")));

		// ⚠ THE REAL BUILD, not a "1.0" that was written once and could never be wrong because it
		// never changed. GetBuildId is the same number the About box shows, derived from the
		// build date in one place — so a client reporting a problem names a build that can
		// actually be found.
		info.SetValue(wxT("version"),
			wxString::Format(wxT("build %u"), GetBuildId()));

		// ⭐ ARRIVING ORIENTED, not arriving and then asking. `instructions` is the protocol's own
		// field for the server to say how it should be worked with, and it was left empty — so a
		// client connected, saw TWO tools, and had to discover by trial that the rest are found
		// rather than announced, that the configuration carries written findings, and that a
		// conversation was already in progress.
		//
		// ⚠ WHAT IS HERE IS THE MAP, NOT THE TERRITORY. The census tells a caller what kind of
		// place this is in a few lines; the notes themselves are NOT inlined, because they can run
		// to pages and this is read in full at every single connection. It says they exist and
		// what reads them, which is the part that cannot be guessed.
		result.SetValue(wxT("instructions"), BuildOrientation());

		// ⭐ THE PERSON IS TOLD SOMEBODY ARRIVED. Until now a client could connect, read the whole
		// configuration and start working, and the window in front of the person showed nothing
		// at all — the difference between "nobody is there" and "somebody is reading everything
		// you have" was invisible. Announcing it is the least a window watching this owes them.
		//
		// The client NAMES ITSELF at the handshake; using that is better than "an assistant",
		// because two of them are then distinguishable and neither is anonymous.
		wxString arrival = _("An assistant has connected");

		if (const ibDataNode* client = parsed.m_params.FindChild(wxT("clientInfo"))) {
			const wxString clientName = client->GetValue<wxString>(wxT("name"));
			if (!clientName.IsEmpty())
				arrival = wxString::Format(_("%s has connected"), clientName);
		}

		m_greeted = true;

		Publish(wxT("did"), arrival + wxT("."), wxEmptyString);

		answer = ibMcpWriteResult(parsed.m_id, result);
	}
	else if (parsed.m_method == wxT("tools/list")) {

		ibDataNode result;

		// ⭐ THE FEW THAT LET A CALLER FIND THE REST. Handing over sixty-seven tools with their
		// schemas costs some fifty kilobytes before any work begins, spent in full every session
		// for the dozen actually used — so the handshake announces the finders, and everything
		// else is fetched by name through mcp_search and invoked through mcp_call. Nothing is
		// hidden; it is unannounced, and mcp_search with no query still lists it all.
		std::vector<ibDataValue> tools;
		for (const ibMcpTool* tool : ibMcpTools()) {

			if (!tool->IsAlwaysListed())
				continue;

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("name"), tool->GetName());
			entry->SetValue(wxT("description"), tool->GetDescription());
			tool->DescribeInput(entry->Child(wxT("inputSchema")));

			tools.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("tools"), ibDataValue::Array(tools));
		answer = ibMcpWriteResult(parsed.m_id, result);
	}
	else if (parsed.m_method == wxT("tools/call")) {

		wxString name = parsed.m_params.GetValue<wxString>(wxT("name"));

		// ⭐ THE ENVELOPE IS UNWRAPPED HERE, AND ONLY HERE. mcp_call carries the real tool's name
		// and arguments inside itself, so a caller can invoke something that was never announced
		// at the handshake. Unwrapping it in the SERVER rather than implementing it as a tool is
		// what keeps everything downstream unchanged BY CONSTRUCTION: the undeclared-argument
		// gate, the exception classification, and the journal line all see the real tool and its
		// real arguments, and none of them had to be taught that this door exists.
		//
		// A wrapper that dispatched from inside a tool would have skipped all three, and the trail
		// would read "mcp_call, mcp_call, mcp_call" — which is a record of nothing.
		bool unwrapped = false;
		wxString envelopeRefusal;

		if (name.IsSameAs(wxT("mcp_call"), false)) {

			const ibDataNode* outer = parsed.m_params.FindChild(wxT("arguments"));

			const wxString inner = outer != nullptr
				? outer->GetValue<wxString>(wxT("tool")) : wxString();

			if (inner.IsEmpty()) {
				envelopeRefusal =
					ibMcpText("mcp_call needs `tool` - the name of what to invoke. mcp_search finds it.");
			}
			else if (inner.IsSameAs(wxT("mcp_call"), false)) {
				// A CALL THAT CALLS ITSELF is refused rather than recursed into: the loop would
				// unwrap forever, and there is no reading of it that means anything.
				envelopeRefusal = ibMcpText("mcp_call cannot invoke itself.");
			}
			else if (outer->FindChild(wxT("arguments")) == nullptr
				&& outer->FindField(wxT("arguments")) != nullptr) {

				// 🛑 THE ENVELOPE IS WRONG, AND IT USED TO BE THE LETTER THAT SAID SO. `arguments`
				// arriving as anything but an object - a JSON string, a number - is invisible to
				// FindChild, so the tool ran with NOTHING and complained about its own first
				// required argument: "chat_say needs 'text', and it did not come", of a call that
				// carried it. The caller then looks at the tool, which is fine, instead of at the
				// encoding, which is not.
				//
				// Refused HERE, where the fault is, and named as what it is.
				envelopeRefusal = wxString::Format(
					_("mcp_call's `arguments` must be an OBJECT - the tool's schema filled in - "
					  "and this one is not. Send {tool: '%s', arguments: {...}}, not the arguments "
					  "as a string containing JSON."), inner);
			}
			else {
				name = inner;
				unwrapped = true;
			}
		}

		// A BAD ENVELOPE IS ANSWERED LIKE ANY OTHER REFUSAL, and falls through to the journal at
		// the bottom rather than returning from the middle of the function — an early exit here
		// would leave the one call nobody can explain out of the record entirely.
		const ibMcpTool* tool = envelopeRefusal.IsEmpty() ? ibFindMcpTool(name) : nullptr;

		if (!envelopeRefusal.IsEmpty()) {
			answer = ibMcpWriteError(parsed.m_id, ibMcpError::InvalidParams, envelopeRefusal);
		}
		else if (tool == nullptr) {
			answer = refuse(parsed.m_id, ibMcpError::MethodNotFound,
				wxString::Format(wxT("No tool named '%s'"), name));
		}
		else {
			ibDataNode arguments;

			// The real arguments live one level in when the call arrived wrapped.
			const ibDataNode* given = parsed.m_params.FindChild(wxT("arguments"));

			if (unwrapped && given != nullptr)
				given = given->FindChild(wxT("arguments"));

			if (given != nullptr)
				arguments = *given;

			// CARRIED OUT to the announcement and the journal — the real tool and its real
			// arguments, not the envelope they arrived in. See the note where these are declared.
			toolName = name;
			toolArguments = arguments;

			// 🛑 A CLIENT NEED NOT INTRODUCE ITSELF TO WORK. The token is what admits it;
			// `initialize` is a courtesy the protocol expects and nothing here enforces — so a
			// caller can read and change a configuration having said nothing, and the person
			// watching sees things happen in their tree with no idea who is doing them. The
			// arrival announcement was tied to the handshake, which is exactly the event that can
			// be skipped.
			//
			// So the FIRST ACT announces itself when the introduction did not. Once, because this
			// is about there being somebody there — after that the activity lines say what they
			// are doing. Sets the same flag the handshake sets: "somebody did connect" is the
			// fact, and how politely they arrived does not change it.
			if (!m_greeted) {
				m_greeted = true;
				Publish(wxT("did"),
					_("Something began using the tools without introducing itself - no handshake, "
					  "only the access key. What it does appears below."),
					wxEmptyString);
			}

			// ⭐ AN ARGUMENT NOBODY DECLARED IS A REFUSAL, not something to ignore.
			//
			// A caller who invents an argument — `remove: true` on a verb that only ever adds, a
			// misspelled name, an option borrowed from a neighbouring tool — is told nothing today:
			// the call succeeds having done something ELSE than what was asked, which reads exactly
			// like success. That is the worst failure a machine caller can be handed, because there
			// is nothing in the answer to notice.
			//
			// The check belongs HERE and not in any tool: the schema every tool already publishes
			// through DescribeInput is the declaration, so one gate covers all of them and no tool
			// can forget to have it. Nothing is validated beyond the NAME — types stay each tool's
			// own business, since only it knows what its values mean.
			const wxString undeclared = ibMcpUndeclaredArgument(tool, arguments);

			ibDataNode payload;
			wxString   refusal;
			bool       ok = false;

			// ⚠ THE ENGINE THROWS, AND THAT IS AN ANSWER TOO. A tool calls the
			// platform's own doors, and those report a refusal by raising — with
			// the diagnosis inside, which is exactly what a caller needs. Left
			// uncaught it would leave the tool's frame, cross the listener thread
			// and take the exchange (or the thread) with it, and the caller would
			// see a dropped connection instead of a sentence.
			//
			// So it becomes an ordinary refusal, carrying the words it came with.
			// ⭐ AND CLASSIFIED BY TYPE, not by reading the sentence. Each
			// subsystem here raises its OWN exception — that is the tree's own
			// rule, and it means "you may not" and "the database said no" are
			// told apart by WHICH catch runs, never by matching words in a
			// message that a translation would change.
			//
			// The kind travels with the refusal so a caller can act on it: a
			// rights refusal means stop and say so, a lock means try later, a
			// query error means the text was wrong and can be rewritten.
			wxString kind;

			try {
				// The undeclared name is answered as a REFUSAL rather than a protocol error, so it
				// arrives the way every other "I did not do that, and here is why" arrives — in
				// the same field the caller already reads.
				// 🛑⭐⭐ THE HOST IS WAITING ON A DIALOG, so it is not in a state to be driven. Refused
				// before the argument checks because it is not about this call being wrong — the
				// call may be perfect and still land in the gap between a question and its answer
				// (Max, 2026-09-02: the designer died when a tool opened a document while a modal
				// stood; one pair of hands could never have produced that).
				//
				// The verbs that get OUT of the situation are exempt (RunsWhileBusy), so a caller
				// can always look, say something and dismiss.
				if (const wxString busy = ibMcpBusyWith();
					!busy.IsEmpty() && !tool->RunsWhileBusy()) {

					refusal = wxString::Format(
						_("The designer is waiting on a dialog ('%s'), so it cannot take this now. "
						  "Nothing was done. Read it with window_dismiss, ask the person to answer "
						  "it, or dismiss it with window_dismiss {close: true} - it is answered as "
						  "Cancel."), busy);
				}
				// ⭐⭐ NOT A REMINDER — A DOOR. There is a person sitting in front of this designer, and
				// from their side a connection is INVISIBLE until something says so: an assistant that
				// reads their whole configuration in silence looks exactly like one that never came.
				//
				// 🛑 ASKING NICELY DOES NOT WORK, and every gentler shape was tried in one day
				// (2026-09-03): said once at the handshake, it is gone from a long-running client's
				// context; appended to the answer, it broke the JSON; carried as a second content
				// block, it broke it again for anything that concatenates blocks. Meanwhile the work
				// went on and nobody was told who was doing it.
				//
				// So the rule is enforced where rules belong — at the door. What is allowed before
				// the greeting is exactly what the greeting NEEDS: read the room (chat_history), see
				// where you are (platform_state, metadata_tree), find the verb (mcp_search /
				// mcp_call), and speak (chat_say). Everything else waits, and the refusal says
				// precisely what to do rather than that something is wrong.
				else if (!m_introduced && !ibMcpRunsBeforeGreeting(name)) {

					refusal = ibMcpText("Nobody knows you are here yet. Say who you are first: `chat_say` "
						"with WHO YOU ARE (which assistant and which model), WHAT YOU SEE (the "
						"configuration by name and what is in it - `platform_state` and "
						"`metadata_tree` depth 1 answer that), ANYTHING ALREADY WORTH SAYING, and "
						"WHAT YOU MEAN TO DO. Until then only those four verbs and `chat_history` "
						"run - the person at this designer sees a window that has been silent while "
						"their configuration was being read, and that is the one thing this "
						"platform will not allow.");
				}
				else if (!undeclared.IsEmpty()) {

					wxString closest;
					const wxString names = ibMcpArgumentsOnOffer(tool, undeclared, closest);

					refusal = wxString::Format(
						_("'%s' takes no argument called '%s'. Nothing was done."), name, undeclared);

					if (!closest.IsEmpty())
						refusal += wxString::Format(_(" Did you mean '%s'?"), closest);

					if (!names.IsEmpty())
						refusal += wxString::Format(ibMcpText(" It takes: %s."), names);
				}
				// …AND THE OTHER HALF OF THE SAME GATE: a name the tool declared as required and
				// nobody sent. Refused BEFORE the tool runs, because what a tool does with an
				// argument that never came is undefined by construction — see ibMcpMissingArgument.
				else if (const wxString missing = ibMcpMissingArgument(tool, arguments);
					!missing.IsEmpty()) {
					refusal = wxString::Format(
						_("'%s' needs '%s', and it did not come. Nothing was done."), name, missing);
				}
				// …AND THE THIRD HALF OF THE SAME GATE: an argument that came in the wrong SHAPE, or
				// a word outside the closed set the schema publishes. The name gate and the missing
				// gate both stop a call that cannot work; this one stops a call that would work on
				// something the caller did not mean — `id: "abc"` read as id 0, a `comparison` word
				// nobody declared taken as the default. See ibMcpArgumentFault.
				else if (const wxString fault = ibMcpArgumentFault(tool, arguments);
					!fault.IsEmpty()) {
					refusal = fault;
				}
				else {
					ok = RunTool(tool, arguments, payload, refusal);
				}
			}
			// The query family FIRST, and the positioned one before its parent:
			// a text query refuses AT A PLACE, and the line and column ride as
			// numbers exactly so a caller can point at the token instead of
			// parsing them back out of a sentence that a translation would
			// change.
			// ⚠ GetErrorDescription(), NEVER what(). The description is held as a wxString and
			// what() is the UTF-8 BYTES of it (backend_exception.h); assigning those bytes to a
			// wxString converts them through the CURRENT LOCALE, not as UTF-8 — so on a non-UTF-8
			// Windows build every Cyrillic engine refusal reached the caller as mojibake or was
			// cut at the first byte the locale could not read. The std::exception line below has
			// to say FromUTF8 precisely because a plain std::exception has no wxString to offer;
			// these six do, and taking it costs no conversion at all.
			catch (const ibBackendQuerySourceException& e) {
				kind = wxString::Format(wxT("query %u:%u"), e.GetLine(), e.GetColumn());
				refusal = e.GetErrorDescription();
			}
			catch (const ibBackendQueryException& e)     { kind = wxT("query");     refusal = e.GetErrorDescription(); }
			catch (const ibBackendAccessException& e)    { kind = wxT("access");    refusal = e.GetErrorDescription(); }
			catch (const ibBackendLockException& e)      { kind = wxT("lock");      refusal = e.GetErrorDescription(); }
			catch (const ibBackendInterruptException& e) { kind = wxT("interrupt"); refusal = e.GetErrorDescription(); }
			catch (const ibBackendException& e)          { kind = wxT("platform");  refusal = e.GetErrorDescription(); }
			catch (const std::exception& e)              { kind = wxT("internal");  refusal = wxString::FromUTF8(e.what()); }
			catch (...) {
				kind = wxT("unknown");
				refusal = wxString::Format(
					_("'%s' failed in a way it does not describe."), name);
			}

			// Said in the answer itself, because a caller reads the text and not
			// our source: "[access] ..." is actionable where a bare sentence is a
			// guess.
			if (!kind.IsEmpty())
				refusal = wxT("[") + kind + wxT("] ") + refusal;

			// A REFUSAL IS AN ANSWER, not a transport failure: the caller is a
			// machine that will try something else, and it needs the reason in
			// the same place it reads a result from.
			std::shared_ptr<ibDataNode> content = std::make_shared<ibDataNode>();
			content->SetValue(wxT("type"), wxString(wxT("text")));
			// Rendered against the configuration this server serves, so a type
			// reads as "CatalogRef.Goods" rather than as a number that means
			// nothing on the other side of the socket.
			wxString text = ok
				? ibMcpRenderNode(payload, ibMetaTypeResolver(activeMetaData))
				: refusal;

			// ⭐⭐ SAID ONCE IS NOT SAID. The orientation asks a client to introduce itself in the
			// assistant window before any work, in as many words — and it is handed over exactly
			// once, at the handshake. A client that runs for hours, or one whose own context was
			// compacted, no longer has that text in front of it and cannot be expected to obey a
			// rule it can no longer read. It then works in silence, and the person watching their
			// own configuration change has no idea who is doing it.
			//
			// So the reminder rides the ANSWER, which is the one thing a working client always
			// reads, and stops the moment it is honoured. Not a nag: it is the platform stating an
			// expectation at a moment when the reader can still act on it.
			//
			// ⚠ THE FACT IS "HAS SPOKEN TO THE PERSON", which is not m_greeted — that one means
			// "somebody connected" and is set by the handshake or by the first tool call, neither
			// of which the person sees as an introduction.
			if (ok && name == wxT("chat_say"))
				m_introduced = true;

			content->SetValue(wxT("text"), text);

			std::vector<ibDataValue> blocks = { ibDataValue::Child(content) };

			// ⭐⭐ AN IMAGE TRAVELS AS AN IMAGE. A tool that answers with a picture (screen_capture)
			// puts the bytes in `image` with `imageType` beside them, and they leave here as a
			// content block of their own — the protocol has a kind for exactly this. Left in the
			// text they would arrive as a wall of base64: technically the same bytes, and
			// unreadable, which for a picture means useless.
			if (ok) {
				const ibDataValue* image = payload.FindField(wxT("image"));
				const ibDataValue* type  = payload.FindField(wxT("imageType"));

				if (image != nullptr && image->Kind() == ibDataKind::String && !image->AsString().IsEmpty()) {

					std::shared_ptr<ibDataNode> picture = std::make_shared<ibDataNode>();
					picture->SetValue(wxT("type"), wxString(wxT("image")));
					picture->SetValue(wxT("data"), image->AsString());
					picture->SetValue(wxT("mimeType"),
						type != nullptr && type->Kind() == ibDataKind::String && !type->AsString().IsEmpty()
							? type->AsString() : wxString(wxT("image/png")));

					blocks.push_back(ibDataValue::Child(picture));

					// 🛑 AND THE TEXT BLOCK STOPS CARRYING THE SAME BYTES. Rendered whole, the answer
					// would repeat the picture as a wall of base64 beside it — the identical data in
					// the form that costs the most and reads the least. What stays is what the image
					// cannot say for itself: what was in focus, and how big it is.
					wxString said = ibMcpText("A picture is attached.");

					if (const ibDataValue* focus = payload.FindField(wxT("focus")))
						if (focus->Kind() == ibDataKind::String && !focus->AsString().IsEmpty())
							said << wxT("\n") << ibMcpText("In focus: ") << focus->AsString();

					if (const ibDataValue* note = payload.FindField(wxT("note")))
						if (note->Kind() == ibDataKind::String && !note->AsString().IsEmpty())
							said << wxT("\n") << note->AsString();

					content->SetValue(wxT("text"), said);
				}
			}

			ibDataNode result;
			result.AddField(wxT("content"), ibDataValue::Array(blocks));
			if (!ok)
				result.SetValue(wxT("isError"), true);

			toolRan = true;
			toolOk = ok;
			toolRefusal = refusal;
			toolKind = kind;

			// ⚠ ASKED OF THE ANSWER, NOT OF ITS TEXT. This used to search the rendered JSON for a
			// key — and searched for the wrong one (`said`, where chat_take answers `messages`),
			// so the test was true every time and NOTHING was journalled, including the polls that
			// carried a person's message. Sniffing a string for a field is guesswork about a shape
			// that is right there to be asked.
			// ⚠ AND FRUITLESS MEANS BOTH LISTS EMPTY. chat_take now answers two — what was said and
			// what happened — and a poll that came back carrying a metadata stage is not a poll
			// that found nothing.
			if (ok && name == wxT("chat_take")) {

				auto empty = [&payload](const wxChar* field) {
					const ibDataValue* list = payload.FindField(field);
					return list == nullptr
						|| list->Kind() != ibDataKind::Array
						|| list->AsArray().empty();
				};

				toolFruitless = empty(wxT("messages")) && empty(wxT("observed"));
			}

			answer = ibMcpWriteResult(parsed.m_id, result);
		}
	}
	else if (parsed.m_method == wxT("ping")) {

		// ⭐ THE PROTOCOL'S OWN LIVENESS CHECK, and it used to fall through to "no method named
		// ping". A client that pings to see whether anybody is home would read that error as an
		// answer of the wrong kind and could reasonably drop the connection — the one failure
		// where refusing correctly is worse than saying nothing.
		//
		// An empty result IS the answer the spec asks for; there is nothing to report.
		answer = ibMcpWriteResult(parsed.m_id, ibDataNode());
	}
	else if (parsed.m_method == wxT("logging/setLevel")) {

		// ⚠ WE SEND LOG NOTIFICATIONS, so we owe the client the switch that turns them down.
		// Say() writes `notifications/message` on the stream when a person types; a server that
		// emits those and then refuses to be told how much of it is wanted is advertising a
		// capability it does not honour.
		const wxString level = parsed.m_params.GetValue<wxString>(wxT("level"));

		// Kept as the NUMBER of a level, so comparing is comparing — the words are the wire's
		// business, not the gate's.
		m_logLevel.store(ibMcpLevelRank(level));

		answer = ibMcpWriteResult(parsed.m_id, ibDataNode());
	}
	else if (!parsed.WantsAnswer()) {
		// A notification we do not act on — `notifications/initialized`, `notifications/cancelled`
		// and their kind. The protocol says answer nothing at all.
		answer = wxEmptyString;
	}
	else {
		answer = refuse(parsed.m_id, ibMcpError::MethodNotFound,
			wxString::Format(wxT("No method named '%s'"), parsed.m_method));
	}

	// WHAT THE WINDOW IS TOLD IS NOT THE WIRE.
	//
	// It used to be handed the whole request and the whole reply, and a person
	// reading it saw JSON-RPC frames — including one per poll from a client that
	// was only asking whether anything had been said. Unreadable, and it buried
	// the conversation it existed to show.
	//
	// So: the CONVERSATION is published by Say/Reply, in the person's own words.
	// Everything else is ACTIVITY, and activity is one short line naming what was
	// done. The bodies stay out of the window; a wire log is a different tool for
	// a different reader.
	if (parsed.m_method == wxT("tools/call")) {

		// THE ONE THE EXECUTOR RESOLVED, envelope already off. Falling back to the wire name only
		// for a call that never got that far — an unknown tool, a bad envelope — because those
		// still belong in the record.
		const wxString tool = toolName.IsEmpty()
			? parsed.m_params.GetValue<wxString>(wxT("name")) : toolName;

		// IN THE READER'S WORDS, NOT THE PROTOCOL'S. The person reading either the window or the
		// journal is a developer looking at their own configuration; "creating catalog Goods" is
		// what happened, `metadata_create` is only how it was spelled on the wire. The tool is
		// asked, because it is the only thing that knows what its own arguments mean.
		//
		// Asked ONCE, for both readers — a second copy of this would be a second chance to drift.
		wxString activity = tool;
		wxString detail;

		if (const ibMcpTool* called = ibFindMcpTool(tool)) {

			// 🛑 DESCRIBING WHAT HAPPENED MUST NOT UNDO IT. These two build a LINE FOR A WINDOW —
			// decoration over work that has already run — and they were called outside the try
			// above, so an exception in either escaped the handler entirely: httplib answered
			// HTTP 500 with an empty body, and the caller had no way to learn that its change had
			// LANDED. Found live on 2026-08-31: `metadata_set` with a boolean value set the
			// property, then GetActivity read that same argument as a string
			// (`params.GetValue<wxString>`), ibDataValue::Expect raised on the kind mismatch, and
			// the answer died on the way out. The configuration was modified; the caller was told
			// nothing at all, which is worse than being told no.
			//
			// ⚠ NOT FIXED BY MAKING GetValue FORGIVING. Reading a value as the wrong kind SHOULD
			// raise — that rule caught real defects elsewhere. What is wrong is that a failure to
			// DESCRIBE was allowed to become a failure to ANSWER.
			//
			// So the description is attempted, and a description that cannot be built falls back to
			// the tool's own name. A line that reads `metadata_set` is a poor line; a request that
			// vanishes is a lost change.
			try {
				activity = called->GetActivity(toolArguments);

				// ⭐ THE HEADLINE AND WHAT IS UNDER IT — asked separately because they are read
				// separately. The window gets both; the JOURNAL below gets only the headline, since
				// a record is scanned for what happened and the content of every module ever
				// written would bury exactly that.
				detail = called->GetDetail(toolArguments);
			}
			catch (const ibBackendException&) { activity = tool; detail.clear(); }
			catch (const std::exception&)     { activity = tool; detail.clear(); }
			catch (...)                       { activity = tool; detail.clear(); }
		}

		// The chat verbs ARE the conversation and are shown by Say/Reply.
		// Announcing them again would double every line — and announcing a
		// fruitless `chat_take` is the noise that made this window useless.
		// 🛑 A REFUSED CALL WAS ANNOUNCED AS A DONE ONE. "reading the template ''" appeared for a
		// call that read nothing — it had been refused for want of an id — and the window said it
		// happened, with an empty name where the object should be. Both halves are the same
		// mistake: the line was built from the REQUEST and published without ever asking how the
		// request ended.
		//
		// ⭐⭐ THE WINDOW SHOWS WHAT WAS DONE. A refusal is the ASSISTANT'S problem: it was told no,
		// it knows why, and it can say so in words if it matters — putting it here fills the pane
		// with an assistant's failed attempts, which is not what the person is watching for (Max,
		// 2026-09-01: *"I do not particularly need to know what did not work out for you — you will
		// just tell me. What I am interested in is what DID"*).
		//
		// 🛑 IT IS STILL RECORDED. The registration journal below takes every call, refusals with
		// their reason and their kind — the difference is between a LIVE view somebody is watching
		// and a RECORD somebody reads afterwards, and only the first has to be worth watching.
		//
		// The chat verbs ARE the conversation and are shown by Say/Reply; announcing them here
		// would double every line.
		if (toolOk && tool != wxT("chat_take") && tool != wxT("chat_say"))
			Publish(wxT("did"), activity, detail);

		// ⭐ AND INTO THE REGISTRATION JOURNAL — every tool call, kept.
		//
		// The window above is a LIVE view: it shows what is happening to whoever is watching, and
		// it is gone when the session ends. The journal is the RECORD — read later, by an
		// administrator who was not there, with the same reader (`journal_read`, `ibLoggerReader`)
		// that shows every other thing the platform did. An assistant that can change a
		// configuration has to leave the same trail a person does; anything less makes "who did
		// this" unanswerable for exactly the actor most likely to be asked about.
		//
		// ⭐ ONE PLACE, ALL TOOLS. Written here rather than in each tool for the reason the
		// undeclared-argument gate is here: a per-tool line is a line the sixty-first tool forgets,
		// and the trail would be quietly incomplete rather than visibly absent.
		//
		// The line is the tool's OWN activity sentence — the same one the window gets — so the
		// journal reads in the developer's words ("creating Catalog 'Goods'") and not in the
		// protocol's. A refusal is recorded too, and as a refusal: what was attempted and did not
		// happen is the half an audit is usually opened for. `kind` rides along when the engine
		// classified it (rights, lock, query), because a refusal that means "may not" and one that
		// means "the text was wrong" are read differently.
		//
		// ⚠ THE ONE THING NOT LOGGED is a `chat_take` that took nothing. It is a POLL — the
		// assistant asking whether anything was said — and it arrives on a timer whether or not
		// anything happened. Logging it would bury the record it exists to keep, which is the
		// same reason the window does not show it. Everything else goes in, chat included.
		if (toolRan && ibLog != nullptr) {

			if (!toolFruitless) {

				wxString line = activity;

				if (!toolOk) {
					line << wxT(" - ") << _("refused");
					if (!toolKind.IsEmpty())
						line << wxT(" (") << toolKind << wxT(")");
					if (!toolRefusal.IsEmpty())
						line << wxT(": ") << toolRefusal;
				}

				ibLog->Audit(wxT("assistant"), toolOk ? wxT("mcp.did") : wxT("mcp.refused"), line);
			}
		}
	}

	return answer;
}
