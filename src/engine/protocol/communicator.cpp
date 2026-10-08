#include "communicator.h"

#include <algorithm>
#include <iterator>
#include <set>

#include <nlohmann/json.hpp>

#include <wx/scopeguard.h>   // wxMakeGuard — a call's line, however it returns
#include <wx/time.h>

#include "protocol/journal.h"

namespace {

using json = nlohmann::json;

// What an answer carries beside the frame — its number and how it was made, the login's own, and the events; none of
// it is the frame (frmserver/client/clientHost.cpp: Call, Run, DrawFrame).
bool IsAnswerWord(const std::string& name)
{
	return name == ibProtocolName::Frame || name == ibProtocolName::Since || name == ibProtocolName::Patch
		|| name == ibProtocolName::Messages || name == ibProtocolName::Clear || name == ibProtocolName::Pictures
		|| name == ibProtocolName::Client || name == ibProtocolName::Protocol || name == ibProtocolName::Features;
}

// The envelope's own refusals — JSON-RPC's numbers, said by the port for a message that is not JSON-RPC or a method
// the server does not have — as the protocol says them: a refusal arrives the same by every transport, and a method
// the server does not have is NotFound, as a call in process is told.
ibProtocolRefusal RefusalOf(long long code)
{
	if (code > 0)
		return static_cast<ibProtocolRefusal>(code);
	if (code == -32601)   // method not found
		return ibProtocolRefusal::NotFound;
	if (code == -32603)   // internal error
		return ibProtocolRefusal::Failed;
	return ibProtocolRefusal::BadParameter;
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

bool ibCommunicator::Login(const wxString& user, const wxString& password, ibProtocolMode mode, ibProtocolNode& result,
	ibProtocolRefusal& refusal, wxString& error)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	Forget();

	ibProtocolNode params;
	params.SetValue(ibProtocolName::User, user)
		.SetValue(ibProtocolName::Password, password)
		.SetValue(ibProtocolName::Mode, static_cast<int>(mode))
		.SetValue(ibProtocolName::Protocol, ibProtocolVersion);
	if (!CallLocked(ibProtocolMethod::Login, params, result, refusal, error))
		return false;

	// The client by its id, and the version both speak — the older of the two; a server too old to say one speaks 1.
	m_client = result.GetString(ibProtocolName::Client);
	m_protocol = std::max(1, static_cast<int>(result.GetInt(ibProtocolName::Protocol, 1)));
	Take(result);
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
	ibProtocolRefusal refusal = ibProtocolRefusal::None;
	wxString error;
	CallLocked(ibProtocolMethod::Logout, ibProtocolNode(), result, refusal, error);
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
		if (!ibProtocolNode::Read(text, node) || node.GetString(ibProtocolName::RpcMethod) != ibProtocolName::Changed)
			return;
		if (node.FindChild(ibProtocolName::RpcParams).GetString(ibProtocolName::Client) != client)
			return;
		changed();
	});
}

bool ibCommunicator::Call(ibProtocolMethod method, const ibProtocolNode& params, ibProtocolNode& result,
	ibProtocolRefusal& refusal, wxString& error)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!CallLocked(method, params, result, refusal, error)) {
		// Its session is gone — the server says so for any call but login: nothing of it is worth keeping.
		if (refusal == ibProtocolRefusal::NoSession)
			Forget();
		return false;
	}
	Take(result);
	return true;
}

bool ibCommunicator::CallLocked(ibProtocolMethod method, const ibProtocolNode& params, ibProtocolNode& result,
	ibProtocolRefusal& refusal, wxString& error)
{
	refusal = ibProtocolRefusal::None;
	error.clear();
	result = ibProtocolNode();

	// A LINE PER CALL, the client's side of it: what was asked, how long it took — and of that the exchange, the server's
	// part with the way there and back (the rest is the client's: the request written, the answer read) — how it ended,
	// however it returns.
	const wxLongLong began = wxGetUTCTimeMillis();
	wxLongLong exchanged = 0;
	const wxScopeGuard said = wxMakeGuard([&]() {
		if (refusal == ibProtocolRefusal::None)
			ibProtocolJournalInfo(wxT("call"), wxT("%s: %lld ms, the exchange %lld ms"), ibProtocolMethodName(method),
				(wxGetUTCTimeMillis() - began).GetValue(), exchanged.GetValue());
		else
			ibProtocolJournalWarning(wxT("call"), wxT("%s refused (%d) after %lld ms: %s"), ibProtocolMethodName(method),
				static_cast<int>(refusal), (wxGetUTCTimeMillis() - began).GetValue(), error);
	});

	if (!m_connection) {
		refusal = ibProtocolRefusal::Failed;
		error = wxT("no connection");
		return false;
	}

	// The client, and the frame it holds — a frame answer is then the patch from it. A field a method does not
	// read is skipped by the server.
	ibProtocolNode named = params.IsNode() ? params.Clone() : ibProtocolNode();
	if (method != ibProtocolMethod::Login) {
		named.SetValue(ibProtocolName::Client, m_client);
		if (m_number != 0)
			named.SetValue(ibProtocolName::Since, m_number);
	}
	const long long id = ++m_lastId;
	ibProtocolNode request;
	request.SetValue(ibProtocolName::RpcVersion, "2.0")
		.SetValue(ibProtocolName::RpcId, id)
		.SetValue(ibProtocolName::RpcMethod, ibProtocolMethodName(method))
		.SetValue(ibProtocolName::RpcParams, named);

	// THE WHOLE EXCHANGE, beside the call's own line — what went, and what came back with how long it took: the debug
	// build's journal (the macro is nothing in a release one).
	const std::string sent = request.Write();
	ibProtocolJournalInfo(wxT("protocol"), wxT("%s sent: %s"), ibProtocolMethodName(method), wxString::FromUTF8(sent));

	std::string answer;
	const wxLongLong exchanging = wxGetUTCTimeMillis();
	if (!m_connection->Exchange(sent, answer, refusal, error))
		return false;
	exchanged = wxGetUTCTimeMillis() - exchanging;

	ibProtocolJournalInfo(wxT("protocol"), wxT("%s answered in %lld ms, %zu bytes: %s"), ibProtocolMethodName(method),
		(wxGetUTCTimeMillis() - began).GetValue(), answer.size(), wxString::FromUTF8(answer));

	ibProtocolNode root;
	if (!ibProtocolNode::Read(answer, root) || !root.IsNode()) {
		refusal = ibProtocolRefusal::Failed;
		error = wxT("the server's answer could not be read");
		return false;
	}
	// The answer to THIS call — one is in flight at a time. An envelope with no id is the server refusing a request it
	// could not read: still this one's.
	if (root.GetInt(ibProtocolName::RpcId, id) != id) {
		refusal = ibProtocolRefusal::Failed;
		error = wxT("the server answered another call");
		return false;
	}
	const ibProtocolNode failed = root.FindChild(ibProtocolName::RpcError);
	if (failed.IsNode()) {
		refusal = RefusalOf(failed.GetInt(ibProtocolName::RpcCode));
		error = failed.GetString(ibProtocolName::RpcMessage);
		return false;
	}
	const ibProtocolNode done = root.FindChild(ibProtocolName::RpcResult);
	if (!done.IsEmpty())
		result = done;
	return true;
}

void ibCommunicator::Take(const ibProtocolNode& answer)
{
	if (!answer.Has(ibProtocolName::Frame))
		return;   // not a frame — a fetch's rows, a schema's tree, a file's part

	const ibProtocolNode patch = answer.FindChild(ibProtocolName::Patch);
	if (patch.IsNode()) {
		// (protocol 2) when the patch makes another tab active, the views change hands first: the one held goes to its
		// tab's place, and the new tab's own — when one was kept — comes in; a tab the client was never sent the view
		// of is patched from the view held, as protocol 1 has it.
		const long long was = m_frame.GetInt(ibProtocolName::ActiveTab);
		const long long active = patch.GetInt(ibProtocolName::ActiveTab, was);
		if (m_protocol >= 2 && active != was) {
			json& frame = *m_frame.Json();
			const auto own = m_views.find(active);
			const auto held = frame.find(ibProtocolName::View);
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
				frame[ibProtocolName::View] = std::move(*own->second.Json());
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
	m_number = answer.GetInt(ibProtocolName::Frame);

	// A tab gone takes its view with it; the active tab's is the frame's own.
	if (m_protocol < 2)
		return;
	std::set<long long> open;
	for (const ibProtocolNode& tab : m_frame.FindChild(ibProtocolName::Tabs).Children())
		open.insert(tab.GetId());
	const long long active = m_frame.GetInt(ibProtocolName::ActiveTab);
	for (auto it = m_views.begin(); it != m_views.end();)
		it = open.count(it->first) != 0 && it->first != active ? std::next(it) : m_views.erase(it);
}

void ibCommunicator::Forget()
{
	m_client.clear();
	m_protocol = 1;
	m_number = 0;
	m_frame = ibProtocolNode();
	m_views.clear();
}
