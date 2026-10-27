#include "communicator.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iterator>
#include <set>
#include <thread>

#include <wx/scopeguard.h>   // wxMakeGuard — a call's line, however it returns

#include "core/diagnostics/journal.h"

namespace {

// The frame's own words — what the server draws its frame's state in (frmserver/client/clientHost.cpp: DrawFrame, its
// `state`). Everything else an answer carries is not the frame: its number and how it was made, the login's own, the
// events (Messages, Clear, Exit, Properties — DrawFrame's `events`) and the method's own result beside it.
bool IsFrameWord(const ibString& name)
{
	return name == ibProtocol::ibFrame::Title || name == ibProtocol::ibFrame::Status || name == ibProtocol::ibFrame::Endpoint
		|| name == ibProtocol::ibFrame::Language || name == ibProtocol::ibFrame::ActiveTab
		|| name == ibProtocol::ibFrame::Schemas || name == ibProtocol::ibFrame::Templates || name == ibProtocol::ibFrame::Menu
		|| name == ibProtocol::ibFrame::MetaData || name == ibProtocol::ibFrame::View || name == ibProtocol::ibFrame::Tabs
		|| name == ibProtocol::ibFrame::Request;
}

// The envelope's own refusals — JSON-RPC's numbers, said by the port for a message that is not JSON-RPC or a method
// the server does not have — as the protocol says them: a refusal arrives the same by every transport, and a method
// the server does not have is NotFound, as a call in process is told.
ibProtocol::ibRefusal RefusalOf(long long code)
{
	if (code > 0)
		return static_cast<ibProtocol::ibRefusal>(code);
	if (code == -32601)   // method not found
		return ibProtocol::ibRefusal::NotFound;
	if (code == -32603)   // internal error
		return ibProtocol::ibRefusal::Failed;
	return ibProtocol::ibRefusal::BadParameter;
}

// AN ANSWER READ — the result of the call of that id, or its refusal: one call is in flight on a connection at a time,
// and an envelope with no id is the server refusing a request it could not read — still that one's. The result is the
// answer's own bytes, read where they lie.
bool ReadAnswer(const ibProtocolNode& root, long long id, ibProtocolNode& result, ibProtocol::ibRefusal& refusal,
	ibString& error)
{
	if (root.GetInt(ibProtocol::ibRpc::Id, id) != id) {
		refusal = ibProtocol::ibRefusal::Failed;
		error = ibT("the server answered another call");
		return false;
	}
	const ibProtocolNode failed = root.FindChild(ibProtocol::ibRpc::Error);
	if (failed.IsNode()) {
		refusal = RefusalOf(failed.GetInt(ibProtocol::ibRpc::Code));
		error = failed.GetString(ibProtocol::ibRpc::Message);
		return false;
	}
	const ibProtocolNode done = root.FindChild(ibProtocol::ibRpc::Result);
	if (!done.IsEmpty())
		result = done;
	return true;
}

// Two held events of one kind — the later stands for both: the same event of the same control of the same view; a
// column's place and width are each the column's own.
bool IsSameState(const ibProtocolNode& one, const ibProtocolNode& other)
{
	if (one.GetInt(ibProtocol::ibEvent::Control) != other.GetInt(ibProtocol::ibEvent::Control)
		|| one.GetInt(ibProtocol::ibEvent::Event) != other.GetInt(ibProtocol::ibEvent::Event)
		|| one.GetString(ibProtocol::ibEvent::Form) != other.GetString(ibProtocol::ibEvent::Form)
		|| one.GetInt(ibProtocol::ibEvent::Tab) != other.GetInt(ibProtocol::ibEvent::Tab))
		return false;
	const ibProtocol::ibEvent::ibKind event = static_cast<ibProtocol::ibEvent::ibKind>(one.GetInt(ibProtocol::ibEvent::Event));
	// …a tab's Change hands back one of its parts — a form designer's tab its form and its module's text, each held
	// apart: the one does not stand in for the other.
	if (event == ibProtocol::ibEvent::ibKind::Change)
		return one.FindChild(ibProtocol::ibEvent::Args).Has(ibProtocol::ibEvent::Form)
			== other.FindChild(ibProtocol::ibEvent::Args).Has(ibProtocol::ibEvent::Form);
	if (event != ibProtocol::ibEvent::ibKind::Move && event != ibProtocol::ibEvent::ibKind::Resize)
		return true;
	return one.FindChild(ibProtocol::ibEvent::Args).GetInt(ibProtocol::ibEvent::Column)
		== other.FindChild(ibProtocol::ibEvent::Args).GetInt(ibProtocol::ibEvent::Column);
}

// A NETWORK BETWEEN, put on by hand — OES_PROTOCOL_DELAY, in milliseconds, slept once per exchange as the way there and
// back: what a client far from its server waits, seen with the base beside it (a designer's, a thin client's — how many
// exchanges an action costs). Unset — none; a debug build's only, said once in its journal.
int ExchangeDelay()
{
#ifdef NDEBUG
	return 0;
#else
	static const int delay = []() {
		const char* const value = std::getenv("OES_PROTOCOL_DELAY");
		const int ms = value != nullptr ? std::atoi(value) : 0;
		if (ms > 0)
			ibJournalWarning(ibT("protocol"), ibT("every exchange delayed by %d ms (OES_PROTOCOL_DELAY)"), ms);
		return ms;
	}();
	return delay;
#endif
}

// A REQUEST — its id, its method's name and its params, as the wire carries every call: a call's (CallLocked), the
// debugger's (DebugLocked) and the login that resumes a session (Resume).
ibProtocolNode Request(long long id, ibProtocol::ibMethod method, const ibProtocolNode& params)
{
	ibProtocolNode request;
	request.SetValue(ibProtocol::ibRpc::Id, id)
		.SetValue(ibProtocol::ibRpc::Method, ibProtocol::MethodName(method))
		.SetValue(ibProtocol::ibRpc::Params, params.IsNode() ? params : ibProtocolNode());
	return request;
}

// WHAT THE DEBUG JOURNAL DOES NOT WRITE AS IT IS, at any depth of a request. The bearer secrets — Password, Token and
// the debugger's Channel key: a login's at the top of its params, a setting's under its Args (the assistant access's
// key went into the journal whole, 2026-10-26). And bytes — a file's part, a picture: their length, not their base64
// (an upload of 55 parts was 341 KB of the journal, 2026-10-26).
const char* const kSecrets[] = { ibProtocol::ibLogin::Password, ibProtocol::ibLogin::Token, ibProtocol::ibDebug::Channel };

bool NeedsRedacting(const ibProtocolNode& node)
{
	for (const char* secret : kSecrets) {
		if (node.Has(secret))
			return true;
	}
	for (const auto& [name, entry] : node.Entries()) {
		if (entry.IsBinary())
			return true;
		if (entry.IsNode() && NeedsRedacting(entry))
			return true;
		if (entry.IsList()) {
			for (const ibProtocolNode& item : node.GetList(name)) {
				if (NeedsRedacting(item))
					return true;
			}
		}
	}
	return false;
}

// …each written where it lies — a secret as `***`, bytes as their length; `node` a handle into the copy being written.
// The node's own bytes go last, its parts walked as they were read.
void Redact(ibProtocolNode& node)
{
	for (const char* secret : kSecrets) {
		if (node.Has(secret))
			node.SetValue(secret, ibString(ibT("***")));
	}
	std::vector<std::pair<ibString, std::size_t>> bytes;
	for (auto& [name, entry] : node.Entries()) {
		if (entry.IsBinary())
			bytes.emplace_back(name, entry.AsBinary().GetDataLen());
		else if (entry.IsNode())
			Redact(entry);
		else if (entry.IsList()) {
			for (ibProtocolNode& item : node.GetList(name))
				Redact(item);
		}
	}
	for (const auto& [name, length] : bytes)
		node.SetValue(name.utf8_str().c_str(), ibString::Format(ibT("<%llu bytes>"), static_cast<unsigned long long>(length)));
}

// A REQUEST AS THE DEBUG JOURNAL WRITES IT — its JSON view, redacted. The release build's macro is empty, and nothing of
// this is made. An answer is not written out: a frame rendered for the journal cost more than reading it for the client
// (a Debug profile, 2026-10-20: 535 ms against 387) — its length is.
ibString Redacted(const ibProtocolNode& request)
{
	if (!NeedsRedacting(request.FindChild(ibProtocol::ibRpc::Params)))
		return request.Write();

	ibProtocolNode redacted = request.Clone();
	ibProtocolNode params = redacted.Child(ibProtocol::ibRpc::Params);
	Redact(params);
	return redacted.Write();
}

} // namespace

ibCommunicator::ibCommunicator(std::unique_ptr<ibProtocolConnection> connection)
	: m_connection(std::move(connection))
{
}

ibCommunicator::~ibCommunicator()
{
	Logout();
}

bool ibCommunicator::Login(const ibString& user, const ibString& password, ibProtocol::ibLogin::ibMode mode, ibProtocolNode& result,
	ibProtocol::ibRefusal& refusal, ibString& error, const std::vector<ibProtocol::ibSchema::ibKind>& schemas)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	Forget();

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::User, user)
		.SetValue(ibProtocol::ibLogin::Password, password)
		.SetValue(ibProtocol::ibLogin::Mode, static_cast<int>(mode))
		.SetValue(ibProtocol::ibLogin::Protocol, ibProtocol::Version);
	// Opt into the slot. A server that does not offer `resume` ignores it.
	params.AddItem(ibProtocol::ibLogin::Features, ibProtocol::ibLogin::FeatureResume);
	for (const ibProtocol::ibSchema::ibKind schema : schemas)
		params.AddItem(ibProtocol::ibLogin::Read).SetValue(ibProtocol::ibLogin::Schema, static_cast<long long>(schema));
	if (!CallLocked(ibProtocol::ibMethod::Login, params, result, refusal, error))
		return false;

	// The client by its id, and the version both speak — the older of the two; a server too old to say one speaks 1.
	m_client = result.GetString(ibProtocol::ibLogin::Client);
	m_protocol = std::max(1, static_cast<int>(result.GetInt(ibProtocol::ibLogin::Protocol, 1)));
	m_mode = mode;
	m_token = result.GetString(ibProtocol::ibLogin::Token);
	// The client's own words in the base's language, as the server names it (core/localization/dictionary.h). A
	// server that names none leaves them as they are.
	const ibString locale = result.GetString(ibProtocol::ibLogin::Locale);
	if (!locale.IsEmpty())
		ibDictionary::SetProcessLanguage(locale);
	m_resume = false;
	for (const ibProtocolNode& feature : result.GetList(ibProtocol::ibLogin::Features)) {
		if (feature.AsString() == ibProtocol::ibLogin::FeatureResume)
			m_resume = true;
	}
	Take(result);
	return true;
}

bool ibCommunicator::Users(std::vector<ibString>& names, ibProtocol::ibRefusal& refusal, ibString& error)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	ibProtocolNode result;
	if (!CallLocked(ibProtocol::ibMethod::Users, ibProtocolNode(), result, refusal, error))
		return false;

	for (const ibProtocolNode& name : result.GetList(ibProtocol::ibLogin::Users))
		names.push_back(name.AsString());
	return true;
}

void ibCommunicator::Logout()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_client.IsEmpty())
		return;

	// Told nothing more of a client that is going.
	m_connection->Listen(nullptr);

	ibProtocolNode result;
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	CallLocked(ibProtocol::ibMethod::Logout, ibProtocolNode(), result, refusal, error);
	Forget();
}

void ibCommunicator::Listen(std::function<void()> changed)
{
	if (!changed) {
		m_connection->Listen(nullptr);
		return;
	}

	// Of this client, as logged in now — a connection may carry more than one, and each is told of its own.
	const ibString client = GetClient();
	m_connection->Listen([client, changed = std::move(changed)](const ibProtocolNode& said) {
		if (said.GetString(ibProtocol::ibRpc::Method) != ibProtocol::ibRpc::Changed)
			return;
		if (said.FindChild(ibProtocol::ibRpc::Params).GetString(ibProtocol::ibLogin::Client) != client)
			return;
		changed();
	});
}

bool ibCommunicator::ListenDebug(std::function<void(const ibProtocolNode& news)> told, ibProtocol::ibRefusal& refusal,
	ibString& error)
{
	refusal = ibProtocol::ibRefusal::None;
	error.clear();
	if (!told) {
		std::lock_guard<std::mutex> lock(m_debugMutex);
		if (m_debugConnection)
			m_debugConnection->Listen(nullptr);
		m_debugConnection.reset();
		return true;
	}
	if (m_protocol < 6) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = ibT("the server speaks no protocol with the debugger in it");
		return false;
	}

	// The second connection, and the key that binds it — asked on this one, outside the debugger's lock (the order the
	// two locks are taken in is this one's first).
	std::unique_ptr<ibProtocolConnection> channel = m_connection ? m_connection->OpenChannel(error) : nullptr;
	if (!channel) {
		refusal = ibProtocol::ibRefusal::Failed;
		return false;
	}
	ibProtocolNode params, key;
	params.SetValue(ibProtocol::ibDebug::Command, static_cast<int>(ibProtocol::ibDebug::ibCommand::Channel));
	if (!Call(ibProtocol::ibMethod::Debug, params, key, refusal, error))
		return false;

	// Told before it is bound, so news the moment it is bound is not lost — of this client only.
	const ibString client = GetClient();
	channel->Listen([client, told = std::move(told)](const ibProtocolNode& said) {
		if (said.GetString(ibProtocol::ibRpc::Method) != ibProtocol::ibRpc::Debug)
			return;
		const ibProtocolNode news = said.FindChild(ibProtocol::ibRpc::Params);
		if (news.GetString(ibProtocol::ibLogin::Client) != client)
			return;
		told(news);
	});

	std::lock_guard<std::mutex> lock(m_debugMutex);
	if (m_debugConnection)
		m_debugConnection->Listen(nullptr);
	m_debugConnection = std::move(channel);
	m_debugClient = client;

	ibProtocolNode listen, bound;
	listen.SetValue(ibProtocol::ibDebug::Command, static_cast<int>(ibProtocol::ibDebug::ibCommand::Listen))
		.SetValue(ibProtocol::ibDebug::Channel, key.GetString(ibProtocol::ibDebug::Channel));
	if (DebugLocked(listen, bound, refusal, error))
		return true;
	m_debugConnection->Listen(nullptr);
	m_debugConnection.reset();
	return false;
}

bool ibCommunicator::Debug(const ibProtocolNode& params, ibProtocolNode& result, ibProtocol::ibRefusal& refusal,
	ibString& error)
{
	std::lock_guard<std::mutex> lock(m_debugMutex);
	return DebugLocked(params, result, refusal, error);
}

bool ibCommunicator::DebugLocked(const ibProtocolNode& params, ibProtocolNode& result, ibProtocol::ibRefusal& refusal,
	ibString& error)
{
	refusal = ibProtocol::ibRefusal::None;
	error.clear();
	result = ibProtocolNode();
	if (!m_debugConnection) {
		refusal = ibProtocol::ibRefusal::NoSession;
		error = ibT("no debugger is listening");
		return false;
	}

	// The client named, as every call names it; nothing held goes with it, and no frame is asked for.
	const long long id = ++m_debugLastId;
	ibProtocolNode request = Request(id, ibProtocol::ibMethod::Debug, params);
	request.Child(ibProtocol::ibRpc::Params).SetValue(ibProtocol::ibLogin::Client, m_debugClient);

	ibMemoryBuffer sent;
	request.Write(sent);
	ibProtocolNode answer;
	if (!m_debugConnection->Exchange(sent, answer, refusal, error))
		return false;
	return ReadAnswer(answer, id, result, refusal, error);
}

bool ibCommunicator::Call(ibProtocol::ibMethod method, const ibProtocolNode& params, ibProtocolNode& result,
	ibProtocol::ibRefusal& refusal, ibString& error)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!CallLocked(method, params, result, refusal, error)) {
		// Its session is gone — the server says so for any call but login: nothing of it is worth keeping.
		if (refusal == ibProtocol::ibRefusal::NoSession)
			Forget();
		return false;
	}
	Take(result);
	return true;
}

bool ibCommunicator::Hold(const ibProtocolNode& event)
{
	// A server of protocol 2 or older would never hear it — and one not logged in to has nobody to tell.
	if (m_protocol < 3)
		return false;

	// The latest of its kind in place of the one before — and at the end: it was done last.
	ibProtocolNode held = event.Clone();
	std::lock_guard<std::mutex> lock(m_pendingMutex);
	m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(),
		[&held](const ibProtocolNode& pending) { return IsSameState(pending, held); }), m_pending.end());
	m_pending.push_back(std::move(held));
	return true;
}

bool ibCommunicator::CallLocked(ibProtocol::ibMethod method, const ibProtocolNode& params, ibProtocolNode& result,
	ibProtocol::ibRefusal& refusal, ibString& error)
{
	refusal = ibProtocol::ibRefusal::None;
	error.clear();
	result = ibProtocolNode();

	// A LINE PER CALL, the client's side of it: what was asked, how long it took — and of that the exchange, the server's
	// part with the way there and back (the rest is the client's: the request written, the answer read) — how it ended,
	// however it returns.
	const auto msSince = [](std::chrono::steady_clock::time_point since) {
		return static_cast<long long>(
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - since).count());
	};
	const auto began = std::chrono::steady_clock::now();
	long long exchanged = 0;
	const wxScopeGuard said = wxMakeGuard([&]() {
		if (refusal == ibProtocol::ibRefusal::None)
			ibJournalInfo(ibT("call"), ibT("%s: %lld ms, the exchange %lld ms"), ibProtocol::MethodName(method),
				msSince(began), exchanged);
		else
			ibJournalWarning(ibT("call"), ibT("%s refused (%d) after %lld ms: %s"), ibProtocol::MethodName(method),
				static_cast<int>(refusal), msSince(began), error);
	});

	if (!m_connection) {
		refusal = ibProtocol::ibRefusal::Failed;
		error = ibT("no connection");
		return false;
	}

	// ⭐ NO CLIENT, NO CALL — logged out (the window closing, its tree shown again as the tabs go) or not logged in yet:
	// refused here as the server would refuse it, without the exchange.
	const bool beforeLogin = method == ibProtocol::ibMethod::Login || method == ibProtocol::ibMethod::Users;
	if (!beforeLogin && m_client.IsEmpty()) {
		refusal = ibProtocol::ibRefusal::NoSession;
		error = ibT("not logged in");
		return false;
	}

	// The params copied once, into the request — and the client and the frame it holds named in them: a frame answer is
	// then the patch from it. A field a method does not read is skipped by the server.
	const long long id = ++m_lastId;
	ibProtocolNode request = Request(id, method, params);
	ibProtocolNode named = request.Child(ibProtocol::ibRpc::Params);
	if (!beforeLogin) {
		named.SetValue(ibProtocol::ibLogin::Client, m_client);
		if (m_number != 0)
			named.SetValue(ibProtocol::ibLogin::Since, m_number);
	}
	// ⭐ WHAT WAS HELD GOES WITH ANY CALL — the server applies it before the call itself, so the code a press runs reads the
	// row the cursor stands on. Taken once: a call that does not get through loses it with the session it was of. A
	// client going says nothing more of where it was, and the debugger's call is no work of the client's (the server
	// would drop it). Each held event's bytes go in at the list's end as they are (Hold made them its own).
	if (!beforeLogin && method != ibProtocol::ibMethod::Logout && method != ibProtocol::ibMethod::Debug) {
		std::vector<ibProtocolNode> pending;
		{
			std::lock_guard<std::mutex> lock(m_pendingMutex);
			pending.swap(m_pending);
		}
		for (const ibProtocolNode& event : pending) {
			if (event.IsNode())
				named.Writable()->AddItem(ibProtocol::ibLogin::Pending) = event.Held();
		}
	}

	// THE WHOLE EXCHANGE, beside the call's own line — what went, and how long what came back is and took: the debug
	// build's journal (the macro is nothing in a release one).
	ibJournalInfo(ibT("protocol"), ibT("%s sent: %s"), ibProtocol::MethodName(method), Redacted(request));

	ibMemoryBuffer sent;
	request.Write(sent);
	ibProtocolNode answer;
	const auto exchanging = std::chrono::steady_clock::now();
	if (ExchangeDelay() > 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(ExchangeDelay()));
	if (!m_connection->Exchange(sent, answer, refusal, error) && !Resume(sent, answer, refusal, error))
		return false;
	exchanged = msSince(exchanging);

	ibJournalInfo(ibT("protocol"), ibT("%s answered in %lld ms, %zu bytes"), ibProtocol::MethodName(method),
		msSince(began), answer.Held().Weight());

	return ReadAnswer(answer, id, result, refusal, error);
}

bool ibCommunicator::Resume(const ibMemoryBuffer& sent, ibProtocolNode& answer, ibProtocol::ibRefusal& refusal,
	ibString& error)
{
	// A transport failure of a session the server offered to keep. One try. The unanswered request keeps its id;
	// the login that precedes it takes a newer one, so the counter never goes backwards and never back to 0.
	if (!m_resume || m_token.IsEmpty() || refusal != ibProtocol::ibRefusal::NoSession || !m_connection)
		return false;

	ibString reopen;
	if (!m_connection->Reconnect(reopen)) {
		error = reopen.IsEmpty() ? error : reopen;
		return false;
	}

	const long long loginId = ++m_lastId;
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::Token, m_token)
		.SetValue(ibProtocol::ibLogin::Mode, static_cast<int>(m_mode))
		.SetValue(ibProtocol::ibLogin::Protocol, ibProtocol::Version);
	const ibProtocolNode request = Request(loginId, ibProtocol::ibMethod::Login, params);
	ibJournalInfo(ibT("protocol"), ibT("login sent: %s"), Redacted(request));
	ibMemoryBuffer loginSent;
	request.Write(loginSent);

	ibProtocolNode loginAnswer;
	ibProtocol::ibRefusal loginRefusal = ibProtocol::ibRefusal::None;
	ibString loginError;
	if (!m_connection->Exchange(loginSent, loginAnswer, loginRefusal, loginError)) {
		refusal = loginRefusal;
		error = loginError;
		return false;
	}
	// A refusal — 401 is the session gone — closes the window as any other lost session does.
	ibProtocolNode done;
	if (!ReadAnswer(loginAnswer, loginId, done, refusal, error))
		return false;
	if (done.GetString(ibProtocol::ibLogin::Client) != m_client) {
		// A new Client is another server's session (stage 2). This return is the same process, the same id.
		refusal = ibProtocol::ibRefusal::NoSession;
		error = ibT("the session was not resumed");
		return false;
	}

	ibJournalInfo(ibT("protocol"), ibT("sent again: %zu bytes"), sent.GetDataLen());
	if (!m_connection->Exchange(sent, answer, refusal, error))
		return false;
	return true;
}

void ibCommunicator::Take(const ibProtocolNode& answer)
{
	if (!answer.Has(ibProtocol::ibFrame::Frame))
		return;   // not a frame — a fetch's rows, a schema's tree, a file's part

	const ibProtocolNode patch = answer.FindChild(ibProtocol::ibFrame::Patch);
	if (patch.IsNode()) {
		// (protocol 2) when the patch makes another tab active, the views change hands first: the one held goes to its
		// tab's place, and the new tab's own — when one was kept — comes in; a tab the client was never sent the view
		// of is patched from the view held, as protocol 1 has it.
		const long long was = m_frame.GetInt(ibProtocol::ibFrame::ActiveTab);
		const long long active = patch.GetInt(ibProtocol::ibFrame::ActiveTab, was);
		if (m_protocol >= 2 && active != was && m_frame.IsNode()) {
			const auto own = m_views.find(active);
			const ibProtocolNode leaving = m_frame.FindChild(ibProtocol::ibFrame::View);
			if (leaving.IsNode())
				m_views[was] = leaving.Clone();
			if (own != m_views.end()) {
				m_frame.SetValue(ibProtocol::ibFrame::View, own->second);
				m_views.erase(own);
			}
		}
		m_frame.Apply(patch);
	}
	else {
		// Whole: the frame's own entries copied out of the answer as they lie — what it says beside the frame stays in it
		// for the caller (Draw reads the events there, a door its own result) — and the names of its types. The views
		// kept begin again with it.
		auto frame = std::make_shared<ibProtocolTree>();
		frame->m_made = false;
		for (const auto& field : answer.Held().Fields()) {
			if (IsFrameWord(field.first))
				frame->m_root.AddField(field.first, field.second);
		}
		frame->NameTypes(answer.m_tree->m_types);
		m_frame = ibProtocolNode(std::move(frame), ibProtocolNode::ibShape::Node);
		m_views.clear();
	}
	m_number = answer.GetInt(ibProtocol::ibFrame::Frame);

	// A tab gone takes its view with it; the active tab's is the frame's own.
	if (m_protocol < 2)
		return;
	std::set<long long> open;
	for (const ibProtocolNode& tab : m_frame.FindChild(ibProtocol::ibFrame::Tabs).Children())
		open.insert(tab.GetId());
	const long long active = m_frame.GetInt(ibProtocol::ibFrame::ActiveTab);
	for (auto it = m_views.begin(); it != m_views.end();)
		it = open.count(it->first) != 0 && it->first != active ? std::next(it) : m_views.erase(it);
}

void ibCommunicator::Forget()
{
	// The debugger's connection goes with the client it was bound for.
	{
		std::lock_guard<std::mutex> lock(m_debugMutex);
		if (m_debugConnection)
			m_debugConnection->Listen(nullptr);
		m_debugConnection.reset();
		m_debugClient.clear();
	}
	m_client.clear();
	m_token.clear();
	m_resume = false;
	m_protocol = 1;
	m_number = 0;
	m_frame = ibProtocolNode();
	m_views.clear();
	std::lock_guard<std::mutex> lock(m_pendingMutex);
	m_pending.clear();
}
