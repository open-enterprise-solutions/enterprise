////////////////////////////////////////////////////////////////////////////
//	Description : the MCP server — one person's, answering what their transport carries
////////////////////////////////////////////////////////////////////////////
//
// ⭐ THE TRANSPORT IS THEIR CLIENT'S (frmclient/backend/mcp/mcpServer.h): the port is opened where
// the person is, and what it hears comes here as it is, over the client's own connection
// (ibClientHost::Mcp). Nobody listening — a line said then is dropped.
//
////////////////////////////////////////////////////////////////////////////

#include "frmserver/mcp/mcpServer.h"

#include "backend/appData.h"                        // GetActiveMetaData, GetLogger
#include "backend/logger/logger.h"                  // ibLog — the registration journal
#include "backend/backend_exception.h"              // an engine refusal arrives as an exception
#include "backend/query/queryException.h"           // …and the query family says WHERE
#include "backend/debugger/debugClient.h"           // the bridge list — we ride along on a session
#include "frmserver/mcp/mcpDebugBridge.h"
#include "backend/rpc/rpcMessage.h"
#include "frmserver/mcp/mcpTool.h"
#include "frmserver/mcp/mcpEra.h"                   // the two eras of the protocol: a request's door, its answer's shape
#include "backend/metaCollection/metaIntrospect.h"  // ibMetaTypeResolver
#include "backend/metadataApplication.h"
#include "backend/session/session.h"
#include "backend/session/workerPool.h"             // the queue of the session's tasks

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>

//---------------------------------------------------------------------------
// the subsystem
//---------------------------------------------------------------------------
ibMcpServer::ibMcpServer(ib::ClientHostCtorToken owner, ibSession* session) :
	m_session(session),
	m_applicationInstance(owner.GetApplicationInstance())
{
	// Empty, and still out of line — see the note in the header: the bridge is
	// incomplete there, and an inline constructor would need its destructor.
}

// ============================================================================
//  ibMcpMetaBridge — the assistant's subscription to the metadata
// ============================================================================
//
// ⭐⭐ IT WATCHES ALONGSIDE, exactly as ibMcpDebugBridge watches a debugging session somebody else
// is driving. The studio's navigator draws these same stages; this one forwards them, so an
// assistant learns that a catalog was created the moment the tree does, instead of asking again
// and diffing what came back.
//
// ⭐ FIRST TO HEAR (IsAssistant). Whoever FILLS a gap has to be told before whoever would ASK a
// person about it — see ibMetaDataNotifier::IsAssistant. Today it only listens; the ordering is
// what makes it able to answer, later, the questions a tool has the arguments for and a dialog
// would otherwise put to somebody who is not there.
//
// ⚠ ONE LINE PER STAGE AND NOTHING KEPT. A bridge that remembered would be a second copy of the
// application, going stale the moment anything changed; the application is right there to be
// asked. This is a LOG (Max, 2026-09-01), and its whole value is that it arrives unasked.
class ibMcpMetaBridge : public ibMetaDataNotifier {
public:

	explicit ibMcpMetaBridge(ibMcpServer* server) : m_server(server) {}

	~ibMcpMetaBridge() { Watch(nullptr); }   // off the list while this object is still whole

	virtual bool IsAssistant() const override { return true; }

	void Watch(ibMetaData* metaData)
	{
		if (m_watched.get() == metaData)
			return;

		if (m_watched != nullptr)
			m_watched->RemoveNotifier(this);

		// Off its list first, then let go — the last holder closes it (ibMetaData::MakeShared).
		m_watched = metaData != nullptr ? metaData->shared_from_this() : nullptr;

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

		const ibString said = Word(stage);
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
	static ibString Line(const ibString& said, const ibValueMetaObject* object)
	{
		if (object == nullptr)
			return ibString::Format(ibT("metadata: %s"), said);

		const ibValueMetaObject* const owner = object->GetParent();

		return owner != nullptr
			? ibString::Format(ibT("metadata: %s %s '%s' in '%s' (id %i)"),
				said, object->GetClassName(), object->GetName(), owner->GetName(), (int)object->GetMetaID())
			: ibString::Format(ibT("metadata: %s %s '%s' (id %i)"),
				said, object->GetClassName(), object->GetName(), (int)object->GetMetaID());
	}

	// ⭐ EVERY STAGE BUT ONE. A stage is not only news, it is the ANSWER TO A COMMAND: ask for a
	// save and `Saved` is how you learn it happened, without asking again and comparing (Max,
	// 2026-09-01). So the default is to forward.
	//
	// ⚠ `Edited` IS THE EXCEPTION, and only because of its rate: it arrives on every keystroke in a
	// property field. Forwarding it would bury the ones that answer something, which is the one way
	// a log stops being read.
	static ibString Word(ibMetaStage stage)
	{
		switch (stage) {
		case ibMetaStage::Created:  return _("created");
		case ibMetaStage::Renamed:  return _("renamed");
		case ibMetaStage::Removed:  return _("removed");
		case ibMetaStage::Moved:    return _("moved");
		case ibMetaStage::Loaded:   return _("loaded");
		case ibMetaStage::Run:      return _("running");
		case ibMetaStage::Saved:    return _("saved");
		case ibMetaStage::Closed:   return _("closed");
		case ibMetaStage::Applying: return _("applying to the database");
		case ibMetaStage::Applied:  return _("applied to the database");
		case ibMetaStage::Reverted: return _("rolled back to the database");
		case ibMetaStage::Edited:   return ibEmptyString;   // a keystroke, not an event
		}
		return ibEmptyString;
	}

	ibMcpServer* const m_server;
	// The application whose list it is on — HELD, as whoever works in a metadata holds it (metaData.h), so it goes
	// after the bridge lets go of it, never under it: the session that opened it may leave first.
	std::shared_ptr<ibMetaData> m_watched;

	// The stage not yet said, waiting to learn whether it stands alone — see MetaObjectChanged.
	ibMetaID           m_heldId = 0;
	ibString           m_heldWord;
	ibString           m_heldLine;

	ibMcpMetaBridge(const ibMcpMetaBridge&) = delete;
	ibMcpMetaBridge& operator=(const ibMcpMetaBridge&) = delete;
};

ibMetaDataApplicationBase* ibMcpServer::GetMetaData() const
{
	return m_session != nullptr ? m_session->GetMetaData() : nullptr;
}

void ibMcpServer::WatchMetadata()
{
	if (m_metaBridge == nullptr)
		m_metaBridge = std::make_unique<ibMcpMetaBridge>(this);

	// The open application, or nothing — a server outlives any one application and must not
	// keep a subscription on one that has been closed.
	ibMetaDataApplicationBase* const metaData = GetMetaData();
	m_metaBridge->Watch(metaData != nullptr && metaData->IsOpen() ? metaData : nullptr);
}

ibMcpServer::~ibMcpServer()
{
	Stop();
}

ibString ibMcpServer::Call(const ibString& message, const ibString& protocolVersion, const ibString& method,
	const ibString& name, std::int32_t& status)
{
	status = 200;

	// WHICH REVISION THIS CALLER SPEAKS, and whether we speak it too.
	//
	// The header arrived in revision 2025-06-18 and is REQUIRED of clients from then on; a client
	// older than that sends none, and the transport says to read its absence as 2025-03-26 rather
	// than as an error. What is NOT allowed is to answer an unknown revision as if it were ours —
	// the spec asks for 400 there, so the client learns to negotiate instead of guessing why the
	// answers look wrong.
	const ibString asked = ibString(protocolVersion).Trim(true).Trim(false);
	if (!asked.IsEmpty()) {
		if (!ibMcpEra::Speaks(asked)) {
			status = 400;
			const ibDataNode data = ibMcpEra::SupportedVersions(asked);
			// No id: the refusal happens before the body has been read, so there is no call to
			// answer — which is the shape JSON-RPC allows for exactly this.
			return ibRpcWriteError(ibDataValue(), ibRpcError::UnsupportedVersion, ibT("Unsupported protocol version"), &data);
		}
	}

	// Nothing else asked: a transport opening its stream asks only that.
	if (message.IsEmpty())
		return ibEmptyString;

	// WHAT THE TRANSPORT SAW, held against the body below.
	ibMcpWireHeaders wire;
	wire.m_protocolVersion = protocolVersion;
	wire.m_method          = method;
	wire.m_name            = name;

	// 🛑 THE VERDICT IS ASKED FOR, NOT RECOGNISED. The first version read the status out
	// of the emitted JSON by looking for `"code":-32020` — and got 200 on every refusal,
	// because the writer puts a space after the colon. Searching your own output for a
	// fact you already had is a guess about the writer's formatting dressed up as a
	// decision; the layer that decided the refusal is the one that says so.
	int refusalCode = 0;
	const ibString answer = Answer(message, wire, &refusalCode);

	// ⭐ THE HTTP STATUS IS PART OF THE REFUSAL, not decoration on it. The modern
	// transport asks a client to tell a version refusal from a header refusal from an
	// unknown method WITHOUT parsing — and that is how a dual-era client decides whether
	// to retry with another version or fall back to `initialize` entirely. A refusal
	// answered 200 reads as agreement to a client that only looks at the status.
	switch ((ibRpcError)refusalCode) {
		case ibRpcError::UnsupportedVersion:                     // retry with another version
		case ibRpcError::HeaderMismatch: status = 400; break;    // the two sources disagree
		case ibRpcError::MethodNotFound: status = 404; break;    // nothing here answers to that name
		default: break;                                          // 200: the body carries the whole answer
	}

	// A notification is answered with no body, as the protocol says — but
	// the transport still needs a reply, or the client waits for one.
	if (answer.IsEmpty())
		status = 202;
	return answer;
}

void ibMcpServer::Listen(std::function<void(const ibString& message)> told)
{
	if (!told) {
		if (!m_running.exchange(false))
			return;

		// ⭐ SAID BEFORE IT GOES, AND SAID BY THE SERVER. Whoever is watching this - the assistant
		// window above all - keeps standing there afterwards looking exactly as it did while it
		// worked, and anything typed into it would join a queue no client can ever collect. That
		// reads as being ignored, which is the worst thing a window can do.
		//
		// Published HERE rather than by whoever pressed the button, because the server is the only
		// one that knows it stopped: it also stops when the session ends and when the transport
		// drops, and a caller-side announcement would cover exactly one of the three.
		Publish(ibT("did"),
			_("Assistant access has been switched off. Nothing is listening, so nothing written here "
			  "will be collected until it is switched on again."),
			ibEmptyString);

		// Detach DESTROYS the bridge — the debug client's list owns it. ⚠ The debug client is the application's,
		// so the bridge dies with it — and `Watch(nullptr)` can be what lets the application go. A pointer kept
		// past that was read after it (crash dump 2026-10-20 16:45, studio's exit); the bridge's destructor
		// clears this one.
		if (ibMcpDebugBridge* const bridge = m_debugBridge.load())
			bridge->Detach();

		std::lock_guard<std::mutex> lock(m_toldMutex);
		m_told = nullptr;
		return;
	}

	{
		std::lock_guard<std::mutex> lock(m_toldMutex);
		m_told = std::move(told);
	}
	if (m_running.exchange(true))
		return;
	ibJournalInfo(ibT("mcp"), ibT("assistant access: a transport came"));

	// ⭐ WATCHING FROM THE MOMENT IT IS UP, not from the first question somebody asks it. The
	// subscription is also refreshed at every dispatch, because an application can be opened and
	// closed while the server runs — but relying on the dispatch ALONE means everything the person
	// did before the assistant's first call happened unobserved (2026-09-01, watching exactly that:
	// two empty lists after a template had been created).
	WatchMetadata();

	// The other half of the pair above. A window that was told it went quiet has to be told when
	// it stops being quiet, or the last thing it ever said stays wrong on screen.
	//
	// ⭐ AND IT IS THE SAME TEXT THE WINDOW SHOWS WHEN IT OPENS. Greeting() was written for two
	// moments and was being used in one — the header said "published when the server starts" and
	// nothing did it, so a person watching an already-open window got a thinner line than someone
	// who opened it a second later. One text, both moments, and the promise in the header is now
	// kept by the code rather than by the comment.
	Publish(ibT("did"), Greeting(), ibEmptyString);

	// LISTEN TO THE DEBUGGER TOO, from here on — a studio's: the application's person debugs nothing. The
	// bridge is a list, so this takes nothing away from the IDE's own windows — the developer debugs as they
	// always did and the same stop is visible on this side. The debug client owns what it is given, which is
	// why this is a bare `new` and why letting go is a call to it rather than a delete of our own.
	if (m_session->GetKind() == ibSessionKind::Studio && debugClient != nullptr && m_debugBridge.load() == nullptr) {
		ibMcpDebugBridge* bridge = new ibMcpDebugBridge(this);
		if (!bridge->Attach())
			delete bridge;
	}
}

void ibMcpServer::Stop()
{
	// 🛑 OFF THE METADATA FIRST. The bridge HOLDS the application it watches, so the application goes
	// after the server, never before it. While it held a bare pointer it was outlived twice: by the base's
	// own release order (crash dump 2026-09-08 09:43, `_Adopt_unlocked` inside `ibMetaData::RemoveNotifier`),
	// and by the session that opened it — the studio's logout let go of its application before the
	// base closed, and this line called `RemoveNotifier` on a freed list (2026-10-16).
	//
	// Let go HERE because this is what the host calls before the client's session goes (ibClientHost::
	// StopMcpServer), while the pool and the registry a closing application still asks for are alive.
	if (m_metaBridge != nullptr)
		m_metaBridge->Watch(nullptr);

	Listen(nullptr);
}

void ibMcpServer::Tell(const ibString& message)
{
	std::lock_guard<std::mutex> lock(m_toldMutex);
	if (m_told)
		m_told(message);
}

void ibMcpServer::AddNotifier(ibMcpNotifier* notifier)
{
	if (notifier == nullptr)
		return;

	std::lock_guard<std::mutex> lock(m_notifiersMutex);
	for (const ibMcpNotifier* registered : m_notifiers)
		if (registered == notifier)
			return;

	m_notifiers.push_back(notifier);
}

void ibMcpServer::RemoveNotifier(ibMcpNotifier* notifier)
{
	std::lock_guard<std::mutex> lock(m_notifiersMutex);
	std::erase(m_notifiers, notifier);
}

// ⭐ THE LEVELS AS AN ORDER, which is the only thing a gate needs of them. The wire spells them as
// words (the syslog set the protocol adopted); comparing words is comparing spellings, and the
// question being asked is "is this at least as important as what they asked for".
//
// ⚠ An unknown word ranks as the LOWEST, so a client that sends something we do not recognise
// keeps receiving everything rather than silently losing the stream — the failure that would look
// exactly like the server having stopped talking.
static int ibMcpLevelRank(const ibString& level)
{
	if (level.IsSameAs(ibT("emergency"), false)) return 7;
	if (level.IsSameAs(ibT("alert"),     false)) return 6;
	if (level.IsSameAs(ibT("critical"),  false)) return 5;
	if (level.IsSameAs(ibT("error"),     false)) return 4;
	if (level.IsSameAs(ibT("warning"),   false)) return 3;
	if (level.IsSameAs(ibT("notice"),    false)) return 2;
	if (level.IsSameAs(ibT("info"),      false)) return 1;

	return 0;   // debug, and anything we do not know
}

// What Say() sends on the stream. Named so the gate reads as a comparison, not as a number.
static const int kChatNoteLevel = 1;   // info

void ibMcpServer::Say(const ibString& text)
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
	Publish(ibT("you"), text, ibEmptyString);

	// AND THE LISTENER IS TOLD, so nobody has to poll. This is the whole point
	// of holding a stream open: the assistant learns that something was said at
	// the moment it is said, instead of asking every few seconds and filling the
	// window with the asking.
	// ⚠ AND ONLY AS LOUDLY AS THEY ASKED. A client that sent `logging/setLevel` above info is
	// saying it does not want this frame; declaring the capability and then ignoring the switch
	// would be advertising something we do not honour. The WINDOW is unaffected — a person
	// watching their own studio is not the audience a client's log level is about.
	if (IsRunning() && kChatNoteLevel >= m_logLevel.load()) {
		ibDataNode note;
		note.SetValue(ibT("jsonrpc"), ibString(ibT("2.0")));
		note.SetValue(ibT("method"), ibString(ibT("notifications/message")));

		ibDataNode& params = note.Child(ibT("params"));
		params.SetValue(ibT("level"), ibString(ibT("info")));
		params.SetValue(ibT("logger"), ibString(ibT("chat")));
		params.SetValue(ibT("data"), text);

		Tell(ibRpcRenderNode(note));
	}
}

void ibMcpServer::Note(const ibString& text)
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

	// The transport still learns of it at once — see the note in Say() about the log level.
	if (IsRunning() && kChatNoteLevel >= m_logLevel.load()) {
		ibDataNode note;
		note.SetValue(ibT("jsonrpc"), ibString(ibT("2.0")));
		note.SetValue(ibT("method"), ibString(ibT("notifications/message")));

		ibDataNode& params = note.Child(ibT("params"));
		params.SetValue(ibT("level"), ibString(ibT("info")));
		params.SetValue(ibT("logger"), ibString(ibT("metadata")));
		params.SetValue(ibT("data"), text);

		Tell(ibRpcRenderNode(note));
	}
}

std::vector<ibString> ibMcpServer::TakeNoted()
{
	std::lock_guard<std::mutex> lock(m_saidMutex);

	std::vector<ibString> taken;
	taken.swap(m_noted);
	return taken;
}

std::vector<ibString> ibMcpServer::TakeSaid()
{
	std::vector<ibString> taken;
	{
		std::lock_guard<std::mutex> lock(m_saidMutex);

		// TAKEN, not read: two clients asking would otherwise both answer the same
		// question, and the person would get two replies to one message.
		taken.swap(m_said);
	}

	// ⭐ AND THE WINDOWS ARE TOLD IT WAS — "waiting for an assistant to collect it" ends the moment one has. A window
	// that is drawn only when its client asks (the studio's) has no timer to ask PendingCount by, and hears it
	// here. Nothing to show: not a line of the transcript.
	if (!taken.empty())
		Publish(ibT("taken"), ibEmptyString, ibEmptyString);
	return taken;
}

void ibMcpServer::Reply(const ibString& text)
{
	{
		std::lock_guard<std::mutex> lock(m_saidMutex);
		m_conversation.push_back({ false, text });
	}

	Publish(ibT("assistant"), ibEmptyString, text);
}

ibString ibMcpServer::Greeting() const
{
	if (!IsRunning())
		return _("Assistant access is not running. Start it in the assistant access settings.");

	ibString out = ibString::Format(_("Assistant access is running (build %u)."), GetBuildId());

	// WHAT IS OPEN, because that is what an assistant would be working on and what makes the
	// line worth reading rather than a status light.
	const ibMetaDataApplicationBase* const metaData = GetMetaData();
	if (metaData != nullptr && metaData->IsOpen())
		if (const ibValueMetaObject* root = metaData->GetRootMetaObject())
			out += ibString::Format(_(" Application '%s' is open."), root->GetName());

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
// bell; whoever is connected gets it with the answer to its next call, or takes it with a tool. If
// nobody is calling, the message waits — which is exactly the complaint this exists to answer: text typed into the
// studio sat there until the person went and prodded the assistant from somewhere else.
//
// `sampling/createMessage` inverts it. The server asks; the client runs the completion and posts
// the answer back to the same endpoint, where Answer() recognises it as a RESPONSE (it names no
// method) and hands it to Reply. Nobody polls, and nobody waits on a socket.
//
// ⚠ AND THE PLATFORM STILL HAS NO MODEL, which is the part worth keeping. No key is stored here
// and no outbound connection is made: the completion runs wherever the client's already does, on
// the same assistant that is already working on this application.

bool ibMcpServer::CanAskModel() const
{
	// BOTH HALVES. Running is not enough — a client that never declared `sampling` in its
	// handshake will do nothing with the request, and a button enabled on hope is worse than one
	// that is honestly grey.
	return IsRunning() && m_clientCanSample.load();
}

bool ibMcpServer::AskModel(const ibString& question, ibString& refusal)
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

	if (!IsRunning()) {
		refusal = ibMcpText("Nobody is connected.");
		return false;
	}

	// OUR OWN NUMBERING. A client's id 1 and ours are different conversations, and the response
	// that comes back is matched by this value — mixing the two ranges is how an answer gets
	// attached to the wrong question.
	const long id = m_outgoingId.fetch_add(1);

	ibDataNode ask;
	ask.SetValue(ibT("jsonrpc"), ibString(ibT("2.0")));
	ask.AddField(ibT("id"), ibDataValue::Int((std::int64_t)id));
	ask.SetValue(ibT("method"), ibString(ibT("sampling/createMessage")));

	ibDataNode& params = ask.Child(ibT("params"));

	// ONE MESSAGE, THE PERSON'S OWN. The shape is the protocol's: a list of messages, each with a
	// role and a content block. Nothing is prepended to what was typed — a server that quietly
	// wraps a person's words in instructions of its own is answering a question they did not ask.
	std::shared_ptr<ibDataNode> message = std::make_shared<ibDataNode>();
	message->SetValue(ibT("role"), ibString(ibT("user")));

	ibDataNode& content = message->Child(ibT("content"));
	content.SetValue(ibT("type"), ibString(ibT("text")));
	content.SetValue(ibT("text"), question);

	params.AddField(ibT("messages"), ibDataValue::Array({ ibDataValue::Child(message) }));

	// A ceiling, because an unbounded request is one a client may refuse outright — and because
	// what comes back is shown in a window, not saved to a file.
	params.AddField(ibT("maxTokens"), ibDataValue::Int(4096));

	Tell(ibRpcRenderNode(ask));

	// Shown as asked the moment it is asked. The answer arrives later, through Answer() → Reply,
	// and a window that showed nothing in between would look like the question was swallowed.
	Publish(ibT("you"), question, ibEmptyString);

	return true;
}

// ⭐⭐ A TOOL RUNS WHERE A MOUSE CLICK RUNS.
//
// 🛑 Until this, every tool executed ON THE SOCKET THREAD — httplib's — and reached straight into
// the studio's tree and its widgets. wx allows exactly one thread near a widget, so what came
// back was not a refusal but a crash: `app_run` took the whole studio down on the spot, creating
// a form opened a modal and died on "only the main thread may do this", and a value edited from
// here raced the UI over a refcount ("DecRef: invalid ref data count"). None of it was diagnosable
// from the answer, because there was no answer — the process was gone.
//
// ⚠ AND IT IS A CLASS, NOT A LIST OF VERBS. Any door that asks a person something, or touches a
// control three layers down, fails the same way and only ever at run time. Marking the tools that
// are "safe without a person" would be the same convention this replaces, kept by the attention of
// whoever adds the next one.
//
// ⭐ NOTHING HAD TO BE BUILT. A session runs its tasks through its worker pool, which is where a click
// arrives too, and a tool is one more task there. The pool is asked of the SESSION, so a host that
// has none (tests) answers null and the tool runs here.
//
// ⚠ THE STATE IS SHARED, NOT CAPTURED BY REFERENCE. A timeout does not cancel the task — it only
// stops US waiting — so the task may still run after this function has returned and its stack is
// gone. Everything it touches therefore lives in the shared block, and the arguments are COPIED
// into it rather than pointed at.
bool ibMcpServer::RunTool(const class ibMcpTool* tool, const ibDataNode& arguments,
	ibDataNode& payload, ibString& refusal, bool* outPending)
{
	if (outPending != nullptr)
		*outPending = false;

	// ⭐ ON THE OPEN APPLICATION'S LIST, checked here because this is the one place every call
	// passes and an application can be opened, closed and reopened while the server runs. A
	// pointer compare when nothing has changed, which is almost always.
	WatchMetadata();

	// What touches the records needs the person's roles to give them to the assistant: see ibMcpTool::IsDataAssistant.
	if (tool->IsDataAssistant() && ibIsMcpInApplication()) {
		const ibMetaDataApplicationBase* const metaData = ibSession::Current()->GetMetaData();
		if (metaData == nullptr || !metaData->AccessRight_DataAssistant())
			ibBackendAccessException::Error(ibString::Format(ibMcpText("the data, for '%s': a role's Data assistant right gives it"), tool->GetName()));
	}

	// ⚠ …UNLESS THE TOOL MUST NOT BE THERE. See ibMcpTool::IsSessionTask: a verb that WAITS for
	// something the session's queue delivers cannot be run in it, and the debugger's evaluation is one.
	ibWorkerPool* pool = m_session != nullptr && tool->IsSessionTask()
		? m_session->GetWorkerPool() : nullptr;

	if (pool == nullptr)
		return tool->Call(arguments, payload, refusal);

	struct ibMcpPendingCall {
		const ibMcpTool* m_tool = nullptr;
		ibDataNode       m_arguments;
		ibDataNode       m_payload;
		ibString         m_refusal;
		bool             m_ok = false;
	};

	auto call = std::make_shared<ibMcpPendingCall>();
	call->m_tool      = tool;
	call->m_arguments = arguments;

	// ⭐ WRITTEN DOWN, BECAUSE WHEN THIS GOES WRONG THERE IS NOTHING ELSE TO READ. The journal
	// carried no `mcp` channel at all until 2026-09-04, so a call that reached the session and
	// never came back left the technology journal without a single line about it — the one place
	// somebody would look. Two lines per call: what was asked, and what came of it
	// with how long it took. Enough to tell "slow" from "never ran" without a debugger.
	const ibString toolName = tool->GetName();
	ibJournalInfo(ibT("mcp"), ibT("%s: handed to the session"), toolName);
	const auto startedAt = std::chrono::steady_clock::now();

	std::future<void> done = pool->Submit(m_session, [call]() {
		call->m_ok = call->m_tool->Call(call->m_arguments, call->m_payload, call->m_refusal);
	});

	// ⚠ FINITE, AND THE NUMBER IS A COMPROMISE STATED OUT LOUD. The wait is not there to bound the
	// WORK — applying an application legitimately takes minutes and must not be cut off — it is
	// there so a MODAL DIALOG cannot wedge the server for the rest of the day: the session's task is
	// then waiting until a person clicks, and nobody is watching this socket. Long enough for real
	// work, finite so a forgotten dialog is answered instead of hung on.
	if (done.wait_for(std::chrono::minutes(5)) != std::future_status::ready) {
		// ⚠ A WARNING — the journal's word for "nobody has to be stopped for this". Written as an error it
		// put a modal "Studio Error" box (and a dump) on top of the very dialog the call was waiting
		// behind, in front of a person who had pressed nothing (2026-09-28).
		ibJournalWarning(ibT("mcp"), ibT("%s: not finished within five minutes - most likely waiting behind "
			"a dialog; the call still runs when it is answered"), toolName);

		// 🛑 AND THIS IS NOT A REFUSAL, WHICH IS THE WHOLE POINT OF SAYING SO OUT LOUD. The work is
		// queued and WILL run; what expired is our patience, not the call. Reported as an error it
		// reads to a caller as "this did not happen", and the caller then does the one thing that
		// makes it worse — asks again. That is exactly how three unnamed shared modules were left
		// in somebody's application.
		if (outPending != nullptr)
			*outPending = true;

		refusal = ibMcpText("The window did not get to this within five minutes - it is most likely "
			"showing a dialog and waiting for a person. THE CALL WAS NOT ABANDONED and nothing was "
			"left half-done: it runs when the window is answered, and the outcome is simply not "
			"known here yet.\n"
			"DO NOT REPEAT THIS CALL. Repeating a call that creates something is how a "
			"application ends up with two of it. Read back what you were making instead - "
			"metadata_list or metadata_get for an object, module_read for text - and only act on "
			"what you find.");
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
	ibJournalInfo(ibT("mcp"), ibT("%s: %s in %lld ms"), toolName,
		call->m_ok ? ibString(ibT("done")) : (call->m_refusal.IsEmpty() ? ibString(ibT("refused")) : call->m_refusal),
		static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - startedAt).count()));

	return call->m_ok;
}

void ibMcpServer::Publish(const ibString& method, const ibString& request, const ibString& answer)
{
	std::lock_guard<std::mutex> lock(m_notifiersMutex);
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
static ibString ibMcpUndeclaredArgument(const ibMcpTool* tool, const ibDataNode& arguments)
{
	if (tool == nullptr)
		return ibEmptyString;

	ibDataNode schema;
	tool->DescribeInput(schema);

	auto properties = schema.FindChild(ibT("properties"));

	// A tool that declares no properties takes no arguments BY DESCRIPTION, but that is also what
	// a tool looks like before anyone wrote its schema — so silence here is read as "no opinion"
	// and lets the call through, rather than refusing every argument of an undocumented verb.
	if (!properties)
		return ibEmptyString;

	// ⚠ BOTH AREAS. A node keeps plain values in its FIELDS and composite ones in its PROPERTIES,
	// and an incoming argument lands in whichever its JSON shape calls for — `id: 5` is a field,
	// `text: {…}` is a property. Walking one of them would let exactly the other kind through
	// unchecked, which is the shape that started this: an object handed to an argument that wanted
	// a string.
	//
	// The schema side is asked with FindChild, which reads the property area — ibMcpSchemaArgument
	// declares every argument as a sub-node there.
	for (const auto& field : arguments.Fields()) {   // scalars and objects alike — one set since 2026-10-26
		if (!properties->FindChild(field.first))
			return field.first;
	}

	return ibEmptyString;
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
static ibString ibMcpArgumentsOnOffer(const ibMcpTool* tool, const ibString& given, ibString& closest)
{
	if (tool == nullptr)
		return ibEmptyString;

	ibDataNode schema;
	tool->DescribeInput(schema);

	auto properties = schema.FindChild(ibT("properties"));
	if (!properties)
		return ibEmptyString;

	const ibString wanted = given.Lower();
	ibString names;

	for (const auto& declared : properties->Fields()) {

		if (!names.IsEmpty())
			names += ibT(", ");
		names += declared.first;

		// ⭐⭐ AND WHICH OF THEM IS THE ONE THE VERB CANNOT WORK WITHOUT. A bare list of four names
		// still leaves the caller choosing between them, and the choice is not always obvious from
		// the word: `code_run` takes `text`, `said`, `commit`, `understood`, and which of those
		// carries the CODE is a guess until something says `text` is required (measured 2026-09-07 —
		// two wasted calls on one tool, `code` and then `session`). A shape is added only when it is
		// not a string, because passing a word where a flag or a number belongs is the other half
		// of the same mistake.
		for (const ibMcpTool::ibMcpArgument& argument : tool->Arguments()) {

			if (!argument.Name().IsSameAs(declared.first, false))
				continue;

			ibString marks;
			if (argument.IsRequired())
				marks = ibMcpText("required");

			switch (argument.KindOf()) {
				case ibMcpTool::ibMcpArgument::Kind::Whole:
					marks += (marks.IsEmpty() ? ibT("") : ibT(", ")); marks += ibMcpText("integer"); break;
				case ibMcpTool::ibMcpArgument::Kind::Flag:
					marks += (marks.IsEmpty() ? ibT("") : ibT(", ")); marks += ibMcpText("boolean"); break;
				case ibMcpTool::ibMcpArgument::Kind::Many:
					marks += (marks.IsEmpty() ? ibT("") : ibT(", ")); marks += ibMcpText("array");   break;
				case ibMcpTool::ibMcpArgument::Kind::Node:
					marks += (marks.IsEmpty() ? ibT("") : ibT(", ")); marks += ibMcpText("object");  break;
				default: break;
			}

			if (!marks.IsEmpty())
				names += ibT(" (") + marks + ibT(")");
			break;
		}

		// NEAR ENOUGH TO BE A TYPO: one name inside the other (`parent` in `parent_id`, `value` in
		// `values`). Deliberately not a distance measure — this is a nudge, and a wrong nudge is
		// worse than none when the full list is right beside it.
		const ibString candidate = declared.first.Lower();

		if (closest.IsEmpty() && wanted.length() >= 3
			&& (candidate.Find(wanted) != ibNOT_FOUND || wanted.Find(candidate) != ibNOT_FOUND))
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
// way at all to learn that this application carries written findings about itself.
//
// ⭐⭐ AND IT IS CUT. Claude Code keeps the first 2048 characters of it (2026-11-25: of 14,524, the rest never
// reached the model), so it holds who is here, the greeting, the chat, how tools are found — and points at WHERE THE
// REST IS: the corpus entry `where-to-start` (what does not change) and `platform_state` (what does). A test holds
// both modes to 2048 (test_mcpPersonNames.cpp); only the application's root note, said last, may run past it.
//
// ⚠ THE HELP TEXT IS NOT HERE. It is written for the person USING the application, and mixing the
// two audiences is how engineering prose ends up on somebody's F1 screen.
//
//---------------------------------------------------------------------------

void ibMcpDescribePlatform(const ibApplicationInstance* applicationInstance, ibDataNode& into)
{
	into.AddField(ibT("build"), ibDataValue::Int((std::int64_t)GetBuildId()));

	// ⭐⭐ AND WHICH BASE THIS IS. Every tool here answers for the process that happens to hold the port,
	// and two studios can stand open on two different bases — so an answer naming the application but
	// not the base lets a caller work in the wrong one and never learn it from anything but the journal
	// (2026-09-22: a print form, a command and an applied application, all built in the base somebody
	// else had open). Where the base lives is said beside it: a Firebird folder, or a PostgreSQL server's database.
	// A studio's to know: an application's person is given the base by their client, not where it lies.
	if (!ibIsMcpInApplication()) {
		into.SetValue(ibT("connection"), applicationInstance->GetDatabaseDescription());
		if (!applicationInstance->GetFile().IsEmpty())
			into.SetValue(ibT("base"), applicationInstance->GetFile());
	}

	const ibUserInfo& who = applicationInstance->GetUserInfo();

	if (!who.IsOk()) {

		// ⭐ NO ACCOUNT IS A STATE, NOT A GAP — a base with none opens without asking who you are,
		// which is an ordinary situation and not a failure to report one. Said out loud because a
		// MISSING field is unreadable: a caller cannot tell "there is no account system here"
		// from "this was never implemented" from "something went wrong".
		//
		// ⚠ And it is the most PERMISSIVE state, not the least: with nobody logged in there are no
		// roles to fold, so every right answers with its own declared default. That reads
		// backwards at first glance, which is exactly why it is spelled out.
		into.AddField(ibT("account"), ibDataValue::Bool(false));
		into.SetValue(ibT("accountNote"),
			ibMcpText("No accounts exist in this base, so nobody is logged in - it opens without asking. "
			  "That is not a restriction: with no roles to fold, every right answers with its "
			  "declared default. `metadata_rights` says what those are."));
	}
	else {

		into.AddField(ibT("account"), ibDataValue::Bool(true));

		ibDataNode& user = into.Child(ibT("user"));
		user.SetValue(ibT("name"), who.m_strUserName);

		if (!who.m_strUserFullName.IsEmpty())
			user.SetValue(ibT("fullName"), who.m_strUserFullName);

		std::vector<ibDataValue> roles;

		for (const ibString& name : ibMcpRoleNames(who))
			roles.push_back(ibDataValue::String(name));

		user.AddField(ibT("roles"), ibDataValue::Array(roles));

		if (who.IsSetLanguage())
			user.SetValue(ibT("language"), ibMcpLanguageName(who));
	}

	// The application of the session this is asked in — as the user above is that session's.
	ibMetaDataApplicationBase* metaData = ibApplicationInstance::GetActiveMetaData();

	if (metaData == nullptr || !metaData->IsOpen()) {
		into.AddField(ibT("applicationOpen"), ibDataValue::Bool(false));
		return;
	}

	into.AddField(ibT("applicationOpen"), ibDataValue::Bool(true));

	if (ibValueMetaObject* root = metaData->GetRootMetaObject()) {

		into.SetValue(ibT("application"), root->GetName());
		if (!root->GetSynonym().IsEmpty())
			into.SetValue(ibT("synonym"), root->GetSynonym());

		// ⭐ AND ITS ID, because the root is where the direction is written and nothing else
		// reports it: it appears in no listing (it is not of a listable KIND) and in no answer
		// about anything else. Telling a caller to write on the root without this is telling them
		// to address an object they have no way to name.
		into.AddField(ibT("applicationId"), ibDataValue::Int((std::int64_t)root->GetMetaID()));
	}

	// ⭐⭐ THE ONE A CALLER MUST RE-ASK. Everything else here drifts slowly; this can be flipped
	// between two calls, and code written in the other dialect is rejected by the compiler while
	// reading perfectly to a human.
	into.SetValue(ibT("dialect"), ibString(
		ibApplicationWritesInWords(metaData) ? ibT("word-fenced") : ibT("c-style")));

	// ⭐ HOW A QUERY IS WRITTEN, said before the strings: this answer taught only the text query, and an
	// assistant writing a demo base read every figure of a dashboard through `New Query` and a loop of maps
	// (2026-11-19, Max: "why don't you use LINQ in the platform?"). Then it said "LINQ first", and the same
	// assistant wrote LINQ where the database should have folded, in the long form (Max, the same day: "know
	// WHEN"). So it says which road does what, and the short forms.
	into.SetValue(ibT("queries"),
		ibMcpText("TWO ROADS, CHOSEN BY WHO DOES THE WORK.\n"
		  "A TEXT QUERY (`new Query`, `strings` below) when the DATABASE works: a balance at a moment, turnovers "
		  "over a period, a slice, a total or a grouping over many rows, a join the base makes - the rows never "
		  "travel.\n"
		  "LINQ when the answer is IN HAND and is reshaped - folded again, sorted, cut to the top N, joined to "
		  "another answer, read more than once:\n"
		  "  debts = q.Execute().Unload();\n"
		  "  top = from d in debts orderby d.Amount descending take 5;\n"
		  "  byCompany = from d in debts group d.Amount by d.Company into g "
		  "select { Company = g.Key, Amount = g.Values.Sum() };\n"
		  "The short forms: `group <value> by <key>` folds the value alone (`g.Values.Sum()`, no lambda); no "
		  "`select`, or `select d`, gives the rows themselves.\n"
		  "A source is read through `Data`, and the tables living in it through the dot, with the arguments a "
		  "query gives them: `Data.AccumulationRegisters.Sales.Turnovers(Begin, End)`, "
		  "`Data.AccountingRegisters.Ledger.Balance(At, Account)`, `Data.Documents.Sale.Goods`; "
		  "`Data.From(\"Journal.Events\", Begin, End)` reads one by the name a query writes. Call `linq_methods` "
		  "before writing one - the vocabulary and which form a job wants; `script_query` shows what the compiler "
		  "made of it."));

	// ⭐ HOW A TEXT IS WRITTEN, said beside the dialect because both dialects share the one lexer
	// (ibTranslateCode) and a caller writing code asks here first. A query in code is a MULTI-LINE
	// string, and without this a caller glues it together line by line with `+` (measured
	// 2026-09-15: a day of queries written that way through code_run, until Max pointed at `|`).
	into.SetValue(ibT("strings"),
		ibMcpText("A string literal may run over several lines, and that is how a query text is "
		  "written in code: the line break stays in the text, and every continuing line opens with "
		  "`|` after its indentation, which is dropped. So a query is ONE literal, not a sum of "
		  "lines:\n"
		  "  var query = New Query(\"SELECT Ref\n"
		  "  |FROM Catalog.Goods\n"
		  "  |WHERE Code = &Code\");\n"
		  "  query.SetParameter(\"Code\", code);\n"
		  "A query takes its text in New Query(...) - it has no Text property. "
		  "A quote inside a string is doubled (\"\"). A literal in apostrophes is a DATE, not a "
		  "string: '20260915'."));

	// ⭐ A FUNCTION AS A VALUE, and WHEN. The guide says it whole (guide.lambda), and nothing a caller reads first
	// pointed there: the only lambdas in these answers were "no lambda needed" (2026-11-20, Max: "is there any hint
	// in the platform that you can use lambdas, and when?").
	into.SetValue(ibT("functions"),
		ibMcpText("A FUNCTION CAN BE A VALUE: `Function(x) { return x * 2; }` (word-fenced: `Function(x) ... "
		  "EndFunction`) written where a value stands is stored, passed and called later, and reads the variables "
		  "around it at the time of the call. Three jobs want one: a CALLBACK (`arr.Where(Function(x) { return x > k; "
		  "})` - the collection decides when), an EVENT HANDLER, a RULE PASSED AS AN ARGUMENT (a comparison, a filter "
		  "a routine takes). Not for filtering, sorting or grouping what is in hand - `queries` above writes those in "
		  "keywords, shorter. One held in a field is called through a variable: `var g = s.f; g(1)`. "
		  "`syntax_get guide.lambda` - the whole of it."));

	std::vector<ibDataValue> languages;

	for (const ibString& language : ibListMetaObjectNames(metaData, ibT("Language")))
		languages.push_back(ibDataValue::String(language));

	into.AddField(ibT("languages"), ibDataValue::Array(languages));

	// 🛑 A NUMBER IS NOT AN ANSWER HERE. This reported 1001, which reads as a build or a revision
	// and is neither — it is the enumeration member `version_oes_last`, whose label is "Don't use
	// compatibility". So the figure hid the very fact it was supposed to carry: that compatibility
	// mode is switched OFF. A reader given 1001 could not have guessed it and had nowhere to look.
	//
	// ⭐ ASKED OF THE PROPERTY, which is the one thing that knows its own words — the same road
	// metadata_properties takes for every enumerated property. Not decoded here from the integer:
	// that would be this file keeping a private copy of a list the enumeration already owns, and
	// the copy would be wrong the first time a member was added.
	into.AddField(ibT("compatibilityVersion"), ibDataValue::Int((std::int64_t)metaData->GetVersion()));

	if (ibValueMetaObject* root = metaData->GetRootMetaObject()) {

		// ⭐ ASKED BY NAME. This walked every property of the root comparing names — which is what
		// GetProperty(name) does, on the object that owns them.
		if (const ibProperty* property = root->GetProperty(ibT("Version"))) {

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
				into.SetValue(ibT("compatibility"), held.GetString());
		}
	}

	// ⭐⭐ TWO QUESTIONS THAT ONE WORD WOULD HIDE. "Saved" sounds like one state and is two, with
	// different consequences and different cures:
	//
	//   unsavedEdits  - changed since it was last STORED. Close the studio now and this is
	//                   gone. `app_save` is the cure, and it is cheap.
	//   inDatabase    - the same as the copy the DATABASE holds. False means the running
	//                   application is still on the old one however much was saved.
	//                   `app_apply` is the cure, and it writes DDL.
	//
	// Reporting only one of them is how work is lost: today a note written through this server
	// survived every check a caller could make and died with the process, because nothing
	// answered the first question at all.
	// ⚠ THIS IS NOT "UNSAVED", THOUGH IT WAS REPORTED AS THAT AND THEN, WRONGLY, AS "TOUCHED SINCE
	// OPENED". The flag means "differs from the copy the DATABASE holds", and it is maintained at
	// all three points that can change that answer:
	//
	//   LOAD    — `Modify(!CompareMetadata(m_applicationMetadata))`, a real comparison
	//             (ibMetaDataApplicationStorage::LoadDatabase)
	//   EDIT    — set true by whatever changed something
	//   APPLY   — set FALSE once the application blob is committed
	//             (metadataApplicationQuery.cpp, inside the transaction guard)
	//
	// ⭐ AND IT STAYS TRUE AFTER A PLAIN SAVE, CORRECTLY. `app_save` persists the application
	// so it survives a re-login; it does NOT touch the database's copy, so the difference it
	// describes is still there. Nothing is failing to reset — the condition has not changed.
	//
	// What genuinely has no answer anywhere is whether that diskette save has happened, and
	// `storedUnknown` below says so rather than guessing.
	const bool differs = metaData->IsModified();

	into.AddField(ibT("differsFromDatabase"), ibDataValue::Bool(differs));

	// ⭐ ASKED OF THE CONTAINER, and there is nothing to recognise first. The ACTIVE metadata is a
	// application by definition — an external report or data tool is a container of the BASE
	// metadata class and never this one — so the cast to the storage class tested something that is
	// always true and then reported `inDatabase` only when it happened to hold.
	//
	// ⚠ AND NOT `GetApplication() != nullptr` EITHER, which was the other thing tried here: that
	// answers "is there a studio-side BASELINE", and it is null in the RUNTIME — precisely where
	// this field matters most. IsApplicationSave is virtual on the base and is the question itself.
	const bool applied = metaData->IsApplicationSave();

	into.AddField(ibT("inDatabase"), ibDataValue::Bool(applied));

	// ⭐⭐ THE THIRD FACT, AND IT USED TO BE A CONFESSION INSTEAD OF AN ANSWER. "Would closing the
	// studio lose this?" is the question a session actually needs, and it is independent of both
	// fields above: a plain save writes the store and leaves the database exactly as behind as it
	// was, so `differsFromDatabase: true` with nothing at risk is the ordinary state. The metadata
	// now keeps it (ibMetaData::IsStored) from the only two occasions that can move it.
	const bool unsaved = metaData->IsEdited();

	into.AddField(ibT("unsavedEdits"), ibDataValue::Bool(unsaved));

	// ⭐⭐ THE STATE AS A WORD, not two flags for the caller to combine. Two booleans carry three
	// meaningful situations, and leaving them to be folded outside is how one of the three gets
	// forgotten — the reader tests the flag they thought of and calls it done. The flags stay,
	// because a caller may want either half; the WORD is what the answer is about.
	// 🛑 AND THE WORD READ ONLY ONE OF THE TWO. Three situations, and this folded `differs` away:
	// an application that HAS been applied and has been edited since answered `inStep`, "nothing
	// is pending against it", in the same breath as `differsFromDatabase: true` two lines above.
	// A caller reading its own answer found it arguing with itself and had no way to tell which
	// half to believe (measured over MCP, 2026-09-03).
	if (!applied) {
		into.SetValue(ibT("state"), ibString(ibT("storedNotApplied")));

		// 🛑 THE FIRST HALF OF THIS SENTENCE USED TO BE ASSERTED WITHOUT KNOWING IT. The word says
		// "stored", and the text said "it survives closing the studio" — while four lines below,
		// the same answer admitted that nothing tracked whether it had been stored at all. Now it
		// is known, so the state says which of the two it is instead of asserting the good one.
		into.SetValue(ibT("meaning"), unsaved
			? ibMcpText("NOT stored: closing the studio loses this work. And the DATABASE holds an "
			    "older application besides, so the running application does not have any of it. "
			    "`app_save` writes the store; `app_apply` does that AND publishes to the "
			    "database. `database_diff` says what differs.")
			: ibMcpText("Stored - it survives closing the studio - but the DATABASE still holds an "
			    "older application, so the running application does not have any of it. "
			    "`database_diff` says what differs; `app_apply` writes it."));
	}
	else if (differs) {
		into.SetValue(ibT("state"), ibString(ibT("editedSinceApplied")));

		// ⚠ AND A DIFFERENCE IS NOT ALWAYS A SCHEMA DIFFERENCE, which is why this says which
		// question `database_diff` answers. A comment, a note, a module's text all change the
		// application and change no table, so the diff can legitimately answer "0" while this is
		// true — and a reader who takes the diff for the whole answer concludes the flag is broken.
		into.SetValue(ibT("meaning"),
			ibMcpText("The database's copy is behind this one: it was applied, and edited since. The "
			  "running application still works from what was applied. `database_diff` says what "
			  "of it reaches the SCHEMA - a comment, a note or a module changes the application "
			  "and no table, so an empty diff here means the edits are not structural, not that "
			  "there are none. `app_apply` puts them in."));
	}
	else {
		into.SetValue(ibT("state"), ibString(ibT("inStep")));
		into.SetValue(ibT("meaning"),
			ibMcpText("The database holds this application - nothing is pending against it."));
	}

	// ⭐ SAID ONLY WHEN THERE IS SOMETHING TO LOSE. This field was `storedUnknown` and it was there
	// on every differing application, saying the platform could not tell — advice to save "just in
	// case", which is the shape of a warning that gets read past. It is now a fact about THIS
	// session and appears only when it is true.
	if (unsaved)
		into.SetValue(ibT("atRisk"),
			ibMcpText("There are edits that exist only in this process. Closing the studio - including "
			  "a close for a rebuild - loses them, and nothing else in this answer says so: "
			  "`differsFromDatabase` is about the DATABASE and stays true after a perfectly good "
			  "save. `app_save` is the cure and is cheap."));

	// ⭐ AND WHETHER ANYTHING IS BEING WATCHED. Breakpoints are set against a runtime, not against
	// an application: with nothing attached they are set and never reached, which looks exactly
	// like code that does not run. `debug_sessions` gives the list; this is the count, so a
	// caller learns from ONE call whether that whole road is open.
	if (debugClient != nullptr) {

		int seen = 0;
		int attached = 0;

		// A runtime that answered — not a scanner's socket dialling a port (ibMcpDebugBridge::GetStop).
		debugClient->ForEachConnection([&seen, &attached](const auto& connection) {

			if (!connection.IsVerifiedConnection())
				return;

			seen++;

			if (connection.GetConnectionType() == ConnectionType_Debugger)
				attached++;
		});

		ibDataNode& debugger = into.Child(ibT("debugger"));
		debugger.AddField(ibT("runtimes"), ibDataValue::Int((std::int64_t)seen));
		debugger.AddField(ibT("attached"), ibDataValue::Int((std::int64_t)attached));

		if (seen > 0 && attached == 0)
			debugger.SetValue(ibT("note"),
				ibMcpText("Something is running but nothing is attached - breakpoints will not be "
				  "reached. `debug_attach` takes it."));
	}
}

namespace {

// The person's roles in a line.
ibString RoleNames(const ibUserInfo& who)
{
	ibString names;
	for (const ibString& name : ibMcpRoleNames(who)) {
		if (!names.IsEmpty())
			names << ibT(", ");
		names << name;
	}
	return names;
}

// ⭐ HOW THIS SERVER IS SPOKEN TO — beside WHERE YOU ARE, the first thing after the handshake (Max,
// 2026-10-28: "the engine must tell you how it communicates … when you get here"). A script without
// `use utf8` sent Cyrillic encoded twice, and the person read "Ð¿Ñ…" in the chat. The gate refuses that
// now (ibMcpArgumentFault); this line is so nobody has to be refused to learn it.
void AppendTextEncoding(ibString& out)
{
	out << ibT("Text travels as UTF-8, once: a non-Latin letter encoded twice (Perl without `use utf8`) ")
		<< ibT("is refused.\n");
}

// The language to answer in, by name; said in both modes. Nothing is said for a person who has none set.
void AppendPersonLanguage(ibString& out, const ibUserInfo& who)
{
	if (who.IsOk() && who.IsSetLanguage())
		out << ibT("The person is working in ") << ibMcpLanguageName(who)
			<< ibT(" - answer them in it, whatever language this text is in.\n");
}

// ⭐ WHOSE HANDS THESE ARE — "with their rights" is useless without a name: an assistant that does not know who it
// acts as cannot tell a refusal it caused from a permission it never had. No roles is not no rights: every right
// then answers with its declared default, the permissive end. (The password is in the same structure, untouched.)
void AppendActingAs(ibString& out, const ibUserInfo& who)
{
	if (!who.IsOk()) {
		out << ibT("THERE ARE NO ACCOUNTS in this base: nobody is logged in, and every right answers with its ")
			<< ibT("declared default.\n");
		return;
	}

	out << ibT("YOU ARE ACTING AS ") << who.m_strUserName;
	if (!who.m_strUserFullName.IsEmpty() && who.m_strUserFullName != who.m_strUserName)
		out << ibT(" (") << who.m_strUserFullName << ibT(")");
	out << ibT(".");

	if (who.m_roleArray.empty())
		out << ibT(" No roles: every right answers with its declared default.");
	else
		out << ibT(" Roles: ") << RoleNames(who) << ibT(".");

	out << ibT(" What you do is journalled under that name.\n");
}

// The window, and how what is typed in it reaches you: in the answer, since a client gives its model nothing else.
void AppendConversation(ibString& out)
{
	out << ibT("THE PERSON WRITES TO YOU in the assistant window: what they wrote arrives in the `chat` field of ")
		<< ibT("your next answer, whatever the tool. Answer with `chat_say`; `chat_history` reads the exchange ")
		<< ibT("back.\n\n");
}

// Two tools are listed, the rest found — the whole set is some 75 KB of schemas, most of it never used.
void AppendFindingTools(ibString& out)
{
	out << ibT("TOOLS: only `mcp_search` (the words of the job; | ^ $ .* make it a regular expression) and ")
		<< ibT("`mcp_call` {tool, arguments} are listed - `mcp_search` with no query lists all there is.\n\n");
}

// ⭐⭐ WHAT THE APPLICATION'S ASSISTANT IS TOLD — its own text, not the studio's with lines struck out: that one
// sells building, and every verb it leads to is not on offer here (Max, 2026-10-31: it says at once which mode this
// is and that nothing can be changed from it).
ibString BuildApplicationOrientation(const ibApplicationInstance* applicationInstance)
{
	ibString out;

	out << ibT("You are connected to a running OES Enterprise APPLICATION (build ")
		<< (unsigned)GetBuildId()
		<< ibT("), in the session of the person at it, with exactly their rights.\n");

	const ibUserInfo& who = applicationInstance->GetUserInfo();
	AppendPersonLanguage(out, who);

	out << ibT("\nTHIS IS NOT THE STUDIO: objects, forms, modules and reports are changed there, and those tools ")
		<< ibT("are not offered here - when the person asks for such a change, say it is the studio's work.\n\n");

	const ibMetaDataApplicationBase* const metaData = ibApplicationInstance::GetActiveMetaData();
	if (metaData == nullptr || !metaData->IsOpen()) {
		out << ibT("NO APPLICATION IS OPEN here.\n\n");
	}
	else if (metaData->AccessRight_DataAssistant()) {
		out << ibT("YOU MAY READ THE DATA THIS PERSON MAY SEE: `compose_run` answers with rows - from a report ")
			<< ibT("(`id`), a composition (`schema`) or a `query` (`query_sources` before a FROM, `query_check` ")
			<< ibT("before believing it); `report_show` opens one in their window. `window_run` works IN their ")
			<< ibT("window, under their roles and with no undo: say what you will change and get their yes ")
			<< ibT("first.\n\n");
	}
	else {
		out << ibT("YOU SEE THE APPLICATION, NOT THE DATA: this person's roles do not give the assistant the ")
			<< ibT("records - a role's *Data assistant* right does, and that is the administrator's to decide.\n\n");
	}

	out << ibT("FIRST, AND NOTHING ELSE RUNS UNTIL IT IS DONE: `chat_say` a greeting - only `chat_history`, ")
		<< ibT("`platform_state`, `metadata_tree` and `mcp_search` run before it. Say which assistant and model ")
		<< ibT("you are, the application you see, that here you work within this person's rights and change ")
		<< ibT("nothing in it, and ask what they need.\n\n");

	AppendConversation(out);
	AppendFindingTools(out);

	out << ibT("THE LANGUAGE IS NOT ONE YOU KNOW - look it up before writing it: `syntax_search`, `type_members`, ")
		<< ibT("`pattern_read`, `query_fields`.\n\n");

	AppendActingAs(out, who);
	AppendTextEncoding(out);

	// WHAT IT IS FOR, LAST: the root's note can be any length, and a client keeps only the start of this text
	// (see BuildOrientation) — so it is the one thing here that may be cut, and nothing after it is.
	if (metaData != nullptr && metaData->IsOpen()) {
		if (const ibValueMetaObject* const root = metaData->GetRootMetaObject()) {
			out << ibT("\nTHIS APPLICATION IS CALLED ") << root->GetName();
			const ibString synonym = root->GetSynonym();
			if (!synonym.IsEmpty() && synonym != root->GetName())
				out << ibT(" (") << synonym << ibT(")");
			out << ibT(". `metadata_tree` with depth 1 is the map of what is in it.\n");

			const ibString direction = root->GetNoteContent();
			if (!direction.IsEmpty())
				out << ibT("WHAT IT IS FOR - written on its root:\n") << direction << ibT("\n");
		}
	}

	return out;
}

ibString BuildOrientation(const ibApplicationInstance* applicationInstance)
{
	if (ibIsMcpInApplication())
		return BuildApplicationOrientation(applicationInstance);

	ibString out;

	out << ibT("You are connected to a running OES Enterprise STUDIO (build ")
		<< (unsigned)GetBuildId()
		<< ibT("), in the session of the person at it, with exactly their rights: what you change is ")
		<< ibT("their live application.\n");

	const ibUserInfo& who = applicationInstance->GetUserInfo();
	AppendPersonLanguage(out, who);

	out << ibT("\nFIRST, AND NOTHING ELSE RUNS UNTIL IT IS DONE: `chat_say` a greeting - a person is watching a ")
		<< ibT("window that shows nothing until you speak. Only `chat_history`, `platform_state`, `metadata_tree` ")
		<< ibT("and `mcp_search` run before it. Say which assistant and model you are, what you see, and ask what ")
		<< ibT("they need.\n\n");

	AppendConversation(out);
	AppendFindingTools(out);

	out << ibT("THE PLATFORM IS NOT ONE YOU KNOW - its script, queries and types are its own. Look before ")
		<< ibT("writing: `pattern_read` (how things are done here; no argument lists the entries), `syntax_search`; ")
		<< ibT("`script_check` / `query_check` compile without running. Names and code are English; what a ")
		<< ibT("person reads is in their language.\n\n");

	// ⭐⭐ AND WHERE THE REST IS — the line everything above leads to. The corpus entry carries what does not change (the
	// route, the notes, running what was built, the steps nothing leads to); `platform_state` what does. Whose hands and
	// the encoding come after it: a long list of roles may be cut, this line may not.
	out << ibT("THE REST IS NOT HERE - a client keeps only the start of this text. Read once, before building: ")
		<< ibT("`pattern_read` {name: \"where-to-start\", topic: \"all\"} (how work goes here, the steps nothing ")
		<< ibT("leads you to) and `platform_state` (this application, live).\n\n");

	AppendActingAs(out, who);
	AppendTextEncoding(out);

	return out;
}

} // namespace

ibString ibMcpServer::Answer(const ibString& request, const ibMcpWireHeaders& headers, int* outErrorCode)
{
	// THE VERDICT THE TRANSPORT WILL ASK FOR — recorded where the refusal is decided,
	// which is the only place that knows. Reading it back out of the emitted JSON was
	// the first attempt and it failed on a space: the writer emits `"code": -32020`
	// and the search looked for `"code":-32020`.
	if (outErrorCode != nullptr) *outErrorCode = 0;
	const auto refuse = [&](const ibDataValue& id, ibRpcError code, const ibString& text,
		const ibDataNode* data = nullptr) -> ibString {
		if (outErrorCode != nullptr) *outErrorCode = (int)code;
		return ibRpcWriteError(id, code, text, data);
	};
	// ⭐ IT MAY NOT BE A CALL AT ALL. Once the server can ask the client something, the same
	// endpoint starts receiving both — and they are told apart by one fact: a request names a
	// method, a response does not (rpcMessage.h). This has to be asked FIRST, because reading a
	// response as a request finds no method and answers "no method named ''" to what was actually
	// an answer to our own question.
	{
		ibDataValue answeringId;
		ibString    payload;
		bool        wasError = false;

		if (ibRpcParseResponse(request, answeringId, payload, wasError)) {

			// THE MODEL ANSWERED. It goes where every other side of this conversation goes — into
			// the window, through Reply — rather than back to whoever asked, who by design did not
			// sit waiting on a socket for it (see AskModel in the header).
			Reply(wasError
				? ibString::Format(_("The assistant could not answer: %s"), payload)
				: payload);

			// A response is answered with nothing: it IS the answer.
			return ibEmptyString;
		}
	}

	ibRpcRequest parsed;
	ibString     error;

	if (!ibRpcParseRequest(request, parsed, error)) {
		// A malformed message IS worth a line — but the line says what went
		// wrong, not what the bytes were.
		Publish(ibT("did"), ibString::Format(_("unreadable message: %s"), error), ibEmptyString);
		return ibRpcWriteError(ibDataValue(), ibRpcError::Parse, error);
	}

	// ⭐⭐ WHICH ERA IS SPEAKING, decided before the method is looked at: the era says what a request must carry and
	// how its answer is written (mcpEra.h). Asked of the request, once — what differs between the eras is the era's,
	// and none of it is decided here.
	const ibMcpEra& era = ibMcpEra::Of(parsed);

	const ibString refused = era.Admit(parsed, headers, outErrorCode);
	if (!refused.IsEmpty())
		return refused;

	// ⭐ THE PERSON IS TOLD SOMEBODY ARRIVED, as `initialize` tells them, by a request that names its caller in
	// `_meta` — which a client without a handshake does with every request, so its first is the arrival.
	if (!m_greeted) {
		if (auto meta = parsed.m_params.FindChild(ibT("_meta"))) {
			if (auto client = meta->FindChild(ibT("io.modelcontextprotocol/clientInfo"))) {
				ibString arrival = _("An assistant has connected");
				const ibString clientName = client->GetValue<ibString>(ibT("name"));
				if (!clientName.IsEmpty())
					arrival = ibString::Format(_("%s has connected"), clientName);

				// …AND IN WHICH REVISION, the one thing the person cannot see otherwise: which era the client
				// settled on. Outside `_()` — a revision is a number, not language.
				const ibString version = meta->GetValue<ibString>(ibT("io.modelcontextprotocol/protocolVersion"));
				if (!version.IsEmpty())
					arrival << ibString::Format(ibT(" (%s)"), version);

				m_greeted = true;
				Publish(ibT("did"), arrival + ibT("."), ibEmptyString);
			}
		}
	}

	// Every result, as the era writes it.
	const auto complete = [&era](const ibDataValue& id, ibDataNode result) -> ibString {
		era.Complete(result);
		return ibRpcWriteResult(id, result);
	};

	// EVERY SERVER MUST ANSWER THIS ONE — the modern era's way of asking "who are you and what do
	// you speak", in a single call and without a handshake. It says the same things `initialize`
	// says, which is why it is answered from the same material rather than from a second one; the
	// difference is that it opens nothing and may be asked at any time, including first.
	if (parsed.m_method == ibT("server/discover")) {

		ibDataNode result;

		// AND IT IS A LIST — asked of the one place that builds it, not assembled again here.
		result.AddField(ibT("supportedVersions"), ibDataValue::Array(ibMcpEra::SupportedVersionList()));

		// The same capabilities `initialize` reports: tools, prompts, and the logging that carries what
		// a person types in the assistant window.
		ibDataNode& capabilities = result.Child(ibT("capabilities"));
		capabilities.Child(ibT("tools"));
		capabilities.Child(ibT("prompts"));
		capabilities.Child(ibT("logging"));

		// The same orientation text initialize hands over - one source, two doors.
		result.SetValue(ibT("instructions"), BuildOrientation(m_applicationInstance));

		// ⚠ IN THE MODERN ERA'S SHAPE, whoever asks: it is that era's own method, and a client probing for it is
		// asking whether that era is spoken here.
		const ibMcpModernEra modern{};
		modern.FillCacheable(result);
		modern.Complete(result);

		return ibRpcWriteResult(parsed.m_id, result);
	}

	ibString answer;

	// THE OUTCOME OF A TOOL CALL, carried out to where the record is written. The call itself is
	// made deep inside the branch below; the journal line is written once, at the bottom, for every
	// tool — so the verdict has to travel that far rather than each branch writing its own.
	bool     toolRan = false;
	bool     toolOk = false;
	ibString toolRefusal;
	ibString toolKind;

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
	ibString   toolName;
	ibDataNode toolArguments;

	if (parsed.m_method == ibT("initialize")) {

		// ⭐ WHAT THE CLIENT SAID IT CAN DO, remembered here because this is the only time it is
		// said. `sampling` is the one that matters: it means the server may ask the client's model
		// a question of its own, which is what turns the assistant window from something that has
		// to be POLLED into something that answers when a person types in it.
		//
		// Read as PRESENCE, not as a value — the protocol declares a capability by naming it, and
		// what is inside the object is the capability's own options.
		if (auto capabilities = parsed.m_params.FindChild(ibT("capabilities")))
			m_clientCanSample.store(capabilities->Has(ibT("sampling")));
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
		//
		// AMONG THE LEGACY ERA'S REVISIONS: `initialize` is that era's own method, whoever sends it.
		const ibMcpLegacyEra legacy{};
		const std::vector<ibString>& versions = legacy.GetVersions();

		ibString agreed = versions.front();
		if (auto asked = parsed.m_params.FindField(ibT("protocolVersion"))) {
			if (asked->Kind() == ibDataKind::String) {
				const ibString wanted = asked->AsString();
				if (std::find(versions.begin(), versions.end(), wanted) != versions.end())
					agreed = wanted;
			}
		}
		result.SetValue(ibT("protocolVersion"), agreed);

		// Saying plainly what is on offer is what keeps a client from asking for something and
		// getting silence. RESOURCES are still not among them, and deliberately: everything that
		// would be one is already a verb (metadata_get, module_read, sheet_get, note_read), and half
		// of what a resource is worth is the SUBSCRIPTION — which is the same deferred decision as
		// `subscriptions/listen`. A read-only resource list would be half a surface: it can be shown
		// and cannot be trusted after the first change, which is worse than not offering it.
		ibDataNode& capabilities = result.Child(ibT("capabilities"));
		capabilities.Child(ibT("tools"));

		// ⭐⭐ PROMPTS ARE THE ONE SURFACE THE PERSON SEES. A tool is invisible to them — it is
		// offered to the model, and reaches them only if the model thinks to mention it. The corpus
		// of practice is written for whoever reads it ("a model today, a person tomorrow") and could
		// be reached by nobody but a model that already suspected it was there — the same blind spot
		// `worthReading` closes for the model and nothing closed for the person.
		capabilities.Child(ibT("prompts"));

		// ⚠ AND LOGGING, WHICH WAS BEING DONE WITHOUT BEING DECLARED. Say() writes
		// `notifications/message` on the stream the moment a person types — that IS the logging
		// capability being exercised, and a client is entitled to know it may arrive and to turn
		// it down with `logging/setLevel`. Sending it undeclared meant a strict client had every
		// right to treat the frame as unsolicited.
		capabilities.Child(ibT("logging"));

		ibMcpEra::FillServerInfo(result.Child(ibT("serverInfo")));

		// ⭐ ARRIVING ORIENTED, not arriving and then asking. `instructions` is the protocol's own
		// field for the server to say how it should be worked with, and it was left empty — so a
		// client connected, saw TWO tools, and had to discover by trial that the rest are found
		// rather than announced, that the application carries written findings, and that a
		// conversation was already in progress.
		//
		// ⚠ WHAT IS HERE IS THE MAP, NOT THE TERRITORY. The census tells a caller what kind of
		// place this is in a few lines; the notes themselves are NOT inlined, because they can run
		// to pages and this is read in full at every single connection. It says they exist and
		// what reads them, which is the part that cannot be guessed.
		result.SetValue(ibT("instructions"), BuildOrientation(m_applicationInstance));

		// ⭐ THE PERSON IS TOLD SOMEBODY ARRIVED. Until now a client could connect, read the whole
		// application and start working, and the window in front of the person showed nothing
		// at all — the difference between "nobody is there" and "somebody is reading everything
		// you have" was invisible. Announcing it is the least a window watching this owes them.
		//
		// The client NAMES ITSELF at the handshake; using that is better than "an assistant",
		// because two of them are then distinguishable and neither is anonymous.
		ibString arrival = _("An assistant has connected");

		if (auto client = parsed.m_params.FindChild(ibT("clientInfo"))) {
			const ibString clientName = client->GetValue<ibString>(ibT("name"));
			if (!clientName.IsEmpty())
				arrival = ibString::Format(_("%s has connected"), clientName);
		}

		// …and in the revision agreed above — see the arrival of a request without a handshake.
		arrival << ibString::Format(ibT(" (%s)"), agreed);

		m_greeted = true;

		Publish(ibT("did"), arrival + ibT("."), ibEmptyString);

		answer = complete(parsed.m_id, result);
	}
	// ⭐⭐ THE MENU A PERSON SEES, AND IT IS NINE ENTRIES RATHER THAN SIXTY-SEVEN.
	//
	// A prompt is an INVOKED THING, not a table of contents: putting the whole corpus here would
	// give a client a sixty-seven-item list to scroll, which is a worse surface than the search the
	// model already has. What belongs in a menu is the doors — and the corpus names its own: eight
	// entries are about how to use the others, and a search by the words of a trade reaches none of
	// them, because they are not about a trade.
	//
	// ⚠ THE LIST IS THE CORPUS'S OWN CHOICE, NOT A TASTE. These are the ones its tool description
	// calls out by name, plus `where-to-start`, which the connection's own instructions make step
	// three. Everything else is reachable through the parameterised `pattern` below, so nothing is
	// hidden — it is simply not in the menu, the same trade `tools/list` makes above.
	else if (parsed.m_method == ibT("prompts/list")) {

		static const ibChar* const kDoors[] = {
			ibT("body-map"), ibT("intake"), ibT("where-to-start"), ibT("combining"),
			ibT("same-shape-elsewhere"), ibT("document-chains"), ibT("unfamiliar-domain"),
			ibT("where-behaviour-lives"), ibT("building-an-erp"), nullptr
		};

		std::vector<ibDataValue> prompts;

		const std::vector<std::pair<ibString, ibString>> index = ibMcpPatternIndex();

		for (const ibChar* const* door = kDoors; *door != nullptr; ++door) {

			// ⚠ ASKED OF THE CORPUS RATHER THAN DESCRIBED HERE. A summary written out beside the
			// name would be a second copy of a sentence that lives in the entry, and the day the
			// entry is rewritten the menu would go on saying the old thing.
			const auto found = std::find_if(index.begin(), index.end(),
				[door](const std::pair<ibString, ibString>& each) { return each.first == *door; });

			if (found == index.end())
				continue;   // an entry renamed out of the corpus simply leaves the menu

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(ibT("name"), found->first);
			entry->SetValue(ibT("description"), found->second);
			prompts.push_back(ibDataValue::Child(entry));
		}

		// …AND ONE THAT REACHES EVERY OTHER ENTRY, so the menu is a door rather than a fence.
		{
			std::shared_ptr<ibDataNode> any = std::make_shared<ibDataNode>();
			any->SetValue(ibT("name"), ibString(ibT("pattern")));
			any->SetValue(ibT("description"), ibString::Format(
				ibMcpText("Any one of the %u entries of practice by name - how a thing is usually "
				  "built on this platform. The nine above are the ones about where to START; this "
				  "reaches the rest, which are about a trade: posting, pricing, payroll, VAT, "
				  "stock, closing a period, printed forms, reports, roles."),
				(unsigned int)index.size()));

			std::shared_ptr<ibDataNode> argument = std::make_shared<ibDataNode>();
			argument->SetValue(ibT("name"), ibString(ibT("name")));
			argument->SetValue(ibT("description"),
				ibMcpText("Which entry. `pattern_read` with no argument lists them all."));
			argument->AddField(ibT("required"), ibDataValue::Bool(true));

			any->AddField(ibT("arguments"),
				ibDataValue::Array({ ibDataValue::Child(argument) }));

			prompts.push_back(ibDataValue::Child(any));
		}

		ibDataNode result;
		result.AddField(ibT("prompts"), ibDataValue::Array(prompts));
		era.FillCacheable(result);
		answer = complete(parsed.m_id, result);
	}
	// ⭐ AND ONE ENTRY, WHOLE. The corpus is prose meant to be read by whoever is deciding — so the
	// message is the text as it stands, not a summary of it: a prompt that paraphrases the thing it
	// is a door to has put a second author between the reader and the writing.
	else if (parsed.m_method == ibT("prompts/get")) {

		ibString wanted = parsed.m_params.GetValue<ibString>(ibT("name"));

		// The parameterised one carries the real name in its argument; every other prompt IS the
		// name. Written this way round so a client that offers `pattern` and a client that offers
		// the nine doors both end up asking the same question.
		if (wanted.IsSameAs(ibT("pattern"), false)) {
			if (auto arguments = parsed.m_params.FindChild(ibT("arguments")))
				wanted = arguments->GetValue<ibString>(ibT("name"));
			else
				wanted.clear();
		}

		const ibString text = wanted.IsEmpty() ? ibString() : ibMcpPatternText(wanted);

		if (text.IsEmpty()) {

			// ⚠ REFUSED WITH THE NAMES, not with "no such prompt". A person choosing from a menu
			// cannot mistype; a model calling this by name can, and the list is the answer.
			ibString names;
			for (const std::pair<ibString, ibString>& each : ibMcpPatternIndex())
				names << (names.IsEmpty() ? ibT("") : ibT(", ")) << each.first;

			answer = refuse(parsed.m_id, ibRpcError::InvalidParams,
				wanted.IsEmpty()
					? ibMcpText("Say which entry: `pattern` takes a `name` argument.")
					: ibString::Format(
						ibMcpText("There is no entry called '%s'. There are: %s."), wanted, names),
				nullptr);
		}
		else {
			ibDataNode result;
			result.SetValue(ibT("description"), wanted);

			std::shared_ptr<ibDataNode> content = std::make_shared<ibDataNode>();
			content->SetValue(ibT("type"), ibString(ibT("text")));
			content->SetValue(ibT("text"), text);

			std::shared_ptr<ibDataNode> message = std::make_shared<ibDataNode>();
			message->SetValue(ibT("role"), ibString(ibT("user")));
			message->AddField(ibT("content"), ibDataValue::Child(content));

			result.AddField(ibT("messages"),
				ibDataValue::Array({ ibDataValue::Child(message) }));

			answer = complete(parsed.m_id, result);
		}
	}
	else if (parsed.m_method == ibT("tools/list")) {

		ibDataNode result;

		// ⭐ THE FEW THAT LET A CALLER FIND THE REST. Handing over sixty-seven tools with their
		// schemas costs some fifty kilobytes before any work begins, spent in full every session
		// for the dozen actually used — so the handshake announces the finders, and everything
		// else is fetched by name through mcp_search and invoked through mcp_call. Nothing is
		// hidden; it is unannounced, and mcp_search with no query still lists it all.
		//
		// ⚠ IN THE ORDER OF THEIR NAMES, the same on every call and every build: a client keeps the list
		// (`ttlMs`) and compares the next one with it, and the order of registration is the linker's.
		std::vector<const ibMcpTool*> listed;
		for (const ibMcpTool* tool : ibMcpTools())
			if (tool->IsAlwaysListed() && ibIsMcpToolOffered(tool))
				listed.push_back(tool);
		std::sort(listed.begin(), listed.end(), [](const ibMcpTool* a, const ibMcpTool* b) {
			return a->GetName() < b->GetName();
		});

		std::vector<ibDataValue> tools;
		for (const ibMcpTool* tool : listed) {
			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(ibT("name"), tool->GetName());
			entry->SetValue(ibT("description"), tool->GetDescription());
			tool->DescribeInput(entry->Child(ibT("inputSchema")));

			tools.push_back(ibDataValue::Child(entry));
		}

		result.AddField(ibT("tools"), ibDataValue::Array(tools));
		era.FillCacheable(result);
		answer = complete(parsed.m_id, result);
	}
	else if (parsed.m_method == ibT("tools/call")) {

		ibString name = parsed.m_params.GetValue<ibString>(ibT("name"));

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
		ibString envelopeRefusal;

		if (name.IsSameAs(ibT("mcp_call"), false)) {

			auto outer = parsed.m_params.FindChild(ibT("arguments"));

			const ibString inner = outer
				? outer->GetValue<ibString>(ibT("tool")) : ibString();

			if (inner.IsEmpty()) {
				envelopeRefusal =
					ibMcpText("mcp_call needs `tool` - the name of what to invoke. mcp_search finds it.");
			}
			else if (inner.IsSameAs(ibT("mcp_call"), false)) {
				// A CALL THAT CALLS ITSELF is refused rather than recursed into: the loop would
				// unwrap forever, and there is no reading of it that means anything.
				envelopeRefusal = ibMcpText("mcp_call cannot invoke itself.");
			}
			else if (!outer->FindChild(ibT("arguments"))
				&& outer->Has(ibT("arguments"))) {

				// 🛑 THE ENVELOPE IS WRONG, AND IT USED TO BE THE LETTER THAT SAID SO. `arguments`
				// arriving as anything but an object - a JSON string, a number - is invisible to
				// FindChild, so the tool ran with NOTHING and complained about its own first
				// required argument: "chat_say needs 'text', and it did not come", of a call that
				// carried it. The caller then looks at the tool, which is fine, instead of at the
				// encoding, which is not.
				//
				// Refused HERE, where the fault is, and named as what it is.
				envelopeRefusal = ibString::Format(
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
		if (tool != nullptr && !ibIsMcpToolOffered(tool))
			tool = nullptr;

		if (!envelopeRefusal.IsEmpty()) {
			answer = ibRpcWriteError(parsed.m_id, ibRpcError::InvalidParams, envelopeRefusal);
		}
		else if (tool == nullptr) {
			answer = refuse(parsed.m_id, ibRpcError::MethodNotFound,
				ibString::Format(ibT("No tool named '%s'"), name));
		}
		else {
			ibDataNode arguments;

			// The real arguments live one level in when the call arrived wrapped.
			auto given = parsed.m_params.FindChild(ibT("arguments"));

			if (unwrapped && given)
				given = given->FindChild(ibT("arguments"));

			if (given)
				arguments = *given;

			// CARRIED OUT to the announcement and the journal — the real tool and its real
			// arguments, not the envelope they arrived in. See the note where these are declared.
			toolName = name;
			toolArguments = arguments;

			// 🛑 A CLIENT NEED NOT INTRODUCE ITSELF TO WORK. The token is what admits it;
			// `initialize` is a courtesy the protocol expects and nothing here enforces — so a
			// caller can read and change an application having said nothing, and the person
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
				Publish(ibT("did"),
					_("Something began using the tools without introducing itself - no handshake, "
					  "only the access key. What it does appears below."),
					ibEmptyString);
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
			const ibString undeclared = ibMcpUndeclaredArgument(tool, arguments);

			ibDataNode payload;
			ibString   refusal;
			bool       ok = false;

			// THE THIRD OUTCOME — see RunTool. Neither done nor refused: still queued, and it will
			// run. Kept apart from `ok` because the two answer different questions, and the wire
			// has a place for exactly this distinction below.
			bool       pending = false;

			// ⚠ THE ENGINE THROWS, AND THAT IS AN ANSWER TOO. A tool calls the
			// platform's own doors, and those report a refusal by raising — with
			// the diagnosis inside, which is exactly what a caller needs. Left
			// uncaught it would leave the tool's frame, cross the client's call
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
			ibString kind;

			try {
				// The undeclared name is answered as a REFUSAL rather than a protocol error, so it
				// arrives the way every other "I did not do that, and here is why" arrives — in
				// the same field the caller already reads.
				//
				// ⭐⭐ NOT A REMINDER — A DOOR. There is a person sitting in front of this studio, and
				// from their side a connection is INVISIBLE until something says so: an assistant that
				// reads their whole application in silence looks exactly like one that never came.
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
				if (!m_introduced && !ibMcpRunsBeforeGreeting(name)) {

					refusal = ibMcpText("Nobody knows you are here yet. Say who you are first: `chat_say` "
						"with WHO YOU ARE (which assistant and which model), WHAT YOU SEE (the "
						"application by name and what is in it - `platform_state` and "
						"`metadata_tree` depth 1 answer that), ANYTHING ALREADY WORTH SAYING, and "
						"WHAT YOU MEAN TO DO. Until then only those four verbs and `chat_history` "
						"run - the person sitting there sees a window that has been silent while "
						"their application was being read, and that is the one thing this "
						"platform will not allow.");
				}
				else if (!undeclared.IsEmpty()) {

					ibString closest;
					const ibString names = ibMcpArgumentsOnOffer(tool, undeclared, closest);

					refusal = ibString::Format(
						_("'%s' takes no argument called '%s'. Nothing was done."), name, undeclared);

					if (!closest.IsEmpty())
						refusal += ibString::Format(_(" Did you mean '%s'?"), closest);

					if (!names.IsEmpty())
						refusal += ibString::Format(ibMcpText(" It takes: %s."), names);
				}
				// …AND THE OTHER HALF OF THE SAME GATE: a name the tool declared as required and
				// nobody sent. Refused BEFORE the tool runs, because what a tool does with an
				// argument that never came is undefined by construction — see ibMcpMissingArgument.
				else if (const ibString missing = ibMcpMissingArgument(tool, arguments);
					!missing.IsEmpty()) {
					refusal = ibString::Format(
						_("'%s' needs '%s', and it did not come. Nothing was done."), name, missing);

					// ⭐ AND WHAT THAT ARGUMENT IS, in the words the schema already uses. A name on
					// its own sends the caller off to read the schema — the same round trip the
					// unknown-name refusal above exists to avoid — and for an argument whose word
					// does not carry its meaning (`position`, `understood`, `said`) the trip is the
					// only way to find out. One sentence, taken from the declaration, ends it.
					for (const ibMcpTool::ibMcpArgument& argument : tool->Arguments()) {

						if (!argument.Name().IsSameAs(missing, false))
							continue;

						ibString says = argument.Description();
						const int stop = says.Find(ibT(". "));
						if (stop != ibNOT_FOUND)
							says = says.Left(stop + 1);

						if (!says.IsEmpty())
							refusal += ibT(" ") + missing + ibT(" - ") + says;
						break;
					}
				}
				// …AND THE THIRD HALF OF THE SAME GATE: an argument that came in the wrong SHAPE, or
				// a word outside the closed set the schema publishes. The name gate and the missing
				// gate both stop a call that cannot work; this one stops a call that would work on
				// something the caller did not mean — `id: "abc"` read as id 0, a `comparison` word
				// nobody declared taken as the default. See ibMcpArgumentFault.
				else if (const ibString fault = ibMcpArgumentFault(tool, arguments);
					!fault.IsEmpty()) {
					refusal = fault;
				}
				else {
					ok = RunTool(tool, arguments, payload, refusal, &pending);
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
				kind = ibString::Format(ibT("query %u:%u"), e.GetLine(), e.GetColumn());
				refusal = e.GetErrorDescription();
			}
			catch (const ibBackendQueryException& e)     { kind = ibT("query");     refusal = e.GetErrorDescription(); }
			catch (const ibBackendAccessException& e)    { kind = ibT("access");    refusal = e.GetErrorDescription(); }
			catch (const ibBackendLockException& e)      { kind = ibT("lock");      refusal = e.GetErrorDescription(); }
			catch (const ibBackendInterruptException& e) { kind = ibT("interrupt"); refusal = e.GetErrorDescription(); }
			catch (const ibCoreException& e)          { kind = ibT("platform");  refusal = e.GetErrorDescription(); }
			catch (const std::exception& e)              { kind = ibT("internal");  refusal = ibString::FromUTF8(e.what()); }
			catch (...) {
				kind = ibT("unknown");
				refusal = ibString::Format(
					_("'%s' failed in a way it does not describe."), name);
			}

			// Said in the answer itself, because a caller reads the text and not
			// our source: "[access] ..." is actionable where a bare sentence is a
			// guess.
			if (!kind.IsEmpty())
				refusal = ibT("[") + kind + ibT("] ") + refusal;

			// A REFUSAL IS AN ANSWER, not a transport failure: the caller is a
			// machine that will try something else, and it needs the reason in
			// the same place it reads a result from.
			std::shared_ptr<ibDataNode> content = std::make_shared<ibDataNode>();
			content->SetValue(ibT("type"), ibString(ibT("text")));

			// ⭐⭐ SAID ONCE IS NOT SAID. The orientation asks a client to introduce itself in the
			// assistant window before any work, in as many words — and it is handed over exactly
			// once, at the handshake. A client that runs for hours, or one whose own context was
			// compacted, no longer has that text in front of it and cannot be expected to obey a
			// rule it can no longer read. It then works in silence, and the person watching their
			// own application change has no idea who is doing it.
			//
			// So the reminder rides the ANSWER, which is the one thing a working client always
			// reads, and stops the moment it is honoured. Not a nag: it is the platform stating an
			// expectation at a moment when the reader can still act on it.
			//
			// ⚠ THE FACT IS "HAS SPOKEN TO THE PERSON", which is not m_greeted — that one means
			// "somebody connected" and is set by the handshake or by the first tool call, neither
			// of which the person sees as an introduction.
			if (ok && name == ibT("chat_say"))
				m_introduced = true;

			// ⭐⭐ AN IMAGE TRAVELS AS AN IMAGE. A tool that answers with a picture (screen_capture)
			// puts the bytes in `image` with `imageType` beside them, and they leave here as a
			// content block of their own — the protocol has a kind for exactly this. Left in the
			// text they would arrive as a wall of base64: technically the same bytes, and
			// unreadable, which for a picture means useless.
			std::shared_ptr<ibDataNode> picture;
			ibString                    caption;
			if (ok) {
				auto image = payload.FindField(ibT("image"));
				auto type  = payload.FindField(ibT("imageType"));

				if (image && image->Kind() == ibDataKind::String && !image->AsString().IsEmpty()) {

					picture = std::make_shared<ibDataNode>();
					picture->SetValue(ibT("type"), ibString(ibT("image")));
					picture->SetValue(ibT("data"), image->AsString());
					picture->SetValue(ibT("mimeType"),
						type && type->Kind() == ibDataKind::String && !type->AsString().IsEmpty()
							? type->AsString() : ibString(ibT("image/png")));

					// 🛑 AND THE TEXT BLOCK STOPS CARRYING THE SAME BYTES. Rendered whole, the answer
					// would repeat the picture as a wall of base64 beside it — the identical data in
					// the form that costs the most and reads the least. What stays is what the image
					// cannot say for itself: what was in focus, and how big it is.
					ibString said = ibMcpText("A picture is attached.");

					if (auto focus = payload.FindField(ibT("focus")))
						if (focus->Kind() == ibDataKind::String && !focus->AsString().IsEmpty())
							said << ibT("\n") << ibMcpText("In focus: ") << focus->AsString();

					if (auto note = payload.FindField(ibT("note")))
						if (note->Kind() == ibDataKind::String && !note->AsString().IsEmpty())
							said << ibT("\n") << note->AsString();

					caption = said;
				}
			}

			// ⭐⭐ WHAT IS SAID TO THE MODEL BESIDE THE ANSWER - the person's messages and the reminders below, each
			// under its name. They go INTO the answer's text (see where it is written): a field beside `content` is
			// one a client need not hand its model, and a content block of its own broke the JSON of a client that
			// glues the blocks together (2026-09-03, the greeting's reminder).
			ibDataNode notes;

			// ⭐⭐ WHAT THE PERSON WROTE MEANWHILE - the knock on the door. A client keeps no line open to its model:
			// the model reads what its own calls answer and nothing else, so a message typed into the window waited
			// until the assistant thought of `chat_take` - which, in the middle of work, it does not. Sampling would
			// ring the model, and the clients in use today do not take it; the GET stream reaches the client, not the
			// model. The answer to the next call is what a working client always reads, so the messages travel there -
			// TAKEN, as `chat_take` takes them: the window shows them collected, and nothing hands them over twice.
			const std::vector<ibString> said = TakeSaid();
			if (!said.empty()) {

				ibString knock = ibMcpText("THE PERSON WROTE TO YOU in the studio's assistant window while you "
					"were working. Answer there with `chat_say` - they are watching it; `chat_take` will not "
					"hand these over again.");
				for (const ibString& each : said)
					knock << ibT("\n\n") << each;

				notes.SetValue(ibT("chat"), knock);
			}

			// ⭐⭐ THE PLATFORM REMEMBERS WHAT THE CALLER FORGOT, AND SAYS IT ONCE.
			//
			// A template is always a shape somebody has already worked out; so is a form, so is a
			// register. The corpus holds those, and a caller has to KNOW IT IS THERE to ask. This
			// is not a hypothetical: on 2026-09-05 a printed form was built through twenty-five
			// calls of these very tools without one of those entries being opened, and the pattern
			// describing the bands, the optional column blocks and the whole crossing had been
			// written for weeks. The knowledge was two steps away and nothing stood between the
			// caller and the mistake.
			//
			// ⚠ ONCE PER FAMILY, WHICH IS THE WHOLE DIFFERENCE BETWEEN A REMINDER AND A NAG. On
			// every call it would be sixty of them during one layout, and sixty is read past. The
			// grain is the family (`sheet`, `form`, `metadata`) because that is what a body of
			// craft is written at. Same shape as the greeting above: said at a moment the reader
			// can act on it, and stopped the moment it is honoured.
			if (ok && !name.IsEmpty()) {

				const ibString family = name.BeforeFirst(ibT('_'));

				// The corpus's own doors are exempt: pointing at the patterns from inside
				// pattern_read is a loop, and mcp_search already answers with places.
				const bool asksTheCorpusItself =
					family == ibT("pattern") || family == ibT("mcp") || family == ibT("syntax");

				if (!asksTheCorpusItself && m_hinted.insert(family).second) {

					// The same road mcp_search takes — whatever carries a body of text is asked to
					// point INTO it with these words. No second matcher, and nothing to keep in step.
					std::vector<ibDataValue> places;
					ibMcpFindInside(family, places);

					// ⭐⭐ AND WHETHER THIS APPLICATION SPEAKS MORE THAN ONE LANGUAGE — said at the
					// first thing a caller does, because it changes how EVERY visible string is
					// written from that moment on.
					//
					// A synonym, a form's title, a column heading, a caption on a printed blank: all
					// of them are localised strings, and in a two-language application a bare one
					// is stored as itself and shown as itself, so the second language gets the first
					// language's words. Nothing refuses it and nothing looks wrong — the form is
					// correct in the language whoever built it is looking at, and the failure belongs
					// to somebody else, later.
					//
					// ⚠ AND IT IS ONLY WORTH SAYING BEFORE THE WRITING, not after: retrofitting is
					// per-cell and per-object, and there may be hundreds. A caller that has this
					// early writes every language as it goes, which costs nothing extra when the one
					// writing is a model that can translate.
					ibMetaDataApplicationBase* const metaData = GetMetaData();
					if (!m_saidLanguages && metaData != nullptr && metaData->IsOpen()) {

						const std::vector<ibString> languages =
							ibListMetaObjectNames(metaData, ibT("Language"));

						// …and not over the answer's own `languages` (platform_state lists them): said with the
						// next family's first answer instead.
						if (languages.size() > 1 && !payload.Has(ibT("languages"))) {

							ibString named;
							for (const ibString& each : languages)
								named += (named.IsEmpty() ? ibString() : ibString(ibT(", "))) + each;

							m_saidLanguages = true;
							notes.SetValue(ibT("languages"),
								ibString::Format(
									ibMcpText("This application declares %d languages (%s), so every string a "
									  "PERSON reads - a synonym, a form title, a column heading, a caption on "
									  "a blank - has to carry all of them. Write them together as you go: a "
									  "bare string is shown as itself in every language, and correcting it "
									  "later is one edit per object. Parameter names and identifiers are not "
									  "language and stay as they are. Said once."),
									(int)languages.size(), named));
						}
					}

					if (!places.empty()) {

						// ⭐ TWO BODIES OF TEXT ANSWER THIS, and the difference between them is the
						// difference between knowing HOW a thing is built and knowing what it is
						// CALLED. The patterns carry the craft; the syntax helper carries the
						// names, signatures and guides. A caller pointed at only one of them gets
						// half an answer — a template builder was told how a blank is cut into
						// bands and never that `SpreadsheetDocument` and `GetArea` exist.
						// Each side counted by its own mark - a helper's place carries `syntax` or `type`. A third
						// body answers too, the notes of this application (a place with an `id`), and the rest of
						// the list taken as the helper's said "the syntax helper" of a word only the notes held.
						int fromPatterns = 0, fromSyntax = 0;
						for (const ibDataValue& each : places) {
							if (each.Kind() != ibDataKind::Child)
								continue;
							const auto place = each.AsChild();
							if (place->Has(ibT("pattern")))
								++fromPatterns;
							else if (place->Has(ibT("syntax")) || place->Has(ibT("type")))
								++fromSyntax;
						}

						// 🛑 AND NO COUNT IS QUOTED, BECAUSE THERE IS NONE TO QUOTE. Both sides cap
						// what they hand back — the patterns at two dozen, the helper at four — so
						// the length of this list is a CEILING and printing it as a number said
						// "24 places" about three different words in a row (2026-09-05, on the
						// first live reading of this hint). A ceiling presented as a measurement is
						// the same mistake as every other one this file has been fixing today, and
						// the number bought nothing: nobody chooses what to read by how many hits
						// there were.
						ibString where;
						if (fromPatterns > 0)
							where << ibString::Format(
								ibMcpText("the pattern corpus, for how this is usually built here "
								  "(pattern_read {query: '%s'})"), family);

						if (fromSyntax > 0) {
							if (!where.IsEmpty())
								where << ibMcpText(", and ");
							where << ibString::Format(
								ibMcpText("the syntax helper, for what it is called and what it takes "
								  "(syntax_search {query: '%s'})"), family);
						}

						if (!where.IsEmpty())
							notes.SetValue(ibT("worthReading"),
								ibString::Format(
									ibMcpText("`%s` is written about in %s. Knowledge no single answer "
									  "carries. Said once."),
									family, where));
					}
				}
			}

			// THE TEXT, WRITTEN NOW THAT EVERYTHING IT CARRIES IS KNOWN. A JSON answer takes the notes as fields of its
			// own - a field the tool answered stays the tool's - and is rendered against the application this server
			// serves, so a type reads as "CatalogRef.Goods" rather than as a number that means nothing on the other
			// side of the socket. A refusal, or a picture's caption, is text already: the notes follow it as paragraphs.
			if (ok && picture == nullptr) {
				for (const auto& [field, value] : notes.Fields())
					if (!payload.Has(field))
						payload.AddField(field, value);

				content->SetValue(ibT("text"), ibRpcRenderNode(payload, ibMetaTypeResolver(GetMetaData())));
			}
			else {
				ibString text = picture != nullptr ? caption : refusal;
				for (const auto& [field, value] : notes.Fields())
					text << ibT("\n\n") << value.AsString();

				content->SetValue(ibT("text"), text);
			}

			// ⚠ AND THE BLOCKS ARE MADE AFTER IT: a block takes a COPY of its node (ibDataValue::Child), so a text set
			// after the block was made never left - the picture's caption did not, and its bytes went out twice.
			std::vector<ibDataValue> blocks = { ibDataValue::Child(content) };
			if (picture != nullptr)
				blocks.push_back(ibDataValue::Child(picture));

			ibDataNode result;
			result.AddField(ibT("content"), ibDataValue::Array(blocks));

			// 🛑 A CALL STILL RUNNING IS NOT AN ERROR, and marking it one is what makes a caller
			// repeat it. `isError` is the only field a client is obliged to read, and to a machine
			// it means "this did not happen" — while the work is queued and about to happen. The
			// text says so in words, but words are what the person reads.
			//
			// So the flag stays off and the outcome is stated in a field of its own: a caller that
			// looks at nothing else still learns the one thing that decides what to do next, which
			// is that repeating is the wrong move.
			if (pending)
				result.SetValue(ibT("outcome"), ibString(ibT("pending")));
			else if (!ok)
				result.SetValue(ibT("isError"), true);

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
			if (ok && name == ibT("chat_take")) {

				auto empty = [&payload](const ibChar* field) {
					auto list = payload.FindField(field);
					return !list
						|| list->Kind() != ibDataKind::Array
						|| list->AsArray().empty();
				};

				toolFruitless = empty(ibT("messages")) && empty(ibT("observed"));
			}

			answer = complete(parsed.m_id, result);
		}
	}
	else if (parsed.m_method == ibT("ping")) {

		// ⭐ THE PROTOCOL'S OWN LIVENESS CHECK, and it used to fall through to "no method named
		// ping". A client that pings to see whether anybody is home would read that error as an
		// answer of the wrong kind and could reasonably drop the connection — the one failure
		// where refusing correctly is worse than saying nothing.
		//
		// An empty result IS the answer the spec asks for; there is nothing to report.
		answer = complete(parsed.m_id, ibDataNode());
	}
	else if (parsed.m_method == ibT("logging/setLevel")) {

		// ⚠ WE SEND LOG NOTIFICATIONS, so we owe the client the switch that turns them down.
		// Say() writes `notifications/message` on the stream when a person types; a server that
		// emits those and then refuses to be told how much of it is wanted is advertising a
		// capability it does not honour.
		const ibString level = parsed.m_params.GetValue<ibString>(ibT("level"));

		// Kept as the NUMBER of a level, so comparing is comparing — the words are the wire's
		// business, not the gate's.
		m_logLevel.store(ibMcpLevelRank(level));

		answer = complete(parsed.m_id, ibDataNode());
	}
	else if (!parsed.WantsAnswer()) {
		// A notification we do not act on — `notifications/initialized`, `notifications/cancelled`
		// and their kind. The protocol says answer nothing at all.
		answer = ibEmptyString;
	}
	else {
		answer = refuse(parsed.m_id, ibRpcError::MethodNotFound,
			ibString::Format(ibT("No method named '%s'"), parsed.m_method));
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
	if (parsed.m_method == ibT("tools/call")) {

		// THE ONE THE EXECUTOR RESOLVED, envelope already off. Falling back to the wire name only
		// for a call that never got that far — an unknown tool, a bad envelope — because those
		// still belong in the record.
		const ibString tool = toolName.IsEmpty()
			? parsed.m_params.GetValue<ibString>(ibT("name")) : toolName;

		// IN THE READER'S WORDS, NOT THE PROTOCOL'S. The person reading either the window or the
		// journal is a developer looking at their own application; "creating catalog Goods" is
		// what happened, `metadata_create` is only how it was spelled on the wire. The tool is
		// asked, because it is the only thing that knows what its own arguments mean.
		//
		// Asked ONCE, for both readers — a second copy of this would be a second chance to drift.
		ibString activity = tool;
		ibString detail;

		if (const ibMcpTool* called = ibFindMcpTool(tool)) {

			// 🛑 DESCRIBING WHAT HAPPENED MUST NOT UNDO IT. These two build a LINE FOR A WINDOW —
			// decoration over work that has already run — and they were called outside the try
			// above, so an exception in either escaped the handler entirely: httplib answered
			// HTTP 500 with an empty body, and the caller had no way to learn that its change had
			// LANDED. Found live on 2026-08-31: `metadata_set` with a boolean value set the
			// property, then GetActivity read that same argument as a string
			// (`params.GetValue<wxString>`), ibDataValue::Expect raised on the kind mismatch, and
			// the answer died on the way out. The application was modified; the caller was told
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
			catch (const ibCoreException&) { activity = tool; detail.clear(); }
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
		if (toolOk && tool != ibT("chat_take") && tool != ibT("chat_say"))
			Publish(ibT("did"), activity, detail);

		// ⭐ AND INTO THE REGISTRATION JOURNAL — every tool call, kept.
		//
		// The window above is a LIVE view: it shows what is happening to whoever is watching, and
		// it is gone when the session ends. The journal is the RECORD — read later, by an
		// administrator who was not there, with the same reader (`journal_read`, `ibLoggerReader`)
		// that shows every other thing the platform did. An assistant that can change a
		// application has to leave the same trail a person does; anything less makes "who did
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
		ibLogger* const journal = ibApplicationInstance::GetLogger(m_applicationInstance);
		if (toolRan && journal != nullptr) {

			if (!toolFruitless) {

				ibString line = activity;

				if (!toolOk) {
					line << ibT(" - ") << _("refused");
					if (!toolKind.IsEmpty())
						line << ibT(" (") << toolKind << ibT(")");
					if (!toolRefusal.IsEmpty())
						line << ibT(": ") << toolRefusal;
				}

				journal->Audit(ibT("assistant"), toolOk ? ibT("mcp.did") : ibT("mcp.refused"), line);
			}
		}
	}

	return answer;
}
