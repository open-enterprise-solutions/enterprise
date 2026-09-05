#ifndef _IB_MCP_SERVER_H_
#define _IB_MCP_SERVER_H_

////////////////////////////////////////////////////////////////////////////
//	Description : ibMcpServer — the platform answering a machine caller
////////////////////////////////////////////////////////////////////////////
//
// WHAT IT IS. An appData subsystem, the way ibJobManager / ibLockManager /
// ibSettingsStorage are: constructed by ibApplicationData behind the
// ib::AppDataCtorToken gate, reached through a static accessor, torn down with
// everything else. It is IN THE CORE deliberately — not a plugin, not a mode of
// one binary — so every host that has an appData can offer it, and a plugin
// stays what the boundary was built to be: a showcase, not the mechanism.
//
// WHAT IT SERVES. Whatever registered itself (mcpTool.h). The server holds no
// list of verbs: the compiler's check, the metadata doors, and later the syntax
// helper, the debugger, the query constructor, the composer and the FORM (that
// one registered by the front, which is where controls live) each arrive by
// registering. Adding one edits nothing here.
//
// IN WHOSE NAME. A session's. The server is started from an already
// authenticated session — a person opened the designer, the platform let them
// in — and its worker BINDS ITSELF TO THAT SESSION (ibSession::BindSessionToThread,
// the same primitive the debug server's threads use). So every door it calls
// answers exactly as it would for that developer: same rights, same roles, same
// active configuration. There is no second permission model to keep in step,
// which is the whole reason the session is the unit rather than a token of our
// own invention.
//
// THE TRANSPORT IS HTTP ON LOOPBACK. A client speaks MCP over stdio (it spawns
// the server) or over HTTP (it connects to one). We are not spawned — we live
// inside a designer that is already running, with its stdout taken — so HTTP it
// is. Loopback because the answer includes everything a configuration is.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/backend.h"
#include "backend/appDataCtorToken.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <wx/string.h>

class ibSession;

// WHAT A PERSON SET. Kept apart from the server so the designer's settings page
// edits a value and hands it over, instead of poking at a running object.
struct BACKEND_API ibMcpSettings {

	// Off unless somebody turned it on. An open port that describes and edits a
	// configuration is not something a platform should acquire by upgrading.
	bool           m_enabled = false;

	// Loopback by default, and a person has to mean it to widen that.
	wxString       m_address = wxT("127.0.0.1");
	unsigned short m_port    = 3737;

	// ⭐ WHO IS ALLOWED TO SPEAK. Every request carries it as `Authorization: Bearer <token>`,
	// and one without is refused before any tool is looked up.
	//
	// It exists because the address is not an authorisation. A loopback port is open to every
	// process on the machine — every other application, every script, anything a browser was
	// talked into fetching — and this one edits a configuration and runs code AS THE DEVELOPER
	// WHOSE SESSION IT LIVES IN. "Only local" answers where a caller is, never who.
	//
	// ⚠ AND IT IS NOT OPTIONAL. An empty token refuses everything rather than allowing it: a
	// secret that may be blank is a secret nobody sets, and the failure of that choice is silent
	// where the failure of this one is a message saying where to copy it from. One is generated
	// the first time the settings are written, so it is never blank by accident.
	wxString       m_token;

	bool IsOk() const { return !m_address.IsEmpty() && m_port != 0; }
};

// WATCHING THE CONVERSATION. The designer shows a window with the exchange in
// it — what was asked, what the platform answered — so a person can see what is
// being done in their name rather than infer it from the tree changing under
// them. Publishing is best-effort and cheap when nobody listens, like the
// diagnostics bus it is modelled on.
//
// ⭐ IT PUSHES DATA, NOT A PICTURE. The three strings are what happened; how it
// LOOKS is the front's business — a list, a document, a rendered page — which is
// the same division ibPropertyObjectNotifier keeps ("the object says what
// changed; the front owns the widget"). A server that answered in HTML would put
// presentation in the core and make the window unreplaceable. The same push also
// feeds a log and a test, which a rendered page could not.
class ibMcpNotifier {
public:
	virtual ~ibMcpNotifier() = default;

	// Called on the SERVER'S thread, once per exchange. Keep it short: the
	// answer is not sent until this returns. A front-end notifier should post to
	// its own queue rather than touch a widget here.
	virtual void OnMcpExchange(const wxString& method,
		const wxString& request, const wxString& answer) = 0;
};

class BACKEND_API ibMcpServer {
public:

	ibMcpServer(const ibMcpServer&) = delete;
	ibMcpServer& operator=(const ibMcpServer&) = delete;

	// Construction restricted to ibApplicationData, the same gate every
	// appData-owned subsystem is built behind.
	//
	// ⚠ BOTH OUT OF LINE, and the constructor is not an oversight: the listener
	// below is an incomplete type here (it is declared in the .cpp), and an
	// INLINE constructor needs the member's destructor too — for unwinding if it
	// throws. Defined inline, this is `can't delete an incomplete type` at every
	// call site that builds one.
	explicit ibMcpServer(ib::AppDataCtorToken);
	~ibMcpServer();

	// --- the central entry point -----------------------------------------
	// Settings first, then start. Configuring a running server does not move
	// it: stop it and start it again, so what a person sees on the page and
	// what is listening cannot drift apart.
	void                 Configure(const ibMcpSettings& settings);
	const ibMcpSettings& GetSettings() const { return m_settings; }

	// WHOSE SETTINGS. Every developer who opens the designer configures their
	// own server — two people on one machine are two servers on two ports — so
	// these live in sys_settings under the person's own key, not in a file
	// beside the executable.
	//
	// Read at LOGIN: the moment a session authenticates, that person's settings
	// are in force. Nobody has saved any yet is not a failure — it is a cold
	// start, and the answer is false with the defaults left standing.
	bool LoadSettings(ibSession* session);
	bool SaveSettings(ibSession* session) const;

	// Start listening IN THE NAME OF `session`. False when the settings are
	// off or unusable, when a server is already up, or when the port cannot be
	// taken — `refusal` says which, in words a person can act on.
	bool Start(ibSession* session, wxString& refusal);
	void Stop();

	bool IsRunning() const { return m_running.load(); }

	// What to hand the client, once running — the address a person copies into
	// their tool's configuration. Empty when stopped.
	wxString GetEndpoint() const;

	// --- asking the client's model ----------------------------------------
	//
	// THE PLATFORM HAS NO MODEL, AND SHOULD NOT. Model access belongs to
	// whoever connected: MCP's `sampling/createMessage` is the server asking the
	// CLIENT to run a completion and hand back the text. So no key is stored
	// here, no outbound TLS is needed, and the answer comes from the same
	// assistant that is already working on this configuration rather than from a
	// second one configured elsewhere.
	//
	// True only when a client is connected AND declared the sampling capability
	// in its handshake — which is what makes the Send button mean something
	// instead of being enabled and hopeful.
	bool CanAskModel() const;

	// Queues one question. Answers false with a reason when there is nobody to
	// ask. The REPLY DOES NOT COME BACK HERE: it arrives from the client later
	// and is published to the notifiers like any other exchange, so a window
	// that already shows the conversation shows this too, and the caller does
	// not sit waiting on a socket.
	bool AskModel(const wxString& question, wxString& refusal);

	// --- the conversation itself ------------------------------------------
	//
	// A PERSON'S MESSAGE HAS TO WAIT SOMEWHERE. The platform cannot call a model
	// (no outbound TLS, no key, and neither belongs here); the assistant that
	// CAN is the one already connected. So the window drops what was typed into
	// this queue and whoever is connected picks it up with a tool and answers
	// with another — the conversation happens inside the configuration, and
	// model access stays where it already lives.
	//
	// ⚠ This is the SHORT road, and it is honest about being one: the client has
	// to come and look. The long road is MCP sampling, where the server asks the
	// client without being asked — same window, same notifiers, different
	// plumbing underneath.
	void Say(const wxString& text);      // from the window, to whoever is listening
	std::vector<wxString> TakeSaid();    // from the client: everything not taken yet

	// ⭐⭐ WHAT HAPPENED, AS AGAINST WHAT WAS ASKED — and they are kept apart because they oblige
	// differently (Max, 2026-09-01: *"it is information you can read, but it does not order you to
	// do it"*).
	//
	// A person's message is an ASK: taken once, answered once. A note is CONTEXT: the person
	// created a catalog, the configuration was saved, the database was applied. Both are useful to
	// an assistant and only the first is a request — put them in one list and every observation
	// starts reading like an instruction, which is how an assistant ends up answering a person who
	// said nothing.
	//
	// ⚠ AND IT NEVER REACHES THE WINDOW. The person DID the thing; telling them they did it fills
	// their own pane with their own actions.
	void Note(const wxString& text);
	std::vector<wxString> TakeNoted();

	// ⭐ HAS ANYBODY PICKED IT UP — asked WITHOUT taking, which TakeSaid cannot do. The window
	// needs it to tell two silences apart: a message nobody has collected means nothing is
	// listening, and one that was collected means an answer is being worked on. Both look like
	// waiting; only the first is a problem, and calling it "processing" would be a lie.
	size_t PendingCount() const;
	void Reply(const wxString& text);    // from the client, into the window

	// ⭐ WHO SAID IT, KEPT WITH WHAT WAS SAID. The queue above answers "what is new"; this answers
	// "what was the conversation", and they are different questions that were being served by one
	// destructive vector — so closing the window lost the exchange, and a client reconnecting had
	// no idea what had been discussed.
	struct Turn {
		bool     fromPerson;   // the window, as opposed to the assistant
		wxString text;
	};

	// ⭐ WHAT THE WINDOW SAYS BEFORE ANYONE HAS SAID ANYTHING. An empty pane tells a person
	// nothing — not whether this is working, not what it is, not what to do next — and "nothing
	// has happened yet" and "this is broken" look identical when both are blank.
	//
	// One text, two moments: published by Start() (so a window already open sees it appear) and
	// shown when a WINDOW opens onto a server already running (so somebody who opens the tab an
	// hour later is not the only one who missed it). Written once here because two greetings would
	// drift into disagreeing about the same server.
	//
	// ⚠ NOT by Stop(). At the point Stop announces itself the server is still running, so this
	// would say so — the one moment the text would be exactly wrong. Stopping has words of its
	// own, and that is the reason, not an oversight.
	wxString Greeting() const;

	// Everything said this session, oldest first. The window fills itself from it when it opens;
	// an assistant reads it to learn the context it is joining.
	//
	// ⚠ THIS SESSION ONLY. It lives with the server and goes when the process does — persisting a
	// conversation is a separate decision with a place to store it, and pretending otherwise here
	// would be the worse kind of half-answer.
	std::vector<Turn> GetConversation() const;

	// --- watching ---------------------------------------------------------
	// The pair every notifier in this tree is added and removed by. Removal is
	// by pointer identity and must happen before the notifier is destroyed.
	void AddNotifier(ibMcpNotifier* notifier);
	void RemoveNotifier(ibMcpNotifier* notifier);

private:

	class ibMcpListener;   // the worker thread + its socket, defined in the .cpp

	// WHAT THE HTTP HEADERS SAID, for the parts of the message the transport mirrors
	// into them. From revision 2025-06-18 a client states the protocol version in a
	// header; from 2026-07-28 it also mirrors the method and the tool name, and the
	// server MUST refuse a request whose header and body disagree — because a
	// load balancer routes on the one and the server executes on the other.
	//
	// Carried in a struct of its own rather than by handing Answer an
	// httplib::Request: the JSON-RPC layer has never known what HTTP is, and the
	// three strings it now needs are not a reason to teach it.
	struct ibMcpWireHeaders {
		wxString m_protocolVersion;   // MCP-Protocol-Version, empty when absent
		wxString m_method;            // Mcp-Method
		wxString m_name;              // Mcp-Name (tools/call, resources/read, prompts/get)
	};

	// ANSWER ONE MESSAGE. Parses the envelope, finds the tool, calls it, writes
	// the answer. Public behaviour of the protocol lives here rather than in the
	// listener so a test can ask a question without opening a port.
	//
	// `headers` is what the transport saw; a caller with no transport (a test) passes
	// none and the header checks simply have nothing to compare against.
	//
	// `outErrorCode` receives the JSON-RPC error code when the answer is a refusal, 0
	// otherwise. The transport needs it because the modern revision expects certain
	// refusals to carry an HTTP status of their own — but WHICH status is the
	// transport's business, so this hands over the protocol's own verdict rather than
	// an HTTP number. It also spares the caller from reading its own output back:
	// searching the emitted JSON for `"code":-32020` is a guess about the writer's
	// spacing, and it was wrong the first time it was tried.
	wxString Answer(const wxString& request) { return Answer(request, ibMcpWireHeaders(), nullptr); }
	wxString Answer(const wxString& request, const ibMcpWireHeaders& headers, int* outErrorCode);
	friend class ibMcpListener;

	void Publish(const wxString& method, const wxString& request, const wxString& answer);

	// ⭐⭐ RUN A TOOL WHERE A MOUSE CLICK RUNS — through the session's worker pool, which on a
	// desktop host is the wx main thread. The socket thread may not touch a widget or the tree, and
	// what that cost was not a refusal but the process: see the note on the definition.
	bool RunTool(const class ibMcpTool* tool, const class ibDataNode& arguments,
		class ibDataNode& payload, wxString& refusal);

	ibMcpSettings                m_settings;
	std::unique_ptr<ibMcpListener> m_listener;

	// ⭐⭐ THE ASSISTANT'S OWN SUBSCRIPTION TO THE METADATA — the twin of ibMcpDebugBridge, and the
	// same idea: it watches ALONGSIDE the designer's navigator instead of asking the navigator
	// anything, so a person building a configuration by hand and an assistant watching it see the
	// same events at the same moment.
	//
	// What it does with them is forward them, one line each, down the road a chat message already
	// travels (Max, 2026-09-01: *"you can just forward them to yourself — for you it is a log"*).
	//
	// ⚠ THE CLASS ITSELF LIVES IN THE .cpp and is unnameable from outside, exactly as the tree's
	// notifier does. Nothing hands one in, nothing holds one but this.
	std::unique_ptr<class ibMcpMetaBridge> m_metaBridge;

	// Puts the bridge on the open configuration's list, moving it when that configuration is not
	// the one it is already watching. Called from the one place every tool call passes through —
	// a configuration can be opened, closed and reopened while the server runs.
	void WatchMetadata();
	std::atomic<bool>            m_running{ false };

	// Not owned — the debug client's bridge list owns what it holds. Null when
	// the process has no debugger, or while the server is stopped.
	class ibMcpDebugBridge*      m_debugBridge = nullptr;

	// WHAT THE CLIENT SAID IT CAN DO, taken from its handshake. Asked before
	// every outgoing request, because a client that cannot sample must be told
	// so by a disabled button rather than by silence after a press.
	std::atomic<bool>            m_clientCanSample{ false };

	// The id of the next request WE send. Server-initiated ids live in their own
	// numbering — a client's id 1 and ours are different conversations, and
	// mixing them is how an answer gets matched to the wrong question.
	std::atomic<long>            m_outgoingId{ 1 };

	// The session everything is done in the name of. Not owned: it outlives the
	// server by construction (the server is stopped when the session closes).
	ibSession*                   m_session = nullptr;

	std::vector<ibMcpNotifier*>  m_notifiers;

	// WHAT WAS TYPED AND NOT YET COLLECTED. Written on the UI thread (the Send
	// button), drained on the server's (a tool call), so it is guarded — the one
	// place in this class where two threads meet over a container.
	// HOW MUCH THE CLIENT ASKED FOR, as a rank rather than a word — set by `logging/setLevel`,
	// read on the thread that writes the stream. Starts at 0 (debug), which is everything: a
	// client that never sets it gets the notifications, which is what the whole stream is for.
	std::atomic<int>             m_logLevel{ 0 };

	// HAS ANYBODY EVER FINISHED A HANDSHAKE. Not the same as "running": a server can listen all
	// day with nobody there, and a person waiting for an answer cannot tell those apart from the
	// window. Set once at `initialize` and never cleared — HTTP has no hang-up to observe, so
	// claiming somebody left would be a guess, while "somebody did connect" stays true.
	bool                         m_greeted = false;

	// HAS THE ASSISTANT SPOKEN TO THE PERSON YET — a different fact from m_greeted, which only
	// records that somebody connected. Until this is true every answer carries the reminder to
	// introduce itself, because the orientation that asks for it is handed over once and a
	// long-running client no longer has it in view.
	bool                         m_introduced = false;

	mutable std::mutex           m_saidMutex;

	// NOT YET COLLECTED — drained by TakeSaid, so each message is answered once.
	std::vector<wxString>        m_said;

	// …AND WHAT WAS MERELY OBSERVED, drained by TakeNoted. Same mutex, separate list: see Note().
	std::vector<wxString>        m_noted;

	// WHAT WAS SAID, kept. Guarded by the same mutex: the two are written in one breath and a
	// second lock would only be a second thing to forget.
	std::vector<Turn>            m_conversation;
};

#endif // _IB_MCP_SERVER_H_
