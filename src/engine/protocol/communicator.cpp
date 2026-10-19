#include "communicator.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iterator>
#include <set>
#include <thread>

#include <nlohmann/json.hpp>

#include <wx/scopeguard.h>   // wxMakeGuard — a call's line, however it returns
#include <wx/time.h>

#include "core/diagnostics/journal.h"

namespace {

using json = nlohmann::json;

// What an answer carries beside the frame — its number and how it was made, the login's own, and the events; none of
// it is the frame (frmserver/client/clientHost.cpp: Call, Run, DrawFrame).
bool IsAnswerWord(const std::string& name)
{
	return name == ibProtocol::ibFrame::Frame || name == ibProtocol::ibLogin::Since || name == ibProtocol::ibFrame::Patch
		|| name == ibProtocol::ibFrame::Messages || name == ibProtocol::ibFrame::Clear
		|| name == ibProtocol::ibLogin::Client || name == ibProtocol::ibLogin::Protocol || name == ibProtocol::ibLogin::Features
		|| name == ibProtocol::ibLogin::Token;
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
			ibJournalWarning(wxT("protocol"), wxT("every exchange delayed by %d ms (OES_PROTOCOL_DELAY)"), ms);
		return ms;
	}();
	return delay;
#endif
}

// Password and Token are bearer secrets. The debug journal records the exchange; the release build's macro is empty.
std::string Redacted(const std::string& text)
{
	json node = json::parse(text, nullptr, false);
	if (!node.is_object())
		return text;
	const auto hide = [](json& object) {
		if (!object.is_object())
			return;
		for (const char* name : { ibProtocol::ibLogin::Password, ibProtocol::ibLogin::Token }) {
			const auto found = object.find(name);
			if (found != object.end() && found->is_string())
				*found = "***";
		}
	};
	hide(node);
	const auto params = node.find(ibProtocol::ibRpc::Params);
	if (params != node.end())
		hide(*params);
	const auto result = node.find(ibProtocol::ibRpc::Result);
	if (result != node.end())
		hide(*result);
	return node.dump();
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

bool ibCommunicator::Login(const wxString& user, const wxString& password, ibProtocol::ibLogin::ibMode mode, ibProtocolNode& result,
	ibProtocol::ibRefusal& refusal, wxString& error, const std::vector<ibProtocol::ibSchema::ibKind>& schemas)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	Forget();

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::User, user)
		.SetValue(ibProtocol::ibLogin::Password, password)
		.SetValue(ibProtocol::ibLogin::Mode, static_cast<int>(mode))
		.SetValue(ibProtocol::ibLogin::Protocol, ibProtocol::Version);
	// Opt into the slot. A server that does not offer `resume` ignores it.
	params.AddItem(ibProtocol::ibLogin::Features, wxString::FromUTF8(ibProtocol::ibLogin::FeatureResume));
	for (const ibProtocol::ibSchema::ibKind schema : schemas)
		params.AddItem(ibProtocol::ibLogin::Read).SetValue(ibProtocol::ibLogin::Schema, static_cast<long long>(schema));
	if (!CallLocked(ibProtocol::ibMethod::Login, params, result, refusal, error))
		return false;

	// The client by its id, and the version both speak — the older of the two; a server too old to say one speaks 1.
	m_client = result.GetString(ibProtocol::ibLogin::Client);
	m_protocol = std::max(1, static_cast<int>(result.GetInt(ibProtocol::ibLogin::Protocol, 1)));
	m_mode = mode;
	m_token = result.GetString(ibProtocol::ibLogin::Token);
	m_resume = false;
	for (const ibProtocolNode& feature : result.GetList(ibProtocol::ibLogin::Features)) {
		if (feature.AsString() == wxString::FromUTF8(ibProtocol::ibLogin::FeatureResume))
			m_resume = true;
	}
	Take(result);
	return true;
}

bool ibCommunicator::Users(std::vector<wxString>& names, ibProtocol::ibRefusal& refusal, wxString& error)
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
	wxString error;
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
	const wxString client = GetClient();
	m_connection->Listen([client, changed = std::move(changed)](const std::string& text) {
		ibProtocolNode node;
		if (!ibProtocolNode::Read(text, node) || node.GetString(ibProtocol::ibRpc::Method) != ibProtocol::ibRpc::Changed)
			return;
		if (node.FindChild(ibProtocol::ibRpc::Params).GetString(ibProtocol::ibLogin::Client) != client)
			return;
		changed();
	});
}

bool ibCommunicator::Call(ibProtocol::ibMethod method, const ibProtocolNode& params, ibProtocolNode& result,
	ibProtocol::ibRefusal& refusal, wxString& error)
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
	ibProtocol::ibRefusal& refusal, wxString& error)
{
	refusal = ibProtocol::ibRefusal::None;
	error.clear();
	result = ibProtocolNode();

	// A LINE PER CALL, the client's side of it: what was asked, how long it took — and of that the exchange, the server's
	// part with the way there and back (the rest is the client's: the request written, the answer read) — how it ended,
	// however it returns.
	const wxLongLong began = wxGetUTCTimeMillis();
	wxLongLong exchanged = 0;
	const wxScopeGuard said = wxMakeGuard([&]() {
		if (refusal == ibProtocol::ibRefusal::None)
			ibJournalInfo(wxT("call"), wxT("%s: %lld ms, the exchange %lld ms"), ibProtocol::MethodName(method),
				(wxGetUTCTimeMillis() - began).GetValue(), exchanged.GetValue());
		else
			ibJournalWarning(wxT("call"), wxT("%s refused (%d) after %lld ms: %s"), ibProtocol::MethodName(method),
				static_cast<int>(refusal), (wxGetUTCTimeMillis() - began).GetValue(), error);
	});

	if (!m_connection) {
		refusal = ibProtocol::ibRefusal::Failed;
		error = wxT("no connection");
		return false;
	}

	// ⭐ NO CLIENT, NO CALL — logged out (the window closing, its tree shown again as the tabs go) or not logged in yet:
	// refused here as the server would refuse it, without the exchange.
	const bool beforeLogin = method == ibProtocol::ibMethod::Login || method == ibProtocol::ibMethod::Users;
	if (!beforeLogin && m_client.IsEmpty()) {
		refusal = ibProtocol::ibRefusal::NoSession;
		error = wxT("not logged in");
		return false;
	}

	// The client, and the frame it holds — a frame answer is then the patch from it. A field a method does not
	// read is skipped by the server.
	ibProtocolNode named = params.IsNode() ? params.Clone() : ibProtocolNode();
	if (!beforeLogin) {
		named.SetValue(ibProtocol::ibLogin::Client, m_client);
		if (m_number != 0)
			named.SetValue(ibProtocol::ibLogin::Since, m_number);
	}
	// ⭐ WHAT WAS HELD GOES WITH ANY CALL — the server applies it before the call itself, so the code a press runs reads the
	// row the cursor stands on. Taken once: a call that does not get through loses it with the session it was of. A
	// client going says nothing more of where it was.
	if (!beforeLogin && method != ibProtocol::ibMethod::Logout) {
		std::vector<ibProtocolNode> pending;
		{
			std::lock_guard<std::mutex> lock(m_pendingMutex);
			pending.swap(m_pending);
		}
		for (const ibProtocolNode& event : pending)
			(*named.Json())[ibProtocol::ibLogin::Pending].push_back(*event.Json());
	}
	const long long id = ++m_lastId;
	ibProtocolNode request;
	request.SetValue(ibProtocol::ibRpc::Version, "2.0")
		.SetValue(ibProtocol::ibRpc::Id, id)
		.SetValue(ibProtocol::ibRpc::Method, ibProtocol::MethodName(method))
		.SetValue(ibProtocol::ibRpc::Params, named);

	// THE WHOLE EXCHANGE, beside the call's own line — what went, and what came back with how long it took: the debug
	// build's journal (the macro is nothing in a release one).
	const std::string sent = request.Write();
	ibJournalInfo(wxT("protocol"), wxT("%s sent: %s"), ibProtocol::MethodName(method), wxString::FromUTF8(Redacted(sent)));

	std::string answer;
	const wxLongLong exchanging = wxGetUTCTimeMillis();
	if (ExchangeDelay() > 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(ExchangeDelay()));
	if (!m_connection->Exchange(sent, answer, refusal, error) && !Resume(sent, answer, refusal, error))
		return false;
	exchanged = wxGetUTCTimeMillis() - exchanging;

	ibJournalInfo(wxT("protocol"), wxT("%s answered in %lld ms, %zu bytes: %s"), ibProtocol::MethodName(method),
		(wxGetUTCTimeMillis() - began).GetValue(), answer.size(), wxString::FromUTF8(Redacted(answer)));

	ibProtocolNode root;
	if (!ibProtocolNode::Read(answer, root) || !root.IsNode()) {
		refusal = ibProtocol::ibRefusal::Failed;
		error = wxT("the server's answer could not be read");
		return false;
	}
	// The answer to THIS call — one is in flight at a time. An envelope with no id is the server refusing a request it
	// could not read: still this one's.
	if (root.GetInt(ibProtocol::ibRpc::Id, id) != id) {
		refusal = ibProtocol::ibRefusal::Failed;
		error = wxT("the server answered another call");
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

bool ibCommunicator::Resume(const std::string& sent, std::string& answer, ibProtocol::ibRefusal& refusal, wxString& error)
{
	// A transport failure of a session the server offered to keep. One try. The unanswered request keeps its id;
	// the login that precedes it takes a newer one, so the counter never goes backwards and never back to 0.
	if (!m_resume || m_token.IsEmpty() || refusal != ibProtocol::ibRefusal::NoSession || !m_connection)
		return false;

	wxString reopen;
	if (!m_connection->Reconnect(reopen)) {
		error = reopen.IsEmpty() ? error : reopen;
		return false;
	}

	const long long loginId = ++m_lastId;
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::Token, m_token)
		.SetValue(ibProtocol::ibLogin::Mode, static_cast<int>(m_mode))
		.SetValue(ibProtocol::ibLogin::Protocol, ibProtocol::Version);
	ibProtocolNode request;
	request.SetValue(ibProtocol::ibRpc::Version, "2.0")
		.SetValue(ibProtocol::ibRpc::Id, loginId)
		.SetValue(ibProtocol::ibRpc::Method, ibProtocol::MethodName(ibProtocol::ibMethod::Login))
		.SetValue(ibProtocol::ibRpc::Params, params);
	const std::string loginSent = request.Write();
	ibJournalInfo(wxT("protocol"), wxT("login sent: %s"), wxString::FromUTF8(Redacted(loginSent)));

	std::string loginAnswer;
	ibProtocol::ibRefusal loginRefusal = ibProtocol::ibRefusal::None;
	wxString loginError;
	if (!m_connection->Exchange(loginSent, loginAnswer, loginRefusal, loginError)) {
		refusal = loginRefusal;
		error = loginError;
		return false;
	}
	ibProtocolNode root;
	if (!ibProtocolNode::Read(loginAnswer, root) || !root.IsNode()) {
		refusal = ibProtocol::ibRefusal::Failed;
		error = wxT("the server's answer could not be read");
		return false;
	}
	if (root.GetInt(ibProtocol::ibRpc::Id, loginId) != loginId) {
		refusal = ibProtocol::ibRefusal::Failed;
		error = wxT("the server answered another call");
		return false;
	}
	const ibProtocolNode failed = root.FindChild(ibProtocol::ibRpc::Error);
	if (failed.IsNode()) {
		// 401 is the session gone — the window closes as it does for any other lost session.
		refusal = RefusalOf(failed.GetInt(ibProtocol::ibRpc::Code));
		error = failed.GetString(ibProtocol::ibRpc::Message);
		return false;
	}
	const ibProtocolNode done = root.FindChild(ibProtocol::ibRpc::Result);
	if (done.GetString(ibProtocol::ibLogin::Client) != m_client) {
		// A new Client is another server's session (stage 2). This return is the same process, the same id.
		refusal = ibProtocol::ibRefusal::NoSession;
		error = wxT("the session was not resumed");
		return false;
	}

	ibJournalInfo(wxT("protocol"), wxT("sent again: %s"), wxString::FromUTF8(Redacted(sent)));
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
		if (m_protocol >= 2 && active != was) {
			json& frame = *m_frame.Json();
			const auto own = m_views.find(active);
			const auto held = frame.find(ibProtocol::ibFrame::View);
			if (held != frame.end()) {
				json leaving = std::move(*held);
				if (own != m_views.end()) {
					*held = std::move(*own->second.Json());
					m_views.erase(own);
				}
				else {
					*held = leaving;
				}
				m_views[was] = ibProtocolNode(std::move(leaving));
			}
			else if (own != m_views.end()) {
				frame[ibProtocol::ibFrame::View] = std::move(*own->second.Json());
				m_views.erase(own);
			}
		}
		m_frame.Apply(patch);
	}
	else {
		// Whole: the answer less what is not the frame — and the views kept begin again with it.
		json frame = json::object();
		const json& whole = *answer.Json();
		for (auto entry = whole.begin(); entry != whole.end(); ++entry) {
			if (!IsAnswerWord(entry.key()))
				frame[entry.key()] = *entry;
		}
		m_frame = ibProtocolNode(std::move(frame));
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
