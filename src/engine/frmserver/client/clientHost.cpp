#include "clientHost.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include <wx/datetime.h>

#include <cstring>

#include "backend/appData.h"
#include "backend/appHost.h"
#include "backend/backend_exception.h"   // ibBackendException — a value the configuration cannot make
#include "backend/backend_localization.h"   // the session's language — said with the frame
#include "backend/backend_picture.h"   // ibBackendPicture::GetServerPicture — a picture asked for by its id
#include "core/exception.h"
#include "core/formatString.h"        // ibFormatString — a value presented through Format()'s codes
#include "backend/mcp/mcpServer.h"      // the base's assistant access — where it listens, with the frame
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaFormObject.h"   // ibBackendCommandItem::Execute
#include "backend/rpc/rpcMessage.h"
#include "core/serialize/dataBuilder.h"
#include "core/utils/base64.h"         // ibBase64 — a file's part, in and out of the call's text
#include "backend/session/session.h"
#include "backend/temp/tempStorage.h"   // the client's files
#include "backend/userInfo.h"           // users — the names the login window offers
#include "core/crypto/sessionToken.h"
#include "core/crypto/sha256.hpp"

#include "frmserver/docView/docManager.h"   // ibDocTemplate — what opens a file
#include "frmserver/visualView/visualHost.h"

#include "clientChildFrame.h"
#include "clientDebugger.h"
#include "clientFrame.h"
#include "clientInstance.h"
#include "protocol/protocol.h"
#include "clientPatch.h"

namespace {

std::mutex                                                s_hostsMutex;
std::map<const ibApplicationInstance*, ibClientHost*>     s_hosts;

// A connectionless instance (an HTTP login) nobody has heard from for this long is taken down. A client with a
// connection is ended when its connection drops (Disconnect), not by this limit.
constexpr std::chrono::minutes kIdleLimit(30);

std::int64_t NowMs()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// backend.conf Resume, as milliseconds. The host is the process; a caller with no base still has a number.
std::int64_t ResumeWindowMs()
{
	const ibApplicationHost* const host = ibApplicationHost::Get();
	const std::size_t seconds = host != nullptr ? host->GetResumeSeconds() : 120;
	return static_cast<std::int64_t>(seconds) * 1000;
}

bool IsThinClient(const ibClientInstance& instance)
{
	const std::shared_ptr<ibSession> session = instance.ShareSession();
	return session != nullptr && session->GetKind() == ibSessionKind::ThinClient;
}

// A socket to come back to, and a window that is on. An HTTP login has no connection, a designer is not kept,
// and Resume 0 drops the session at once — none of those is handed a token.
bool WindowFor(const void* connection, const ibClientInstance& instance)
{
	return connection != nullptr && IsThinClient(instance) && ResumeWindowMs() > 0;
}

// The public Client id is not the bearer of an attached socket. A detached client is reached by login {Token}
// only. An HTTP client is the one marked at login, not whatever currently has a null connection: a thin
// client's socket is also null while it is being dropped.
bool MayCall(bool connectionless, const void* bound, std::int64_t deadline, const void* connection)
{
	if (connectionless)
		return true;
	if (deadline != 0)
		return false;
	if (bound == nullptr)
		return false;
	return bound == connection;
}

// "detached until 15:04:07" — what Active users shows for the window, fitted to currentActivity.
wxString DetachedUntil(std::int64_t deadlineMs)
{
	// time_t is UTC. Active users reads a local clock.
	const wxDateTime local = wxDateTime(static_cast<time_t>(deadlineMs / 1000)).ToTimezone(wxDateTime::Local);
	return wxString::Format(_("detached until %s"), local.Format(wxT("%H:%M:%S")));
}

bool AskedResume(const ibDataNode& params)
{
	const ibDataValue* features = params.FindField(ibProtocol::ibLogin::Features);
	if (features == nullptr || features->Kind() != ibDataKind::Array)
		return false;
	const wxString resume = ibProtocol::ibLogin::FeatureResume;
	for (const ibDataValue& item : features->AsArray()) {
		if (item.Kind() == ibDataKind::String && item.AsString() == resume)
			return true;
	}
	return false;
}

void FeedText(ibSHA256& sha, const ibString& text);
void FeedValue(ibSHA256& sha, const ibDataValue& value);

void FeedNode(ibSHA256& sha, const ibDataNode& node)
{
	for (const auto& field : node.Fields()) {
		FeedText(sha, field.first);
		FeedValue(sha, field.second);
	}
}

// The characters as they lie: a digest is compared with one this process made, never with another's, so it needs no
// encoding of its own — and the text no copy.
void FeedText(ibSHA256& sha, const ibString& text)
{
	if (!text.IsEmpty())
		sha.Update(reinterpret_cast<const unsigned char*>(text.wc_str()), text.length() * sizeof(wchar_t));
}

void FeedValue(ibSHA256& sha, const ibDataValue& value)
{
	const unsigned char kind = static_cast<unsigned char>(value.Kind());
	sha.Update(&kind, 1);
	switch (value.Kind()) {
	case ibDataKind::Bool: {
		const unsigned char bit = value.AsBool() ? 1 : 0;
		sha.Update(&bit, 1);
		break;
	}
	case ibDataKind::Number:
		FeedText(sha, value.AsNumber().ToString());
		break;
	case ibDataKind::String:
		FeedText(sha, value.AsString());
		break;
	case ibDataKind::Array:
		for (const ibDataValue& item : value.AsArray())
			FeedValue(sha, item);
		break;
	case ibDataKind::Child:
		if (value.AsChild())
			FeedNode(sha, *value.AsChild());
		break;
	default:
		break;
	}
}

void CallDigest(const ibString& method, const ibDataNode& params, unsigned char out[32])
{
	ibSHA256 sha;
	FeedText(sha, method);
	FeedNode(sha, params);
	sha.Final(out);
}

bool IdLess(const ibDataValue& left, const ibDataValue& right)
{
	if (left.Kind() == ibDataKind::Number && right.Kind() == ibDataKind::Number)
		return left.AsNumber().Compare(right.AsNumber()) < 0;
	if (left.Kind() == ibDataKind::String && right.Kind() == ibDataKind::String)
		return left.AsString() < right.AsString();
	return false;
}

void OfferResume(ibDataNode& result, const wxString& id, int protocol, const wxString& token)
{
	result.SetValue(ibProtocol::ibLogin::Client, id);
	result.SetValue(ibProtocol::ibLogin::Protocol, protocol);
	result.AddField(ibProtocol::ibLogin::Features, ibDataValue::Array(std::vector<ibDataValue>{
		ibDataValue::String(ibProtocol::ibLogin::FeatureResume) }));
	if (!token.IsEmpty())
		result.SetValue(ibProtocol::ibLogin::Token, token);
}

// WHAT A CLIENT EXECUTES IS WHAT THE DESKTOP'S NAVIGATION DOES: an object of the configuration — a catalog, a
// document, a common form, a command — found by its metadata id among the own objects of the configuration the
// client's session works in, and executed as a command item (enterprise's OnMenuItemClicked). An object's own forms
// are reached through the object: it hands out its form for the command type, the configuration's or the one the
// form builder makes.
const ibBackendCommandItem* FindCommand(const ibSession& session, ibMetaID metaId)
{
	const ibMetaDataConfigurationBase* metaData = session.GetMetaData();
	return metaData != nullptr ? dynamic_cast<const ibBackendCommandItem*>(metaData->FindAnyObjectByFilter(metaId)) : nullptr;
}

// WHAT A SCHEMA SHOWS, asked on the client's session: what it lists may depend on the person's rights. Without the right
// to it, the person gets an error, not the schema; what it could not read is the person's refusal, in the base's words —
// an unreadable list is not an empty one (the users' ListAll throws it). The method Schema's, and a login's Read.
bool BuildSchema(const ibClientSchema& schema, const std::shared_ptr<ibSession>& session, const ibDataNode& args,
	ibDataNode& result, ibProtocol::ibRefusal& refusal, wxString& error)
{
	bool done = false;
	session->Submit([&schema, &session, &args, &result, &done, &refusal, &error]() {
		if (!schema.AccessRight(*session)) {
			refusal = ibProtocol::ibRefusal::NoRight;
			error = wxT("no right to this schema");
			return;
		}
		try {
			schema.Build(*session, args, result);
			done = true;
		}
		catch (const ibCoreException& err) {
			refusal = ibProtocol::ibRefusal::Failed;
			error = err.GetErrorDescription();
		}
	}).get();
	return done;
}

// …AND EVERY SCHEMA A LOGIN NAMES (its Read), each built as above, beside the frame of its start. A schema the person has
// no right to, or one that could not be read, is left out: the client asks for it, and is told why.
void BuildSchema(ibClientInstance& instance, const ibDataNode& params, ibDataNode& result)
{
	const ibDataValue* const reads = params.FindField(ibProtocol::ibLogin::Read);
	ibClientFrame* const frame = instance.GetFrame();
	const std::shared_ptr<ibSession> session = instance.ShareSession();
	if (reads == nullptr || reads->Kind() != ibDataKind::Array || frame == nullptr || session == nullptr)
		return;
	ibDataNode& answered = result.Child(ibProtocol::ibLogin::Read);
	for (const ibDataValue& read : reads->AsArray()) {
		if (read.Kind() != ibDataKind::Child || read.AsChild() == nullptr)
			continue;
		const std::int32_t kind = read.AsChild()->GetValue<std::int32_t>(ibProtocol::ibLogin::Schema);
		const ibClientSchema* const schema = frame->GetDocumentManager()->FindSchema(static_cast<ibProtocol::ibSchema::ibKind>(kind));
		if (schema == nullptr)
			continue;
		ibDataNode built;
		ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
		wxString error;
		if (!BuildSchema(*schema, session, ibDataNode(), built, refusal, error))
			continue;
		built.SetValue(ibProtocol::ibLogin::Schema, kind);
		answered.Children().push_back(std::move(built));
	}
}

// THE MENU AS IT STANDS NOW: each item with what it does, a command with its name, default key and picture, and whether
// it is enabled now (the desktop's EVT_UPDATE_UI): a command by the active tab's document, a schema by its own right —
// the same questions a call of it is refused by.
void DrawMenu(ibClientFrame* frame, const std::vector<ibClientMenu::Item>& items, ibDataNode& node)
{
	const ibDocManager* const manager = frame->GetDocumentManager();
	for (const ibClientMenu::Item& item : items) {
		ibDataNode& child = node.AddChild(0, 0);
		if (item.kind == ibClientMenuItemKind::Separator) {
			child.SetValue(ibProtocol::ibFrame::ibMenu::Separator, true);
			continue;
		}

		bool enabled = true;
		if (item.kind == ibClientMenuItemKind::Command) {
			const auto found = manager->GetCommands().find(item.command);
			child.SetValue(ibProtocol::ibFrame::ibMenu::Command, static_cast<std::int32_t>(item.command));
			if (found != manager->GetCommands().end()) {
				child.SetValue(ibProtocol::ibFrame::ibMenu::Title, found->second.title);
				if (!found->second.shortcut.IsEmpty())
					child.SetValue(ibProtocol::ibFrame::ibMenu::Shortcut, found->second.shortcut);
				if (found->second.picture != ibClientStockPicture::None)
					child.SetValue(ibProtocol::ibFrame::ibMenu::StockPicture, static_cast<std::int32_t>(found->second.picture));
			}
			enabled = found != manager->GetCommands().end() && frame->IsCommandEnabled(item.command);
		}
		else if (item.kind == ibClientMenuItemKind::Schema) {
			child.SetValue(ibProtocol::ibFrame::ibMenu::Schema, static_cast<std::int32_t>(item.schema));
			if (item.schemaCommand != ibProtocol::ibSchema::ibCommand{})
				child.SetValue(ibProtocol::ibFrame::ibMenu::Command, static_cast<std::int32_t>(item.schemaCommand));
			child.SetValue(ibProtocol::ibFrame::ibMenu::Title, item.title);
			enabled = frame->IsSchemaAllowed(item.schema);
		}
		else {
			child.SetValue(ibProtocol::ibFrame::ibMenu::Title, item.title);
			DrawMenu(frame, item.items, child);
		}
		// Its own picture, its id alone — the client draws it from the core, as the backend does.
		if (item.picture != 0)
			child.SetValue(ibProtocol::ibFrame::ibMenu::Picture, ibBackendPicture::SendPicture(item.picture));
		if (enabled)
			child.SetValue(ibProtocol::ibFrame::ibMenu::Enabled, true);
	}
}

// The form a call is for: the one it names by its key (a cell of the start page), the active tab's otherwise.
ibValueForm* FormOfCall(const ibClientFrame* frame, const wxString& key)
{
	return key.IsEmpty() ? frame->ActiveForm() : ibFormVisualDocument::FindFormByGuid(ibGuid(key));
}

// WHAT THE PERSON DID — an Event call's {Control, Event[, Args][, Form][, Tab]}, or one the client held and a later call
// carries (Pending): to the control of the form it names, or to a tab's own view.
void DispatchEvent(ibClientFrame* frame, const ibDataNode& call)
{
	const ibProtocol::ibEvent::ibKind event = static_cast<ibProtocol::ibEvent::ibKind>(call.GetValue<std::int32_t>(ibProtocol::ibEvent::Event));
	const ibDataNode none;
	const ibDataNode* const found = call.FindChild(ibProtocol::ibEvent::Args);
	const ibDataNode& args = found != nullptr ? *found : none;
	// A TAB'S OWN VIEW, named by the tab — what the person did in a text, not in a control of a form; by the tab, not the
	// active one, as it may be handed over when the tab is being closed.
	const std::int32_t tabId = call.GetValue<std::int32_t>(ibProtocol::ibEvent::Tab);
	if (tabId != 0) {
		if (ibClientChildFrame* const tab = frame->Tab(frame->FindTab(tabId))) {
			if (ibView* const view = tab->GetView())
				view->OnClientEvent(event, args);
		}
	}
	else if (ibValueForm* const form = FormOfCall(frame, call.GetValue<wxString>(ibProtocol::ibEvent::Form)))
		form->DispatchEvent(call.GetValue<std::int32_t>(ibProtocol::ibEvent::Control), event, args);
}

} // namespace

ibClientHost::ibClientHost(ibApplicationInstance* applicationInstance, unsigned route)
	: m_applicationInstance(applicationInstance), m_route(route)
{
	{
		std::lock_guard<std::mutex> lock(s_hostsMutex);
		s_hosts[applicationInstance] = this;
	}
	m_round = std::thread(&ibClientHost::RoundBody, this);
}

ibClientHost::~ibClientHost()
{
	{
		std::lock_guard<std::mutex> lock(s_hostsMutex);
		s_hosts.erase(m_applicationInstance);
	}
	{
		std::lock_guard<std::mutex> lock(m_roundMutex);
		m_stop = true;
	}
	m_roundSignal.notify_all();
	if (m_round.joinable())
		m_round.join();

	// Every client goes — each its own session's teardown (ibClientInstance::OnExit).
	std::map<ibString, std::shared_ptr<Client>> clients;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		clients.swap(m_clients);
	}
	for (auto& entry : clients)
		entry.second->instance->OnExit();
}

ibClientHost* ibClientHost::Find(const ibApplicationInstance* applicationInstance)
{
	std::lock_guard<std::mutex> lock(s_hostsMutex);
	auto it = s_hosts.find(applicationInstance);
	return it != s_hosts.end() ? it->second : nullptr;
}

std::shared_ptr<ibClientHost::Client> ibClientHost::FindClient(const ibString& id) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto it = m_clients.find(id);
	return it != m_clients.end() ? it->second : nullptr;
}

void ibClientHost::RemoveClient(const ibString& id)
{
	std::shared_ptr<Client> client;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_clients.find(id);
		if (it == m_clients.end())
			return;
		client = it->second;
		m_clients.erase(it);
	}
	client->instance->OnExit();
}

std::shared_ptr<ibClientHost::Client> ibClientHost::FindByDigest(const unsigned char* digest) const
{
	// Every digest is compared. A match does not stop the walk: the compare itself does not stop early either.
	std::shared_ptr<Client> found;
	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto& entry : m_clients) {
		const bool same = entry.second->hasToken && ibSessionToken::DigestEqual(entry.second->tokenDigest, digest);
		if (same)
			found = entry.second;
	}
	return found;
}

bool ibClientHost::ResumeByToken(const ibDataNode& params, ibDataNode& result, ibProtocol::ibRefusal& refusal, wxString& error,
	const void* connection)
{
	unsigned char raw[ibSessionToken::kBytes];
	unsigned char digest[ibSessionToken::kDigest];
	// The route it ends in was the cluster's to read; the bearer is the hex.
	wxString token;
	unsigned route = 0;
	ibSessionToken::SplitRoute(params.GetValue<wxString>(ibProtocol::ibLogin::Token), token, route);
	const auto refuse = [&refusal, &error](const wxString& text) {
		refusal = ibProtocol::ibRefusal::LoginRefused;
		error = text;
	};
	if (!ibSessionToken::ParseHex(token, raw) || !ibSessionToken::Hash(raw, ibSessionToken::kBytes, digest)) {
		refuse(wxT("the login was refused"));
		return false;
	}

	const std::shared_ptr<Client> client = FindByDigest(digest);
	if (!client) {
		refuse(wxT("the login was refused"));
		return false;
	}

	wxString id;
	bool expired = false;
	bool rebound = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const auto& entry : m_clients) {
			if (entry.second == client)
				id = entry.first;
		}
		if (id.IsEmpty()) {
			refuse(wxT("the login was refused"));
			return false;
		}
		const std::int64_t deadline = client->resumeDeadlineMs.load(std::memory_order_acquire);
		// Attached: the token must not move the session onto another socket. Only the window (deadline set,
		// and not yet passed) rebinds. An expired window closes the client below.
		if (deadline == 0) {
			refuse(wxT("the login was refused"));
			return false;
		}
		if (NowMs() >= deadline)
			expired = true;
		else {
			client->connection.store(connection, std::memory_order_release);
			client->resumeDeadlineMs.store(0, std::memory_order_release);
			client->instance->Touch();
			rebound = true;
		}
	}
	if (rebound) {
		if (std::shared_ptr<ibSession> session = client->instance->ShareSession())
			session->SetActivity(wxT("idle"));
	}
	if (expired) {
		RemoveClient(id);
		refuse(wxT("the login was refused"));
		return false;
	}

	// The previous client. Not a frame: Client::frame is left where the last answer put it.
	OfferResume(result, id, client->protocol, wxString());
	return true;
}

bool ibClientHost::RememberToken(Client& client, ibDataNode& result, ibProtocol::ibRefusal& refusal, wxString& error)
{
	const std::vector<unsigned char> raw = ibSessionToken::Generate();
	if (raw.size() != ibSessionToken::kBytes) {
		refusal = ibProtocol::ibRefusal::Failed;
		error = wxT("the token could not be drawn");
		return false;
	}
	unsigned char digest[ibSessionToken::kDigest];
	if (!ibSessionToken::Hash(raw.data(), raw.size(), digest)) {
		refusal = ibProtocol::ibRefusal::Failed;
		error = wxT("the token could not be drawn");
		return false;
	}
	const wxString hash = ibSessionToken::Hex(digest, ibSessionToken::kDigest);
	// Into the session's row by the registry, which writes the row's every other column too. A row held, or a
	// registry busy past its wait, is a login to repeat, not a server that failed.
	const std::shared_ptr<ibSession> session = client.instance->ShareSession();
	if (!session || !session->SetTokenHash(hash)) {
		refusal = ibProtocol::ibRefusal::NotNow;
		error = wxT("the token could not be stored");
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::memcpy(client.tokenDigest, digest, ibSessionToken::kDigest);
		client.hasToken = true;
	}
	// The plaintext, once, on this answer, ending in this server's route. It is not kept.
	result.SetValue(ibProtocol::ibLogin::Token,
		ibSessionToken::WithRoute(ibSessionToken::Hex(raw.data(), raw.size()), m_route));
	return true;
}

void ibClientHost::Disconnect(const void* connection)
{
	if (connection == nullptr)
		return;

	// Who was on this socket, copied out before anyone is classified: a thin client stays, for Resume seconds;
	// every other kind ends as it always has. The host's destructor does not come through here — it ends them all.
	std::vector<std::shared_ptr<Client>> matched;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_notifiers.erase(connection);
		for (const auto& entry : m_clients) {
			if (entry.second->connection.load(std::memory_order_acquire) == connection)
				matched.push_back(entry.second);
			// A debugger's connection takes nobody with it: its client is no longer told the debugger's news.
			if (entry.second->debugConnection.load(std::memory_order_acquire) == connection)
				entry.second->debugConnection.store(nullptr, std::memory_order_release);
		}
	}

	struct Held {
		std::shared_ptr<Client> client;
		bool                    grace = false;
	};
	std::vector<Held> held;
	held.reserve(matched.size());
	for (const std::shared_ptr<Client>& client : matched)
		held.push_back(Held{ client, WindowFor(client->connection.load(std::memory_order_acquire), *client->instance) });

	const std::int64_t deadline = NowMs() + ResumeWindowMs();
	std::vector<std::shared_ptr<Client>> gone;
	std::vector<std::shared_ptr<Client>> detaching;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const Held& one : held) {
			if (one.client->connection.load(std::memory_order_acquire) != connection)
				continue;   // returned already, on some other call
			wxString id;
			for (const auto& entry : m_clients) {
				if (entry.second == one.client)
					id = entry.first;
			}
			if (id.IsEmpty())
				continue;
			if (one.grace) {
				// The deadline first. A call that already sees the cleared connection still sees a
				// deadline, and only a client marked connectionless at login is callable with no connection.
				one.client->resumeDeadlineMs.store(deadline, std::memory_order_release);
				detaching.push_back(one.client);
			}
			else {
				gone.push_back(one.client);
				m_clients.erase(id);
			}
		}
	}
	for (const std::shared_ptr<Client>& client : detaching) {
		if (std::shared_ptr<ibSession> session = client->instance->ShareSession())
			session->SetActivity(DetachedUntil(deadline));
	}
	if (!detaching.empty()) {
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const std::shared_ptr<Client>& client : detaching) {
			// A resume that landed during the probe owns the client again.
			if (client->connection.load(std::memory_order_acquire) == connection
				&& client->resumeDeadlineMs.load(std::memory_order_acquire) != 0)
				client->connection.store(nullptr, std::memory_order_release);
		}
	}
	for (const std::shared_ptr<Client>& client : gone)
		client->instance->OnExit();
}

bool ibClientHost::LoggedIn(const void* connection) const
{
	if (connection == nullptr)
		return false;

	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto& it : m_clients) {
		if (it.second->connection == connection || it.second->debugConnection == connection)
			return true;
	}
	return false;
}

void ibClientHost::SetNotifier(const void* connection, std::function<void(const ibString& text)> notify)
{
	if (connection == nullptr)
		return;
	std::lock_guard<std::mutex> lock(m_mutex);
	if (notify)
		m_notifiers[connection] = std::move(notify);
	else
		m_notifiers.erase(connection);
}

void ibClientHost::NotifyChanged(const ibClientInstance* instance)
{
	// The client's id and how its connection is told — sent outside the lock: a transport's send may wait on its socket.
	wxString id;
	std::function<void(const ibString& text)> notify;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const auto& [clientId, client] : m_clients) {
			if (client->instance.get() != instance)
				continue;
			if (client->answering.load(std::memory_order_acquire))
				break;   // the call being answered carries it
			const auto found = m_notifiers.find(client->connection.load(std::memory_order_acquire));
			if (found != m_notifiers.end()) {
				id = clientId;
				notify = found->second;
			}
			break;
		}
	}
	if (!notify)
		return;

	// A JSON-RPC notification — no id, no answer wanted: the client asks for its frame by itself.
	notify(wxString::Format(wxT("{\"jsonrpc\":\"2.0\",\"method\":\"changed\",\"params\":{\"Client\":\"%s\"}}"), id));
}

void ibClientHost::NotifyDebug(const ibClientInstance* instance, const ibDataNode& event)
{
	// As NotifyChanged — the id and the notifier taken under the lock, sent outside it; the debugger's connection's, and
	// told whatever call is being answered: the news is no part of a frame.
	wxString id;
	std::function<void(const ibString& text)> notify;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const auto& [clientId, client] : m_clients) {
			if (client->instance.get() != instance)
				continue;
			const auto found = m_notifiers.find(client->debugConnection.load(std::memory_order_acquire));
			if (found != m_notifiers.end()) {
				id = clientId;
				notify = found->second;
			}
			break;
		}
	}
	if (!notify)
		return;

	// The envelope written as NotifyChanged's — the method where an answer's id stands, which is how a connection tells
	// the news from an answer, whatever its length (ibProtocolConnectionServer).
	ibDataNode params = event;
	params.SetValue(ibProtocol::ibLogin::Client, id);
	notify(wxT("{\"jsonrpc\":\"2.0\",\"method\":\"debug\",\"params\":") + ibRpcRenderNode(params) + wxT("}"));
}

//*******************************************************************************************
//*                                        The door                                         *
//*******************************************************************************************

ibString ibClientHost::Call(const ibString& text, const void* connection, const ibString& address)
{
	// ⭐⭐ A CALL'S TYPES ARE ITS SESSION'S (Max, 2026-10-15: the metadata is taken from the session). A node a client hands back
	// names its type by word (NodeType) — the form editor's form, whole — and the word is a class of the configuration
	// the CALLER's session works in. This thread works for none yet: the thread's lookup (activeMetaData) threw "works
	// for no base" at every such call. So the call is read once, its words kept where it named a type by one, and then
	// they are looked up in the configuration of that client's session (ibRpcLookupTypes) — the text is not read again:
	// it was, the whole form a second time on every call of the form editor (40-90 ms of 300, measured 2026-10-21).
	const std::int64_t reading = NowMs();
	ibRpcRequest request;
	wxString error;
	bool unknownType = false;
	if (!ibRpcParseRequest(text, request, error, unknownType))
		return ibRpcWriteError(ibDataValue(), ibRpcError::Parse, error);
	if (unknownType) {
		if (const std::shared_ptr<Client> caller = FindClient(request.m_params.GetValue<ibString>(ibProtocol::ibLogin::Client))) {
			if (const std::shared_ptr<ibSession> session = caller->instance->ShareSession())
				ibRpcLookupTypes(request.m_params, session->GetMetaData());
		}
	}
	const std::int64_t read = NowMs() - reading;

	const ibProtocol::ibMethod method = ibProtocol::MethodFromName(request.m_method.ToUtf8().c_str());
	if (method == ibProtocol::ibMethod::Unknown) {
		return request.WantsAnswer()
			? ibRpcWriteError(request.m_id, ibRpcError::MethodNotFound, wxString::Format(wxT("no method named '%s'"), request.m_method))
			: ibString();
	}

	// THE LAST ANSWER OF A CLIENT THAT OPTED IN. The same id with the same method and params is the saved text,
	// and it is not run again. A different call under that id, or an older id, is refused and not run. A login
	// does not take the slot: the lost call is retried after login {Token}, with its own id. Who may call is
	// decided before the slot is read, so a detached client does not hand its last answer to whoever names it.
	// Nor does the debugger's: answered at once beside the client's calls, by a connection of its own (Debug).
	const bool tokenLogin = method == ibProtocol::ibMethod::Login
		&& request.m_params.FindField(ibProtocol::ibLogin::User) == nullptr
		&& request.m_params.FindField(ibProtocol::ibLogin::Token) != nullptr;
	struct Slot {
		std::shared_ptr<Client> client;
		bool                    owner = false;   // set only by the call that sets replyPending
		~Slot()
		{
			if (!owner || !client)
				return;
			std::lock_guard<std::mutex> lock(client->lock);
			client->replyPending = false;
			client->replyWait.notify_all();
		}
	} slot;
	unsigned char callDigest[32] = {};
	if (request.WantsAnswer() && !tokenLogin && method != ibProtocol::ibMethod::Login && method != ibProtocol::ibMethod::Debug) {
		slot.client = FindClient(request.m_params.GetValue<ibString>(ibProtocol::ibLogin::Client));
		if (slot.client) {
			std::unique_lock<std::mutex> lock(slot.client->lock);
			if (!MayCall(slot.client->connectionless, slot.client->connection.load(std::memory_order_acquire),
				slot.client->resumeDeadlineMs.load(std::memory_order_acquire), connection)) {
				lock.unlock();
				slot.client.reset();
				return ibRpcWriteError(request.m_id, static_cast<std::int32_t>(ibProtocol::ibRefusal::NoSession), wxT("the client is not connected"));
			}
			if (slot.client->dedupe) {
				CallDigest(request.m_method, request.m_params, callDigest);
				for (;;) {
					if (slot.client->replyPending) {
						slot.client->replyWait.wait(lock);
						continue;
					}
					if (slot.client->hasReply && IdLess(request.m_id, slot.client->replyId)) {
						lock.unlock();
						slot.client.reset();
						return ibRpcWriteError(request.m_id, static_cast<std::int32_t>(ibProtocol::ibRefusal::BadParameter),
							wxT("the call id is older than the last one answered"));
					}
					if (slot.client->hasReply && slot.client->replyId == request.m_id) {
						const bool same = slot.client->hasReply == true
							&& std::memcmp(callDigest, slot.client->replyDigest, sizeof(callDigest)) == 0;
						if (same) {
							const ibString saved = slot.client->reply;
							return saved;
						}
						lock.unlock();
						slot.client.reset();
						return ibRpcWriteError(request.m_id, static_cast<std::int32_t>(ibProtocol::ibRefusal::BadParameter),
							wxT("the call id was already answered for another call"));
					}
					slot.client->replyPending = true;
					slot.client->pendingId = request.m_id;
					slot.owner = true;
					break;
				}
			}
			else
				slot.client.reset();
		}
	}

	ibDataNode result;
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	const std::int64_t began = NowMs() - reading;
	ibJournalStopwatch took;
	took.Resume();
	const bool done = Call(method, request.m_params, result, refusal, error, connection, address);
	took.Pause();

	// EACH CALL, A LINE — what a client asked, how long it took, why it was refused: the client's side of the work, in
	// order beside the engine's own, for a file base in the client's process as for the application server.
	if (done)
		ibJournalInfo(wxT("client"), wxT("%s: %lld ms"), request.m_method, took.Ms());
	else
		ibJournalWarning(wxT("client"), wxT("%s refused (%d) after %lld ms: %s"), request.m_method,
			static_cast<int>(refusal), took.Ms(), error);
	// …and before it, the request: its text read (and the types it named by words looked up in its session's
	// configuration), then whose it is and whether it was answered already — the call begun that long after the text came in.
	ibJournalInfo(wxT("client"), wxT("  request read %lld ms%s, %zu chars, the call begun at %lld ms"), read,
		unknownType ? wxT(", its types looked up") : wxT(""), text.length(), began);

	if (!request.WantsAnswer())
		return ibString();

	// …and the answer written — its text, the part of a call that grows with the frame. The result goes into the
	// envelope as it is (it is not read again), and the text saved for a repeated call is one more owner of it.
	const std::int64_t writing = NowMs();
	const ibString answer = done ? ibRpcWriteResult(request.m_id, std::move(result)) : ibRpcWriteError(request.m_id, static_cast<std::int32_t>(refusal), error);
	ibJournalInfo(wxT("client"), wxT("  answer written %lld ms, %zu chars"), NowMs() - writing, answer.length());
	// A detached NoSession was not accepted, so it is not the call's answer. Anything else, including a refusal, is.
	// Only the call that owns the slot writes it: a lookup that returned the saved text never did.
	if (slot.owner && slot.client && (done || refusal != ibProtocol::ibRefusal::NoSession)) {
		std::lock_guard<std::mutex> lock(slot.client->lock);
		slot.client->hasReply = true;
		slot.client->replyId = request.m_id;
		std::memcpy(slot.client->replyDigest, callDigest, sizeof(callDigest));
		slot.client->reply = answer;
	}
	return answer;
}

bool ibClientHost::Call(ibProtocol::ibMethod method, const ibDataNode& params, ibDataNode& result, ibProtocol::ibRefusal& refusal,
	wxString& error, const void* connection, const ibString& address)
{
	bool done = false;
	refusal = ibProtocol::ibRefusal::None;
	error.clear();
	// Why, and its text — said together by every refusal below.
	const auto refuse = [&refusal, &error](ibProtocol::ibRefusal why, const wxString& text) { refusal = why; error = text; };

	if (method == ibProtocol::ibMethod::Unknown) {
		refuse(ibProtocol::ibRefusal::NotFound, wxT("no such method"));
	}
	else if (method == ibProtocol::ibMethod::Login) {
		const bool byToken = params.FindField(ibProtocol::ibLogin::User) == nullptr
			&& params.FindField(ibProtocol::ibLogin::Token) != nullptr;
		if (byToken) {
			// The same client. No frame is drawn, and the frame number stays, so the next frame {Since} is a patch.
			done = ResumeByToken(params, result, refusal, error, connection);
		}
		else {
		// The instance is made and logged in; the start runs as work of its own, so a start that asks the person
		// something returns here with the request — and the client's id, without which it could not respond.
		std::int32_t protocol = 1;
		params.GetValue(ibProtocol::ibLogin::Protocol, protocol);
		auto client = std::make_shared<Client>();
		const wxString id = ibGuid(ibGuid::newGuid()).str();
		std::int32_t mode = static_cast<std::int32_t>(ibProtocol::ibLogin::ibMode::Runtime);
		params.GetValue(ibProtocol::ibLogin::Mode, mode);
		client->instance = std::make_shared<ibClientInstance>(m_applicationInstance, id, address, static_cast<ibProtocol::ibLogin::ibMode>(mode));
		client->connectionless = connection == nullptr;
		client->connection.store(connection, std::memory_order_relaxed);
		client->protocol = std::min(protocol, ibProtocol::Version);
		// THE LOGIN — false: its user or its password; thrown: refused before any (another designer at work), said as
		// it is. Read as a wrong password, it opened the client's login window on a question nobody asked (2026-10-20).
		const auto logIn = [&]() {
			try {
				if (client->instance->Login(params.GetValue<wxString>(ibProtocol::ibLogin::User), params.GetValue<wxString>(ibProtocol::ibLogin::Password)))
					return true;
				refuse(ibProtocol::ibRefusal::LoginRefused, wxT("the login was refused"));
			}
			catch (const ibCoreException& err) {
				refuse(ibProtocol::ibRefusal::NotNow, err.GetErrorDescription());
			}
			return false;
		};
		if (protocol < 1) {
			refuse(ibProtocol::ibRefusal::BadParameter, wxString::Format(wxT("no protocol version %d"), protocol));
		}
		else if (logIn()) {
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_clients[id] = client;
			}
			std::lock_guard<std::mutex> clientLock(client->lock);
			ibClientInstance* const instance = client->instance.get();
			done = Run(*client, [instance](ibClientFrame*) { instance->Start(); }, 0, result, refusal, error);
			if (client->instance->GetFrame() == nullptr && !client->unsettled.valid()) {
				// The start refused the client — whatever the drawing after it met: a refused start ends the session,
				// and the frame's drawing then races its teardown.
				done = false;
				refuse(ibProtocol::ibRefusal::StartRefused, wxT("the start was refused"));
			}
			if (!done)
				RemoveClient(id);
			else if (!WindowFor(client->connection.load(std::memory_order_acquire), *client->instance)) {
				// No window: no token, and `resume` is not offered. The answer is the frame, as before.
				result.SetValue(ibProtocol::ibLogin::Client, id);
				result.SetValue(ibProtocol::ibLogin::Protocol, client->protocol);
			}
			else if (!RememberToken(*client, result, refusal, error)) {
				done = false;
				RemoveClient(id);
			}
			else {
				// THE VERSION BOTH SPEAK — the older of the two — and what the server offers beyond it. `resume` is
				// listed, so a client that does not know it never depends on it. Naming it on the request opts into
				// the slot. RememberToken has put Token on the answer, once.
				if (AskedResume(params))
					client->dedupe = true;
				OfferResume(result, id, client->protocol, result.GetValue<wxString>(ibProtocol::ibLogin::Token));
			}
			// WHAT THE CLIENT READS RIGHT AFTER ITS START, read in it (Read) — each schema it names, built as `schema`
			// would build it, beside the frame: the designer's debugger store was a call of its own at every start, for
			// an empty answer where the configuration has no debugger. Only once the start is done, not waiting.
			if (done && !client->unsettled.valid())
				BuildSchema(*client->instance, params, result);
		}
		}
	}
	else if (method == ibProtocol::ibMethod::Users) {
		// The names the login window offers, as the desktop's lists them — asked before any login, so on the caller's
		// thread, which works for no base (the server's listener, a file base's client, the web server's worker): it
		// works for this host's for the read.
		const ibApplicationInstanceScope working(m_applicationInstance);
		try {
			std::vector<ibDataValue> names;
			for (const ibUserInfo::Brief& user : ibUserInfo::ListAll())
				names.push_back(ibDataValue::String(user.m_strUserName));
			result.AddField(ibProtocol::ibLogin::Users, ibDataValue::Array(std::move(names)));
			done = true;
		}
		catch (const ibCoreException& err) {
			refuse(ibProtocol::ibRefusal::Failed, err.GetErrorDescription());
		}
	}
	else if (method == ibProtocol::ibMethod::Logout) {
		const std::shared_ptr<Client> client = FindClient(params.GetValue<ibString>(ibProtocol::ibLogin::Client));
		if (client == nullptr) {
			done = true;
		}
		else {
			bool allowed = false;
			{
				std::lock_guard<std::mutex> clientLock(client->lock);
				allowed = MayCall(client->connectionless, client->connection.load(std::memory_order_acquire),
					client->resumeDeadlineMs.load(std::memory_order_acquire), connection);
			}
			if (!allowed)
				refuse(ibProtocol::ibRefusal::NoSession, wxT("the client is not connected"));
			else {
				RemoveClient(params.GetValue<ibString>(ibProtocol::ibLogin::Client));
				done = true;
			}
		}
	}
	else if (method == ibProtocol::ibMethod::Debug) {
		// ⭐ BESIDE THE CLIENT'S CALLS, NOT AFTER THEM (protocol 6): not under the client's lock and with no frame — a step
		// is not queued behind a call waiting on the very stop it would let go.
		const std::shared_ptr<Client> client = FindClient(params.GetValue<ibString>(ibProtocol::ibLogin::Client));
		if (client == nullptr)
			refuse(ibProtocol::ibRefusal::NoSession, wxT("no such client"));
		else
			done = Debug(*client, params, result, refusal, error, connection);
	}
	else {
		const std::shared_ptr<Client> client = FindClient(params.GetValue<ibString>(ibProtocol::ibLogin::Client));
		if (client == nullptr) {
			refuse(ibProtocol::ibRefusal::NoSession, wxT("no such client"));
		}
		else {
			std::lock_guard<std::mutex> clientLock(client->lock);
			// Detached, or a socket that is not this client's: the public Client id is not the bearer.
			if (!MayCall(client->connectionless, client->connection.load(std::memory_order_acquire),
				client->resumeDeadlineMs.load(std::memory_order_acquire), connection)) {
				refuse(ibProtocol::ibRefusal::NoSession, wxT("the client is not connected"));
			}
			else {
			client->instance->Touch();
			// ⭐ WHERE THE PERSON IS, held by the client (protocol 3, Pending) — the latest of each, in the order it came,
			// done before the call that carries it: a press reads the row the cursor stands on. One that fails is said in
			// the journal; the call goes on.
			if (const ibDataValue* const pending = params.FindField(ibProtocol::ibLogin::Pending)) {
				ibClientFrame* const frame = client->instance->GetFrame();
				const std::shared_ptr<ibSession> session = client->instance->ShareSession();
				if (frame != nullptr && session != nullptr && pending->Kind() == ibDataKind::Array) {
					session->Submit([frame, pending]() {
						for (const ibDataValue& held : pending->AsArray()) {
							if (held.Kind() != ibDataKind::Child || held.AsChild() == nullptr)
								continue;
							try {
								DispatchEvent(frame, *held.AsChild());
							}
							catch (const std::exception& err) {
								ibJournalWarning(wxT("client"), wxT("a held event ended with an exception: %s"),
									wxString::FromUTF8(err.what()));
							}
							catch (...) {
								ibJournalWarning(wxT("client"), wxT("a held event ended with an exception"));
							}
						}
					}).get();
				}
			}
			// The frame the client holds — the number it was last answered with; 0 or absent: none, the frame whole.
			const std::int32_t since = params.GetValue<std::int32_t>(ibProtocol::ibLogin::Since);

			if (method == ibProtocol::ibMethod::Frame) {
				done = Run(*client, nullptr, since, result, refusal, error);
			}
			else if (method == ibProtocol::ibMethod::Schema) {
				// Asked of the doc manager of the client's frame — what its application offers — and asked on the
				// client's session: what a schema lists, and what it acts on, may depend on the person's rights.
				const ibProtocol::ibSchema::ibKind kind = static_cast<ibProtocol::ibSchema::ibKind>(params.GetValue<std::int32_t>(ibProtocol::ibSchema::Schema));
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				const ibClientSchema* const schema = frame != nullptr ? frame->GetDocumentManager()->FindSchema(kind) : nullptr;
				const ibDataNode* found = params.FindChild(ibProtocol::ibSchema::Args);
				const ibDataNode args = found != nullptr ? *found : ibDataNode();
				if (frame == nullptr || session == nullptr) {
					refuse(ibProtocol::ibRefusal::NoSession, wxT("the client has no session"));
				}
				else if (schema == nullptr) {
					refuse(ibProtocol::ibRefusal::NotFound, wxT("no such schema"));
				}
				else if (params.FindField(ibProtocol::ibSchema::Command) == nullptr) {
					done = BuildSchema(*schema, session, args, result, refusal, error);
				}
				else {
					// What a person did in it: refused before anything is done, or the work that does it — run as the
					// client's work, so the answer is the frame, with what it opened.
					const std::int32_t command = params.GetValue<std::int32_t>(ibProtocol::ibSchema::Command);
					std::function<void()> work;
					session->Submit([schema, &session, command, &args, &work, &refuse, &refusal, &error]() {
						if (!schema->AccessRight(*session))
							refuse(ibProtocol::ibRefusal::NoRight, wxT("no right to this schema"));
						else
							work = schema->Command(*session, command, args, refusal, error);
					}).get();
					if (work)
						done = Run(*client, [work](ibClientFrame*) { work(); }, since, result, refusal, error);
				}
			}
			else if (method == ibProtocol::ibMethod::Execute) {
				// What the configuration does not have is refused here, to the client: an empty frame would say
				// nothing happened.
				const ibMetaID metaId = params.GetValue<std::int32_t>(ibProtocol::ibExecute::Command);
				std::int32_t type = ibInterfaceCommandType::ibInterfaceCommandType_Default;
				params.GetValue(ibProtocol::ibExecute::Type, type);
				const std::shared_ptr<ibSession> session = client->instance->ShareSession();
				const ibBackendCommandItem* const command = session != nullptr ? FindCommand(*session, metaId) : nullptr;
				if (session == nullptr) {
					refuse(ibProtocol::ibRefusal::NoSession, wxT("the client has no session"));
				}
				else if (command == nullptr) {
					refuse(ibProtocol::ibRefusal::NotFound, wxString::Format(wxT("no command with id %d"), metaId));
				}
				else {
					const ibInterfaceCommandType commandType = static_cast<ibInterfaceCommandType>(type);
					done = Run(*client, [command, commandType](ibClientFrame*) { command->Execute(commandType); }, since,
						result, refusal, error);
				}
			}
			else if (method == ibProtocol::ibMethod::Event) {
				done = Run(*client, [params](ibClientFrame* frame) { DispatchEvent(frame, params); }, since, result,
					refusal, error);
			}
			else if (method == ibProtocol::ibMethod::Activate || method == ibProtocol::ibMethod::Close) {
				// A TAB BY ITS ID — a position shifts the moment a tab closes. One the frame does not have is refused
				// before anything is done (asked on the session, where the tabs are).
				const std::int32_t tabId = params.GetValue<std::int32_t>(ibProtocol::ibFrame::Tab);
				const bool activate = method == ibProtocol::ibMethod::Activate;
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				bool found = false;
				if (frame != nullptr && session != nullptr)
					session->Submit([frame, tabId, &found]() { found = frame->FindTab(tabId) < frame->TabCount(); }).get();
				if (frame == nullptr || session == nullptr)
					refuse(ibProtocol::ibRefusal::NoSession, wxT("the client has no session"));
				else if (!found)
					refuse(ibProtocol::ibRefusal::NotFound, wxString::Format(wxT("no tab %d"), tabId));
				else {
					done = Run(*client, [tabId, activate](ibClientFrame* frame) {
						const std::size_t tab = frame->FindTab(tabId);
						if (activate)
							frame->SetActiveTab(tab);
						else
							frame->CloseTab(tab);
					}, since, result, refusal, error);
				}
			}
			else if (method == ibProtocol::ibMethod::Respond) {
				// The response goes to the waiting work on the session's worker — the work runs the session's
				// tasks while it waits — and the call then settles on that work: it goes on from where it asked.
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				const std::string requestId = std::string(params.GetValue<wxString>(ibProtocol::ibRequest::Id).utf8_str());
				const ibDataNode* found = params.FindChild(ibProtocol::ibRequest::Response);
				const ibDataNode response = found != nullptr ? *found : ibDataNode();
				bool responded = false;
				if (frame != nullptr && session != nullptr)
					session->Submit([frame, &requestId, &response, &responded]() { responded = frame->Respond(requestId, response); }).get();
				if (!responded)
					refuse(ibProtocol::ibRefusal::NotFound, wxT("no such request is waiting"));
				else
					done = Run(*client, nullptr, since, result, refusal, error);
			}
			else if (method == ibProtocol::ibMethod::Fetch) {
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				const ibFormID controlId = params.GetValue<std::int32_t>(ibProtocol::ibFetch::Control);
				const ibDataNode* found = params.FindChild(ibProtocol::ibFetch::Request);
				const ibDataNode fetchRequest = found != nullptr ? *found : ibDataNode();
				const wxString formKey = params.GetValue<wxString>(ibProtocol::ibFetch::Form);
				// A TAB'S OWN VIEW, named by the tab — a spreadsheet document's cells, not a control's of a form.
				const std::int32_t tabId = params.GetValue<std::int32_t>(ibProtocol::ibFetch::Tab);
				if (frame != nullptr && session != nullptr) {
					session->Submit([frame, controlId, &fetchRequest, &formKey, tabId, &result, &done]() {
						if (tabId != 0) {
							ibClientChildFrame* const tab = frame->Tab(frame->FindTab(tabId));
							ibView* const view = tab != nullptr ? tab->GetView() : nullptr;
							done = view != nullptr && view->Fetch(fetchRequest, result);
						}
						else {
							ibValueForm* const form = FormOfCall(frame, formKey);
							ibValueFrame* const control = form != nullptr ? form->FindControlByID(controlId) : nullptr;
							done = control != nullptr && control->Fetch(fetchRequest, result);
						}
					}).get();
				}
				if (!done)
					refuse(ibProtocol::ibRefusal::NotFound, wxT("nothing to fetch there"));
			}
			else if (method == ibProtocol::ibMethod::Presentation) {
				// A VALUE AS A PERSON READS IT — formatted here, where the configuration is (a reference's name is
				// its), on the client's session: a value the client holds itself (a date from its calendar, a value an
				// answer carried) is shown the way the server shows any other.
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				const ibDataNode* const value = params.FindChild(ibProtocol::ibPresentation::Value);
				const wxString format = params.GetValue<wxString>(ibProtocol::ibPresentation::Format);
				const ibMetaData* const metaData = session != nullptr ? session->GetMetaData() : nullptr;
				if (session == nullptr)
					refuse(ibProtocol::ibRefusal::NoSession, wxT("the client has no session"));
				else if (value == nullptr)
					refuse(ibProtocol::ibRefusal::BadParameter, wxT("a presentation needs a Value"));
				else if (metaData == nullptr)
					refuse(ibProtocol::ibRefusal::Failed, wxT("the session has no configuration"));
				else {
					wxString text;
					session->Submit([metaData, value, &format, &text, &done, &refuse]() {
						try {
							const ibValue presented = metaData->Deserialize(*value);
							// Two statements, not `?:` — ibString and wxString convert both ways, and clang refuses to pick.
							if (format.IsEmpty())
								text = presented.GetString();
							else
								text = ibFormatString::Parse(format).Apply(presented);
							done = true;
						}
						catch (const ibCoreException& err) {
							refuse(ibProtocol::ibRefusal::BadParameter, err.GetErrorDescription());
						}
					}).get();
					if (done)
						result.SetValue(ibProtocol::ibPresentation::Text, text);
				}
			}
			else if (method == ibProtocol::ibMethod::Picture) {
				// THE PICTURES OF THOSE IDS — an answer names a picture by its id alone (ibBackendPicture::SendPicture), and a
				// client that has not got it asks for it here, several at once: a backend one by its number, a configuration's
				// by the guid before its version (`<guid>#<version>`), of the session's configuration; made on the
				// session's worker. One there is none of comes back empty — the client keeps that too, and does not ask
				// again.
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				const ibDataValue* const asked = params.FindField(ibProtocol::ibPicture::Pictures);
				if (session == nullptr)
					refuse(ibProtocol::ibRefusal::NoSession, wxT("the client has no session"));
				else if (asked == nullptr || asked->Kind() != ibDataKind::Array)
					refuse(ibProtocol::ibRefusal::BadParameter, wxT("pictures are asked for by a list of their ids"));
				else {
					session->Submit([&session, asked, &result]() {
						const ibMetaData* const metaData = session->GetMetaData();
						ibDataNode& pictures = result.Child(ibProtocol::ibPicture::Pictures);
						for (const ibDataValue& id : asked->AsArray()) {
							const wxString text = id.AsString();
							unsigned long long number = 0;
							const ibPictureDescription picture = text.ToULongLong(&number)
								? ibPictureDescription(static_cast<ibPictureID>(number)) : ibPictureDescription(ibGuid(text.BeforeFirst(wxT('#'))));
							ibDataNode& sent = pictures.AddChild(0, 0);
							sent.AddField(ibProtocol::ibPicture::Id, id);
							sent.SetValue(ibProtocol::ibPicture::Picture, ibBackendPicture::GetServerPicture(picture, metaData).GetData());
						}
					}).get();
					done = true;
				}
			}
			else if (method == ibProtocol::ibMethod::Upload) {
				// A FILE COMES IN PARTS, each a call of its own, and none is held whole here: the first names the file
				// and is answered with its id, the rest name the id. Into the client's own temporary storage.
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				ibTempStorage* const storage = ibApplicationInstance::GetTempStorage(m_applicationInstance);
				ibMemoryBuffer data;
				const bool decoded = ibBase64::Read(params.GetValue<ibString>(ibProtocol::ibFile::Data), data);
				wxString id = params.GetValue<wxString>(ibProtocol::ibFile::File);
				if (session == nullptr || storage == nullptr) {
					refuse(ibProtocol::ibRefusal::NoSession, wxT("the client has no session"));
				}
				else if (!decoded) {
					refuse(ibProtocol::ibRefusal::BadParameter, wxT("Data is not base64"));
				}
				else if (data.GetDataLen() > ibTempStorage::kPartSize) {
					refuse(ibProtocol::ibRefusal::BadParameter,
						wxString::Format(wxT("a part is at most %u bytes"), static_cast<unsigned int>(ibTempStorage::kPartSize)));
				}
				else {
					const ibGuid& owner = session->Identity().m_guid;
					const wxString name = params.GetValue<wxString>(ibProtocol::ibFile::Name);
					const bool made = id.IsEmpty();
					if (made)
						id = storage->Create(owner, name);
					else if (storage->GetName(owner, id).IsEmpty())
						id.clear();
					if (id.IsEmpty() && !made)
						refuse(ibProtocol::ibRefusal::NotFound, wxT("no such file"));
					else if (id.IsEmpty() && name.IsEmpty())
						refuse(ibProtocol::ibRefusal::BadParameter, wxT("a new file needs a Name"));
					else if (id.IsEmpty())
						refuse(ibProtocol::ibRefusal::Failed, wxT("the file was not made"));
					else if (data.GetDataLen() != 0 && !storage->WritePart(owner, id, storage->PartCount(owner, id), data.GetData(), data.GetDataLen()))
						refuse(ibProtocol::ibRefusal::Failed, wxT("the part was not written"));
					else {
						result.SetValue(ibProtocol::ibFile::File, id);
						done = true;
					}
				}
			}
			else if (method == ibProtocol::ibMethod::Download) {
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				const ibTempStorage* const storage = ibApplicationInstance::GetTempStorage(m_applicationInstance);
				const wxString id = params.GetValue<wxString>(ibProtocol::ibFile::File);
				const std::int32_t part = params.GetValue<std::int32_t>(ibProtocol::ibFile::Part);
				ibMemoryBuffer data;
				if (session == nullptr || storage == nullptr) {
					refuse(ibProtocol::ibRefusal::NoSession, wxT("the client has no session"));
				}
				else {
					const ibGuid& owner = session->Identity().m_guid;
					const wxString name = storage->GetName(owner, id);
					const int parts = storage->PartCount(owner, id);
					// An empty file has no parts, and its part 0 is nothing — it is answered, empty.
					if (name.IsEmpty())
						refuse(ibProtocol::ibRefusal::NotFound, wxT("no such file"));
					else if ((part != 0 || parts != 0) && !storage->ReadPart(owner, id, part, data))
						refuse(ibProtocol::ibRefusal::NotFound, wxT("no such part"));
					else {
						result.SetValue(ibProtocol::ibFile::Name, name);
						result.SetValue(ibProtocol::ibFile::Part, part);
						result.SetValue(ibProtocol::ibFile::Parts, static_cast<std::int32_t>(parts));
						ibString text;
						ibBase64::Append(text, data.GetData(), data.GetDataLen());
						result.SetValue(ibProtocol::ibFile::Data, text);
						done = true;
					}
				}
			}
			else if (method == ibProtocol::ibMethod::Open) {
				// THE DOC MANAGER OPENS IT, as the desktop's File → Open: the template the file's name says, a document
				// of it reading the file by its id (ibDocument::DoOpenDocument). Refused before anything is done when
				// the client has no such file, or nothing its application has reads it — asked on the session, whose
				// temporary storage the file is in.
				const wxString id = params.GetValue<wxString>(ibProtocol::ibFile::File);
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				if (frame == nullptr || session == nullptr) {
					refuse(ibProtocol::ibRefusal::NoSession, wxT("the client has no session"));
				}
				else if (params.FindField(ibProtocol::ibFile::File) == nullptr) {
					// No file: a NEW document, as the desktop's File → New with a template picked — the template that
					// opens the Extension.
					const wxString extension = params.GetValue<wxString>(ibProtocol::ibFile::Extension);
					ibDocTemplate* temp = nullptr;
					session->Submit([frame, &extension, &temp, &refuse]() {
						// Asked of the templates by the extension itself — FindTemplateForPath reads a path as a temporary
						// file's id.
						for (ibDocTemplate* const candidate : frame->GetDocumentManager()->GetTemplatesVector()) {
							if (!extension.IsEmpty() && candidate->IsVisible() && candidate->FileMatchesTemplate(wxT("*.") + extension)) {
								temp = candidate;
								break;
							}
						}
						if (temp == nullptr)
							refuse(ibProtocol::ibRefusal::NotFound, wxString::Format(wxT("nothing here makes '%s'"), extension));
					}).get();
					if (refusal == ibProtocol::ibRefusal::None)
						done = Run(*client, [temp](ibClientFrame*) {
							if (ibDocument* const document = temp->CreateDocument(wxEmptyString, ibDOC_NEW)) {
								document->SetDocumentName(temp->GetDocumentName());
								if (!document->OnNewDocument())
									document->DeleteAllViews();
							}
						}, since, result, refusal, error);
				}
				else {
					session->Submit([frame, &id, &refuse]() {
						const ibTempFile file(id);
						const ibDocTemplate* const temp = file.IsOpened() ? frame->GetDocumentManager()->FindTemplateForPath(id) : nullptr;
						if (!file.IsOpened())
							refuse(ibProtocol::ibRefusal::NotFound, wxT("no such file"));
						else if (temp == nullptr || !temp->IsVisible())
							refuse(ibProtocol::ibRefusal::NotFound, wxString::Format(wxT("nothing here opens '%s'"), file.GetName()));
					}).get();
					if (refusal == ibProtocol::ibRefusal::None)
						done = Run(*client, [id](ibClientFrame* frame) { frame->GetDocumentManager()->CreateDocument(id); }, since,
							result, refusal, error);
				}
			}
			else if (method == ibProtocol::ibMethod::Command) {
				// One of the commands the client's application offers, on the active tab's document — refused before
				// anything is done when it offers no such command, or the document does not take it now (asked on
				// the session, where the document is).
				const ibDocCommand command = static_cast<ibDocCommand>(params.GetValue<std::int32_t>(ibProtocol::ibFrame::Command));
				const wxString name = params.GetValue<wxString>(ibProtocol::ibFrame::Name);   // Save as's: the file's
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				bool enabled = false;
				if (frame == nullptr || session == nullptr)
					refuse(ibProtocol::ibRefusal::NoSession, wxT("the client has no session"));
				else if (frame->GetDocumentManager()->GetCommands().count(command) == 0)
					refuse(ibProtocol::ibRefusal::NotFound, wxT("no such command"));
				else {
					session->Submit([frame, command, &enabled]() { enabled = frame->IsCommandEnabled(command); }).get();
					if (!enabled)
						refuse(ibProtocol::ibRefusal::NotNow, wxT("the active document does not take this command now"));
					else if (command == ibDocCommand::SaveAs && name.IsEmpty())
						refuse(ibProtocol::ibRefusal::BadParameter, wxT("Save as needs a Name"));
					else
						done = Run(*client, [command, name](ibClientFrame* frame) { frame->DoCommand(command, name); }, since, result,
							refusal, error);
				}
			}
			}
		}
	}

	// A refusal always says why — one that slipped through unnamed is the server's failure, never «nothing».
	if (!done && refusal == ibProtocol::ibRefusal::None)
		refuse(ibProtocol::ibRefusal::Failed, error.IsEmpty() ? wxString(wxT("the call was not done")) : error);
	return done;
}

bool ibClientHost::Debug(Client& client, const ibDataNode& params, ibDataNode& result, ibProtocol::ibRefusal& refusal,
	wxString& error, const void* connection)
{
	const auto refuse = [&refusal, &error](ibProtocol::ibRefusal why, const wxString& text) { refusal = why; error = text; };
	const ibProtocol::ibDebug::ibCommand command = static_cast<ibProtocol::ibDebug::ibCommand>(params.GetValue<std::int32_t>(ibProtocol::ibDebug::Command));

	// The frame's debugger, the designer's — a client of another mode has none, and nothing of this is its.
	ibClientFrame* const frame = client.instance->GetFrame();
	ibClientDebugger* const debugger = frame != nullptr ? frame->GetDebugger() : nullptr;
	if (debugger == nullptr) {
		refuse(ibProtocol::ibRefusal::NotFound, wxT("the client has no debugger"));
		return false;
	}

	// Who holds the client — its login connection, as every call of it is let in (MayCall).
	const bool holder = MayCall(client.connectionless, client.connection.load(std::memory_order_acquire),
		client.resumeDeadlineMs.load(std::memory_order_acquire), connection);

	if (command == ibProtocol::ibDebug::ibCommand::Channel) {
		// THE KEY, drawn for the one who holds the client — what binds a connection to it is never given to whoever names it.
		// Its SHA-256 is kept until Listen takes it; the key itself goes on this answer, once.
		if (!holder) {
			refuse(ibProtocol::ibRefusal::NoSession, wxT("the client is not connected"));
			return false;
		}
		const std::vector<unsigned char> raw = ibSessionToken::Generate();
		unsigned char digest[ibSessionToken::kDigest];
		if (raw.size() != ibSessionToken::kBytes || !ibSessionToken::Hash(raw.data(), raw.size(), digest)) {
			refuse(ibProtocol::ibRefusal::Failed, wxT("the key could not be drawn"));
			return false;
		}
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			std::memcpy(client.channelDigest, digest, ibSessionToken::kDigest);
			client.hasChannelKey = true;
		}
		// …ending in this server's route, as the token does: the second connection is carried to the same server.
		result.SetValue(ibProtocol::ibDebug::Channel,
			ibSessionToken::WithRoute(ibSessionToken::Hex(raw.data(), raw.size()), m_route));
		return true;
	}

	if (command == ibProtocol::ibDebug::ibCommand::Listen) {
		// THE KEY PRESENTED, on the connection it binds — compared as its digest, in constant time, and taken: a key binds
		// once. A connection without a notifier is bound all the same, and is told nothing.
		unsigned char raw[ibSessionToken::kBytes];
		unsigned char digest[ibSessionToken::kDigest];
		wxString key;
		unsigned route = 0;
		ibSessionToken::SplitRoute(params.GetValue<wxString>(ibProtocol::ibDebug::Channel), key, route);
		const bool read = connection != nullptr
			&& ibSessionToken::ParseHex(key, raw)
			&& ibSessionToken::Hash(raw, ibSessionToken::kBytes, digest);
		bool bound = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (read && client.hasChannelKey && ibSessionToken::DigestEqual(client.channelDigest, digest)) {
				client.hasChannelKey = false;
				client.debugConnection.store(connection, std::memory_order_release);
				bound = true;
			}
		}
		if (!bound)
			refuse(ibProtocol::ibRefusal::LoginRefused, wxT("the channel was refused"));
		return bound;
	}

	// …the rest by the connection the debugger listens on, or by the client's own.
	if (!holder && (connection == nullptr || client.debugConnection.load(std::memory_order_acquire) != connection)) {
		refuse(ibProtocol::ibRefusal::NoSession, wxT("the client is not connected"));
		return false;
	}
	client.instance->Touch();
	return debugger->Command(command, params, result, refusal, error);
}

//*******************************************************************************************
//*                                Running the client's work                                *
//*******************************************************************************************

bool ibClientHost::Run(Client& client, std::function<void(ibClientFrame*)> work, std::int32_t since, ibDataNode& result,
	ibProtocol::ibRefusal& refusal, wxString& error)
{
	std::shared_ptr<ibSession> session = client.instance->ShareSession();
	if (session == nullptr) {
		refusal = ibProtocol::ibRefusal::NoSession;
		error = wxT("the client has no session");
		return false;
	}

	// THE CALL'S PARTS, timed — the work (the script, the handlers), the frame drawn, the patch taken: where a slow call
	// spends its time, said beside the call's own line (a debug build's journal).
	const std::int64_t began = NowMs();
	client.answering.store(true, std::memory_order_release);

	if (work) {
		// ⭐ WHILE THE PERSON IS ASKED SOMETHING, a call still runs — inside the work that waits, which runs this
		// session's tasks: the desktop's modal loop, which runs the events of the windows over it (a choice form a
		// window opened). Which windows the person can reach while a window waits is the client's to say: its modal.
		// The call settles on its own work; the frame it is answered with is drawn on the same worker, behind the work
		// that asked.
		ibClientInstance* const instance = client.instance.get();
		client.unsettled = session->Submit([instance, work]() { work(instance->GetFrame()); }).share();
	}
	Settle(client);
	const std::int64_t settled = NowMs();

	// The frame drawn on the worker — inside the waiting work when it waits for a response (it runs the session's
	// tasks), after it otherwise; the tabs that closed go first — a choice form closed while its window waits among them.
	ibClientInstance* const instance = client.instance.get();
	ibDataNode drawn, events;
	bool drew = false;
	try {
		session->Submit([&client, instance, &drawn, &events, &drew]() {
			client.answering.store(false, std::memory_order_release);
			ibClientFrame* const frame = instance->GetFrame();
			if (frame == nullptr)
				return;
			frame->DrainPendingCloses();
			DrawFrame(frame, drawn, events);
			drew = true;
		}).get();
	}
	catch (...) {
		client.answering.store(false, std::memory_order_release);
		refusal = ibProtocol::ibRefusal::Failed;
		error = wxT("the frame could not be drawn");
		return false;
	}
	if (!drew)
		return true;
	const std::int64_t framed = NowMs();

	// ⭐ THE ANSWER IS NUMBERED, and a client that names the frame it holds (Since — the last number it was given)
	// is answered with the patch from it to this one; any other gets the frame whole. The events go beside either:
	// they are not state, and a message said twice is two messages, never «unchanged».
	//
	// ⭐ (protocol 2) A TAB'S VIEW IS PATCHED FROM ITS OWN — from the view the client was last sent for the tab active
	// now, not from the last frame's, which showed another tab when the client switched: back on a tab it is sent what
	// changed there, not the whole form again (8 KB → 153 B, 2026-10-06). A tab the client was never sent the view of
	// is patched from the last frame's, as in 1 — forms are alike, and the difference of two is a part of either
	// (25 KB for a list, not its 47). The frame the patch is taken from is the last one sent with that view in its
	// place; the client puts its own copy in place the same way.
	const std::int32_t active = drawn.GetValue<std::int32_t>(ibProtocol::ibFrame::ActiveTab);
	ibDataNode base;
	const bool fromLast = since != 0 && since == client.frame;
	if (fromLast && client.protocol >= 2) {
		const auto kept = client.views.find(active);
		const bool ownView = kept != client.views.end();
		for (const auto& field : client.sent.Fields())
			base.AddField(field.first, field.second);
		for (const auto& property : client.sent.Properties())
			if (!ownView || property.first != ibProtocol::ibFrame::View)
				base.SetProperty(property.first, property.second);
		if (ownView)
			base.SetProperty(ibProtocol::ibFrame::View, ibDataValue::Child(kept->second));
	}
	ibDataNode patch;
	const bool patched = fromLast && ibClientFramePatch(client.protocol >= 2 ? base : client.sent, drawn, patch);
	ibJournalInfo(wxT("client"), wxT("  work %lld ms, frame drawn %lld ms, %s %lld ms"), settled - began, framed - settled,
		patched ? wxT("patched") : wxT("sent whole"), NowMs() - framed);
	if (patched) {
		result.SetValue(ibProtocol::ibLogin::Since, since);
		result.Child(ibProtocol::ibFrame::Patch) = patch;
	}
	else {
		for (const auto& field : drawn.Fields())
			result.AddField(field.first, field.second);
		for (const auto& property : drawn.Properties())
			result.SetProperty(property.first, property.second);
	}
	for (const auto& field : events.Fields())
		result.AddField(field.first, field.second);
	for (const auto& property : events.Properties())
		result.SetProperty(property.first, property.second);
	result.SetValue(ibProtocol::ibFrame::Frame, ++client.frame);

	// …and the views kept as the client keeps them: this tab's is the one just sent; a frame sent whole begins the
	// store again; a tab gone takes its view with it.
	if (client.protocol >= 2) {
		if (!patched)
			client.views.clear();
		if (const ibDataValue* const view = drawn.FindProperty(ibProtocol::ibFrame::View))
			client.views[active] = view->AsChild();
		else
			client.views.erase(active);
		std::set<std::int32_t> open;
		if (const ibDataNode* const tabs = drawn.FindChild(ibProtocol::ibFrame::Tabs))
			for (const ibDataNode& tab : tabs->Children())
				open.insert(static_cast<std::int32_t>(tab.GetMetaId()));
		for (auto it = client.views.begin(); it != client.views.end();)
			it = open.count(it->first) != 0 ? std::next(it) : client.views.erase(it);
	}

	client.sent = std::move(drawn);
	return true;
}

void ibClientHost::Settle(Client& client)
{
	if (!client.unsettled.valid())
		return;
	for (;;) {
		if (client.unsettled.wait_for(std::chrono::milliseconds(20)) == std::future_status::ready) {
			try {
				client.unsettled.get();
			}
			catch (const std::exception& err) {
				ibJournalInfo(wxT("client"), wxT("the client's work ended with an exception: %s"), wxString::FromUTF8(err.what()));
			}
			catch (...) {
				// A failure of the work reported itself where it happened — to the frame's messages, which the
				// client receives with the frame.
				ibJournalInfo(wxT("client"), wxT("the client's work ended with an exception"));
			}
			client.unsettled = std::shared_future<void>();
			return;
		}
		ibClientFrame* const frame = client.instance->GetFrame();
		if (frame != nullptr && frame->HasPendingRequest())
			return;
	}
}

void ibClientHost::DrawFrame(ibClientFrame* frame, ibDataNode& state, ibDataNode& events)
{
	// Its parts timed — the menus, the tabs, the active view: what of a frame takes the time (a debug build's journal).
	const std::int64_t began = NowMs();
	state.SetValue(ibProtocol::ibFrame::Title, frame->GetTitle());
	state.SetValue(ibProtocol::ibFrame::Status, frame->GetStatusText());
	// …and the base's assistant access by where it listens, empty while it does not — the bottom bar's lamp, the
	// McpServer schema's Endpoint.
	const ibApplicationInstance* const applicationInstance = frame->GetSession()->GetApplicationInstance();
	const ibMetaData* const metaData = frame->GetSession()->GetMetaData();
	const ibMcpServer* const assistant = applicationInstance != nullptr
		? ibApplicationInstance::GetMcpServer(applicationInstance) : nullptr;
	state.SetValue(ibProtocol::ibFrame::Endpoint, assistant != nullptr ? assistant->GetEndpoint() : wxString());
	// The language the session reads text in — drawn on its worker, so its own: a caption sent whole (an inspector's
	// Synonym) is read by the client in it, as the desktop's grid read it in the configuration's.
	state.SetValue(ibProtocol::ibFrame::Language, ibBackendLocalization::GetUserLanguage());
	// The active tab by its id, as every tab is named on the wire; 0 — none.
	const ibClientChildFrame* const active = frame->Tab(frame->ActiveTab());
	state.SetValue(ibProtocol::ibFrame::ActiveTab, active != nullptr ? active->GetId() : 0);

	// What the client's application offers besides its tabs — the schemas its doc manager registered.
	std::vector<ibDataValue> schemas;
	for (const ibProtocol::ibSchema::ibKind kind : frame->GetDocumentManager()->GetSchemaKinds())
		schemas.push_back(ibDataValue::Int(static_cast<std::int64_t>(kind)));
	state.AddField(ibProtocol::ibFrame::Schemas, ibDataValue::Array(schemas));

	// …and the documents it opens from a file — the templates its doc manager registered that a person picks
	// (the desktop's File → Open filter and File → New): an uploaded file opens by the one its name matches, a new one is
	// made by its extension. The client registers these and no others, each with its picture by its id.
	ibDataNode& templates = state.Child(ibProtocol::ibFrame::Templates);
	for (const ibDocTemplate* const temp : frame->GetDocumentManager()->GetTemplatesVector()) {
		if (!temp->IsVisible())
			continue;
		ibDataNode& node = templates.AddChild(0, 0);
		node.SetValue(ibProtocol::ibFile::Title, temp->GetDescription());
		node.SetValue(ibProtocol::ibFile::Mask, temp->GetFileFilter());
		node.SetValue(ibProtocol::ibFile::Extension, temp->GetDefaultExtension());
		node.SetValue(ibProtocol::ibFile::Picture, ibBackendPicture::SendPicture(temp->GetPictureID()));
		if ((temp->GetFlags() & ibTEMPLATE_ONLY_OPEN) != 0)
			node.SetValue(ibProtocol::ibFile::OnlyOpen, true);
	}

	// …and its menus — the application's, beside the File and Edit of the client's own doc manager.
	DrawMenu(frame, frame->GetMenu().GetItems(), state.Child(ibProtocol::ibFrame::Menu));
	const std::int64_t menus = NowMs();

	// …and the mode's own part of it, beside the tabs — the designer's configuration tree.
	frame->OnDraw(state);
	const std::int64_t frameDrawn = NowMs();

	// ⚠ THE ACTIVE VIEW BEFORE THE TABS: drawing it puts its form's caption on its document (ibVisualHost::
	// UpdateVisualHost → SetCaption) — the tab's title. Drawn after them, a title changed by this answer (the form editor's
	// Apply) reached the tab one frame late.
	if (ibClientChildFrame* const tab = frame->Tab(frame->ActiveTab())) {
		if (ibView* const view = tab->GetView())
			view->OnDraw(state.Child(ibProtocol::ibFrame::View));
	}
	const std::int64_t viewDrawn = NowMs();

	// Each tab by its id (NodeId) — what activate and close name it by; their order is the frame's. With the commands its
	// document takes now: the client's doc manager enables its File and Edit by them, and asks nothing.
	ibDataNode& tabs = state.Child(ibProtocol::ibFrame::Tabs);
	for (std::size_t i = 0; i < frame->TabCount(); ++i) {
		ibClientChildFrame* const tab = frame->Tab(i);
		ibDataNode& node = tabs.AddChild(0, tab->GetId());
		node.SetValue(ibProtocol::ibFrame::ibTab::Title, tab->GetTitle());
		if (!tab->GetIcon().IsEmptyPicture())
			node.SetValue(ibProtocol::ibFrame::ibTab::Icon, ibBackendPicture::SendPicture(tab->GetIcon(), metaData));
		if (tab->IsLocked())
			node.SetValue(ibProtocol::ibFrame::ibTab::Locked, true);
		// What it shows takes no edit — the windows opened from it ask the client's configuration door, which reads this.
		if (tab->GetDocument() != nullptr && !tab->GetDocument()->IsEditable())
			node.SetValue(ibProtocol::ibFrame::ibTab::ReadOnly, true);
		// What its view shows, when it is no form — the client draws it with the view of its own it has for that.
		if (const ibView* const view = tab->GetView()) {
			if (view->GetViewKind() != ibProtocol::ibView::ibKind::Form)
				node.SetValue(ibProtocol::ibFrame::ibTab::Kind, static_cast<std::int32_t>(view->GetViewKind()));
		}
		// What its document saves as — the named lines of a file dialog, Title and Mask each, as Templates are — and its
		// file, once it has one: a file of the session's temporary storage the client takes down to where the person keeps
		// it (Save, Save as). Its document as Save as takes it: a form's active grid box's sheet, while it is that.
		if (const ibDocument* const document = frame->GetSaveAsDocument(i)) {
			const wxArrayString lines = wxSplit(document->GetSaveFilter(), wxT('|'));
			if (lines.size() >= 2) {
				ibDataNode& formats = node.Child(ibProtocol::ibFrame::ibTab::Formats);
				for (std::size_t line = 0; line + 1 < lines.size(); line += 2) {
					ibDataNode& format = formats.AddChild(0, 0);
					format.SetValue(ibProtocol::ibFrame::ibTab::Title, lines[line]);
					format.SetValue(ibProtocol::ibFrame::ibTab::Mask, lines[line + 1]);
				}
				if (document->GetDocumentSaved())
					node.SetValue(ibProtocol::ibFrame::ibTab::File, document->GetFilename());
			}
		}
		// The tab whose document owns this one's — the client raises it under that one, and it goes with it.
		if (const ibDocument* const owner = tab->GetDocument() != nullptr ? tab->GetDocument()->GetDocParent() : nullptr) {
			for (std::size_t j = 0; j < frame->TabCount(); ++j) {
				if (frame->Tab(j)->GetDocument() == owner)
					node.SetValue(ibProtocol::ibFrame::ibTab::Parent, frame->Tab(j)->GetId());
			}
		}

		std::vector<ibDataValue> commands;
		for (const auto& command : frame->GetDocumentManager()->GetCommands()) {
			if (frame->IsCommandEnabled(command.first, i))
				commands.push_back(ibDataValue::Int(static_cast<std::int64_t>(command.first)));
		}
		node.AddField(ibProtocol::ibFrame::ibTab::Commands, ibDataValue::Array(commands));
	}

	const std::int64_t tabsDrawn = NowMs();

	// THE EVENTS — what happened since the last answer, not what the frame is: apart, never patched.
	ibDataNode& messages = events.Child(ibProtocol::ibFrame::Messages);
	for (const ibClientFrame::PendingMessage& message : frame->DrainPendingMessages()) {
		ibDataNode& node = messages.AddChild(0, 0);
		node.SetValue(ibProtocol::ibFrame::ibMessage::Level, static_cast<std::int32_t>(message.level));
		node.SetValue(ibProtocol::ibFrame::ibMessage::Text, message.text);
	}
	if (frame->TakeClearPending())
		events.SetValue(ibProtocol::ibFrame::Clear, true);
	// Exit done — the client goes; its session goes when it logs out.
	if (frame->TakeExitPending())
		events.SetValue(ibProtocol::ibFrame::Exit, true);
	// A debugger left its loop — the client's window raised, as the desktop's raised its own.
	if (frame->TakeRaisePending())
		events.SetValue(ibProtocol::ibFrame::Raise, true);
	// The inspector's rows of what a door acted on (ShowProperties) — each by its object's id and tab: the client shows
	// them without reading them again.
	std::vector<ibDataNode> shown = frame->TakeShownProperties();
	if (!shown.empty()) {
		ibDataNode& properties = events.Child(ibProtocol::ibFrame::Properties);
		for (ibDataNode& rows : shown)
			properties.Children().push_back(std::move(rows));
	}

	if (frame->HasPendingRequest()) {
		const ibClientFrame::PendingRequest pending = frame->PeekPendingRequest();
		ibDataNode& request = state.Child(ibProtocol::ibFrame::Request);
		request = pending.request;
		request.SetValue(ibProtocol::ibRequest::Id, wxString::FromUTF8(pending.id.c_str()));
	}

	ibJournalInfo(wxT("client"), wxT("    the frame: menus %lld ms, its own %lld ms, tabs %lld ms, the view %lld ms"),
		menus - began, frameDrawn - menus, tabsDrawn - viewDrawn, viewDrawn - frameDrawn);
}

//*******************************************************************************************
//*                                        The round                                        *
//*******************************************************************************************

void ibClientHost::RoundBody()
{
	for (;;) {
		{
			std::unique_lock<std::mutex> lock(m_roundMutex);
			if (m_roundSignal.wait_for(lock, std::chrono::seconds(1), [this]() { return m_stop; }))
				return;
		}

		std::vector<std::pair<wxString, std::shared_ptr<Client>>> clients;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			for (auto& entry : m_clients)
				clients.emplace_back(entry.first, entry.second);
		}

		const std::int64_t now = NowMs();
		for (auto& entry : clients) {
			ibClientInstance* const instance = entry.second->instance.get();
			const std::int64_t deadline = entry.second->resumeDeadlineMs.load(std::memory_order_acquire);
			// The window, not the idle limit: a detached thin client is ended when Resume has passed, and not before.
			if (deadline != 0 && now >= deadline) {
				RemoveClient(entry.first);
				continue;
			}
			// Only a connectionless client is idle: one whose socket is alive is someone at a window, however long
			// they think; a dropped socket already went through Disconnect (a window or removal).
			const bool idle = deadline == 0 && entry.second->connectionless
				&& now - instance->LastActiveMs() > std::chrono::duration_cast<std::chrono::milliseconds>(kIdleLimit).count();
			if (instance->IsCloseRequested() || idle) {
				RemoveClient(entry.first);
				continue;
			}
			// The forms' due idle handlers, handed to the session — they run where the forms' scripts run, in order
			// with everything else the session does, and not waited for here: so what they throw is said in the task,
			// nobody holding its future (the pool hands an exception to the future only).
			if (std::shared_ptr<ibSession> session = instance->ShareSession()) {
				if (instance->GetFrame() != nullptr) {
					(void)session->Submit([]() {
						try {
							ibFormVisualDocument::RunIdleHandlers();
						}
						catch (const std::exception& err) {
							ibJournalWarning(wxT("client"), wxT("an idle handler ended with an exception: %s"), wxString::FromUTF8(err.what()));
						}
						catch (...) {
							ibJournalWarning(wxT("client"), wxT("an idle handler ended with an exception"));
						}
					});
				}
			}
		}
	}
}
