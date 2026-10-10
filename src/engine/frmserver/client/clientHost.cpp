#include "clientHost.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include <wx/datetime.h>

#include <cstring>

#include <wx/base64.h>

#include "backend/appData.h"
#include "backend/appHost.h"
#include "backend/backend_exception.h"   // ibBackendException — a value the configuration cannot make
#include "core/exception.h"
#include "core/formatString.h"        // ibFormatString — a value presented through Format()'s codes
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaFormObject.h"   // ibBackendCommandItem::Execute
#include "backend/rpc/rpcMessage.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"
#include "backend/temp/tempStorage.h"   // the client's files
#include "backend/utils/sessionToken.h"
#include "backend/utils/sha256.hpp"

#include "frmserver/docView/docManager.h"   // ibDocTemplate — what opens a file
#include "frmserver/visualView/visualHost.h"

#include "clientChildFrame.h"
#include "clientFrame.h"
#include "clientInstance.h"
#include "protocol/protocol.h"
#include "clientPatch.h"

namespace {

std::mutex                                                s_hostsMutex;
std::map<const ibApplicationInstance*, ibClientHost*>     s_hosts;
std::function<void()>                                     s_betweenDetach;

// An instance nobody has heard from for this long is taken down.
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
bool MayCall(bool http, const void* bound, std::int64_t deadline, const void* connection)
{
	if (http)
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
	const ibDataValue* features = params.FindField(wxString::FromUTF8(ibProtocolName::Features));
	if (features == nullptr || features->Kind() != ibDataKind::Array)
		return false;
	const wxString resume = wxString::FromUTF8(ibProtocolName::FeatureResume);
	for (const ibDataValue& item : features->AsArray()) {
		if (item.Kind() == ibDataKind::String && item.AsString() == resume)
			return true;
	}
	return false;
}

void FeedText(ibSHA256& sha, const wxString& text);
void FeedValue(ibSHA256& sha, const ibDataValue& value);

void FeedNode(ibSHA256& sha, const ibDataNode& node)
{
	for (const auto& field : node.Fields()) {
		FeedText(sha, field.first);
		FeedValue(sha, field.second);
	}
}

// utf8_str() does not own its bytes. Copy them out of the wxString before it dies.
void FeedText(ibSHA256& sha, const wxString& text)
{
	const std::string utf(text.utf8_str());
	if (!utf.empty())
		sha.Update(reinterpret_cast<const unsigned char*>(utf.data()), utf.size());
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

void CallDigest(const wxString& method, const ibDataNode& params, unsigned char out[32])
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
	result.SetValue(wxT("Client"), id);
	result.SetValue(wxT("Protocol"), protocol);
	result.AddField(wxT("Features"), ibDataValue::Array(std::vector<ibDataValue>{
		ibDataValue::String(wxString::FromUTF8(ibProtocolName::FeatureResume)) }));
	if (!token.IsEmpty())
		result.SetValue(wxString::FromUTF8(ibProtocolName::Token), token);
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

// THE MENU AS IT STANDS NOW: each item with what it does, a command with its name, key and picture, and whether it is
// enabled now (the desktop's EVT_UPDATE_UI): a command by the active tab's document, a schema by its own right — the
// same questions a call of it is refused by.
void DrawMenu(ibClientFrame* frame, const std::vector<ibClientMenu::Item>& items, ibDataNode& node)
{
	const ibDocManager* const manager = frame->GetDocumentManager();
	for (const ibClientMenu::Item& item : items) {
		ibDataNode& child = node.AddChild(0, 0);
		if (item.kind == ibClientMenuItemKind::Separator) {
			child.SetValue(wxT("Separator"), true);
			continue;
		}

		bool enabled = true;
		if (item.kind == ibClientMenuItemKind::Command) {
			const auto found = manager->GetCommands().find(item.command);
			child.SetValue(wxT("Command"), static_cast<s32>(item.command));
			if (found != manager->GetCommands().end()) {
				child.SetValue(wxT("Title"), found->second.title);
				if (!found->second.shortcut.IsEmpty())
					child.SetValue(wxT("Shortcut"), found->second.shortcut);
				if (found->second.picture != ibClientStockPicture::None)
					child.SetValue(wxT("StockPicture"), static_cast<s32>(found->second.picture));
			}
			enabled = found != manager->GetCommands().end() && frame->IsCommandEnabled(item.command);
		}
		else if (item.kind == ibClientMenuItemKind::Schema) {
			child.SetValue(wxT("Schema"), static_cast<s32>(item.schema));
			child.SetValue(wxT("Title"), item.title);
			enabled = frame->IsSchemaAllowed(item.schema);
		}
		else {
			child.SetValue(wxT("Title"), item.title);
			DrawMenu(frame, item.items, child);
		}
		if (enabled)
			child.SetValue(wxT("Enabled"), true);
	}
}

// The form a call is for: the one it names by its key (a cell of the start page), the active tab's otherwise.
ibValueForm* FormOfCall(const ibClientFrame* frame, const wxString& key)
{
	return key.IsEmpty() ? frame->ActiveForm() : ibFormVisualDocument::FindFormByGuid(ibGuid(key));
}

} // namespace

ibClientHost::ibClientHost(ibApplicationInstance* applicationInstance)
	: m_applicationInstance(applicationInstance)
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
	std::map<wxString, std::shared_ptr<Client>> clients;
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

std::shared_ptr<ibClientHost::Client> ibClientHost::FindClient(const wxString& id) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto it = m_clients.find(id);
	return it != m_clients.end() ? it->second : nullptr;
}

void ibClientHost::RemoveClient(const wxString& id)
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

bool ibClientHost::ResumeByToken(const ibDataNode& params, ibDataNode& result, ibProtocolRefusal& refusal, wxString& error,
	const void* connection)
{
	unsigned char raw[ibSessionToken::kBytes];
	unsigned char digest[ibSessionToken::kDigest];
	const wxString token = params.GetValue<wxString>(wxString::FromUTF8(ibProtocolName::Token));
	const auto refuse = [&refusal, &error](const wxString& text) {
		refusal = ibProtocolRefusal::LoginRefused;
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

bool ibClientHost::RememberToken(Client& client, ibDataNode& result, ibProtocolRefusal& refusal, wxString& error)
{
	const std::vector<unsigned char> raw = ibSessionToken::Generate();
	if (raw.size() != ibSessionToken::kBytes) {
		refusal = ibProtocolRefusal::Failed;
		error = wxT("the token could not be drawn");
		return false;
	}
	unsigned char digest[ibSessionToken::kDigest];
	if (!ibSessionToken::Hash(raw.data(), raw.size(), digest)) {
		refusal = ibProtocolRefusal::Failed;
		error = wxT("the token could not be drawn");
		return false;
	}
	const wxString hash = ibSessionToken::Hex(digest, ibSessionToken::kDigest);
	bool wrote = false;
	try {
		if (std::shared_ptr<ibSession> session = client.instance->ShareSession()) {
			session->Submit([&]() {
				ibDatabaseQueryBuilder q;
				q.Execute(ibUpdate(session_table, {
					{ wxT("tokenHash"), ibConst(ibValue(hash)) },
				}, ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("session")), ibConst(ibValue(client.instance->Id())))));
				wrote = true;
			}).get();
		}
	}
	catch (const ibCoreException&) {
		wrote = false;
	}
	catch (...) {
		wrote = false;
	}
	if (!wrote) {
		refusal = ibProtocolRefusal::Failed;
		error = wxT("the token could not be stored");
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::memcpy(client.tokenDigest, digest, ibSessionToken::kDigest);
		client.hasToken = true;
	}
	// The plaintext, once, on this answer. It is not kept.
	result.SetValue(wxString::FromUTF8(ibProtocolName::Token), ibSessionToken::Hex(raw.data(), raw.size()));
	return true;
}

void ibClientHost::SetBetweenDetach(std::function<void()> probe)
{
	s_betweenDetach = std::move(probe);
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
				// deadline, and only a client marked http at login is callable with no connection.
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
	// Outside the host mutex: the probe calls back in (a test's call from another socket).
	if (s_betweenDetach && !detaching.empty())
		s_betweenDetach();
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

void ibClientHost::SetNotifier(const void* connection, std::function<void(const wxString& text)> notify)
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
	std::function<void(const wxString& text)> notify;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const auto& [clientId, client] : m_clients) {
			if (client->instance.get() != instance)
				continue;
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

//*******************************************************************************************
//*                                        The door                                         *
//*******************************************************************************************

wxString ibClientHost::Call(const wxString& text, const void* connection, const wxString& address)
{
	ibRpcRequest request;
	wxString error;
	if (!ibRpcParseRequest(text, request, error))
		return ibRpcWriteError(ibDataValue(), ibRpcError::Parse, error);

	const ibProtocolMethod method = ibProtocolMethodFromName(request.m_method.utf8_str());
	if (method == ibProtocolMethod::Unknown) {
		return request.WantsAnswer()
			? ibRpcWriteError(request.m_id, ibRpcError::MethodNotFound, wxString::Format(wxT("no method named '%s'"), request.m_method))
			: wxString();
	}

	// THE LAST ANSWER OF A CLIENT THAT OPTED IN. The same id with the same method and params is the saved text,
	// and it is not run again. A different call under that id, or an older id, is refused and not run. A login
	// does not take the slot: the lost call is retried after login {Token}, with its own id. Who may call is
	// decided before the slot is read, so a detached client does not hand its last answer to whoever names it.
	const bool tokenLogin = method == ibProtocolMethod::Login
		&& request.m_params.FindField(wxString::FromUTF8(ibProtocolName::User)) == nullptr
		&& request.m_params.FindField(wxString::FromUTF8(ibProtocolName::Token)) != nullptr;
	struct Slot {
		std::shared_ptr<Client> client;
		bool                    owner = false;   // set only by the call that sets working
		~Slot()
		{
			if (!owner || !client)
				return;
			std::lock_guard<std::mutex> lock(client->lock);
			client->working = false;
			client->replyWait.notify_all();
		}
	} slot;
	unsigned char callDigest[32] = {};
	if (request.WantsAnswer() && !tokenLogin && method != ibProtocolMethod::Login) {
		slot.client = FindClient(request.m_params.GetValue<wxString>(wxString::FromUTF8(ibProtocolName::Client)));
		if (slot.client) {
			std::unique_lock<std::mutex> lock(slot.client->lock);
			if (!MayCall(slot.client->http, slot.client->connection.load(std::memory_order_acquire),
				slot.client->resumeDeadlineMs.load(std::memory_order_acquire), connection)) {
				lock.unlock();
				slot.client.reset();
				return ibRpcWriteError(request.m_id, static_cast<s32>(ibProtocolRefusal::NoSession), wxT("the client is not connected"));
			}
			if (slot.client->dedupe) {
				CallDigest(request.m_method, request.m_params, callDigest);
				for (;;) {
					if (slot.client->working) {
						slot.client->replyWait.wait(lock);
						continue;
					}
					if (slot.client->hasReply && IdLess(request.m_id, slot.client->replyId)) {
						lock.unlock();
						slot.client.reset();
						return ibRpcWriteError(request.m_id, static_cast<s32>(ibProtocolRefusal::BadParameter),
							wxT("the call id is older than the last one answered"));
					}
					if (slot.client->hasReply && slot.client->replyId == request.m_id) {
						const bool same = slot.client->hasReply == true
							&& std::memcmp(callDigest, slot.client->replyDigest, sizeof(callDigest)) == 0;
						if (same) {
							const wxString saved = slot.client->reply;
							return saved;
						}
						lock.unlock();
						slot.client.reset();
						return ibRpcWriteError(request.m_id, static_cast<s32>(ibProtocolRefusal::BadParameter),
							wxT("the call id was already answered for another call"));
					}
					slot.client->working = true;
					slot.client->workingId = request.m_id;
					slot.owner = true;
					break;
				}
			}
			else
				slot.client.reset();
		}
	}

	ibDataNode result;
	ibProtocolRefusal refusal = ibProtocolRefusal::None;
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

	if (!request.WantsAnswer())
		return wxString();

	// …and the answer written — its text, the part of a call that grows with the frame.
	const std::int64_t writing = NowMs();
	const wxString answer = done ? ibRpcWriteResult(request.m_id, result) : ibRpcWriteError(request.m_id, static_cast<s32>(refusal), error);
	ibJournalInfo(wxT("client"), wxT("  answer written %lld ms, %zu chars"), NowMs() - writing, answer.length());
	// A detached NoSession was not accepted, so it is not the call's answer. Anything else, including a refusal, is.
	// Only the call that owns the slot writes it: a lookup that returned the saved text never did.
	if (slot.owner && slot.client && (done || refusal != ibProtocolRefusal::NoSession)) {
		std::lock_guard<std::mutex> lock(slot.client->lock);
		slot.client->hasReply = true;
		slot.client->replyId = request.m_id;
		std::memcpy(slot.client->replyDigest, callDigest, sizeof(callDigest));
		slot.client->reply = answer;
	}
	return answer;
}

bool ibClientHost::Call(ibProtocolMethod method, const ibDataNode& params, ibDataNode& result, ibProtocolRefusal& refusal,
	wxString& error, const void* connection, const wxString& address)
{
	bool done = false;
	refusal = ibProtocolRefusal::None;
	error.clear();
	// Why, and its text — said together by every refusal below.
	const auto refuse = [&refusal, &error](ibProtocolRefusal why, const wxString& text) { refusal = why; error = text; };

	if (method == ibProtocolMethod::Unknown) {
		refuse(ibProtocolRefusal::NotFound, wxT("no such method"));
	}
	else if (method == ibProtocolMethod::Login) {
		const bool byToken = params.FindField(wxString::FromUTF8(ibProtocolName::User)) == nullptr
			&& params.FindField(wxString::FromUTF8(ibProtocolName::Token)) != nullptr;
		if (byToken) {
			// The same client. No frame is drawn, and the frame number stays, so the next frame {Since} is a patch.
			done = ResumeByToken(params, result, refusal, error, connection);
		}
		else {
		// The instance is made and logged in; the start runs as work of its own, so a start that asks the person
		// something returns here with the request — and the client's id, without which it could not respond.
		s32 protocol = 1;
		params.GetValue(wxT("Protocol"), protocol);
		auto client = std::make_shared<Client>();
		const wxString id = ibGuid(ibGuid::newGuid()).str();
		s32 mode = static_cast<s32>(ibProtocolMode::Runtime);
		params.GetValue(wxT("Mode"), mode);
		client->instance = std::make_shared<ibClientInstance>(m_applicationInstance, id, address, static_cast<ibProtocolMode>(mode));
		client->http = connection == nullptr;
		client->connection.store(connection, std::memory_order_relaxed);
		client->protocol = std::min(protocol, ibProtocolVersion);
		if (protocol < 1) {
			refuse(ibProtocolRefusal::BadParameter, wxString::Format(wxT("no protocol version %d"), protocol));
		}
		else if (!client->instance->Login(params.GetValue<wxString>(wxT("User")), params.GetValue<wxString>(wxT("Password")))) {
			refuse(ibProtocolRefusal::LoginRefused, wxT("the login was refused"));
		}
		else {
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
				refuse(ibProtocolRefusal::StartRefused, wxT("the start was refused"));
			}
			if (!done)
				RemoveClient(id);
			else if (!WindowFor(client->connection.load(std::memory_order_acquire), *client->instance)) {
				// No window: no token, and `resume` is not offered. The answer is the frame, as before.
				result.SetValue(wxT("Client"), id);
				result.SetValue(wxT("Protocol"), client->protocol);
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
				OfferResume(result, id, client->protocol, result.GetValue<wxString>(wxString::FromUTF8(ibProtocolName::Token)));
			}
		}
		}
	}
	else if (method == ibProtocolMethod::Logout) {
		const std::shared_ptr<Client> client = FindClient(params.GetValue<wxString>(wxT("Client")));
		if (client == nullptr) {
			done = true;
		}
		else {
			bool allowed = false;
			{
				std::lock_guard<std::mutex> clientLock(client->lock);
				allowed = MayCall(client->http, client->connection.load(std::memory_order_acquire),
					client->resumeDeadlineMs.load(std::memory_order_acquire), connection);
			}
			if (!allowed)
				refuse(ibProtocolRefusal::NoSession, wxT("the client is not connected"));
			else {
				RemoveClient(params.GetValue<wxString>(wxT("Client")));
				done = true;
			}
		}
	}
	else {
		const std::shared_ptr<Client> client = FindClient(params.GetValue<wxString>(wxT("Client")));
		if (client == nullptr) {
			refuse(ibProtocolRefusal::NoSession, wxT("no such client"));
		}
		else {
			std::lock_guard<std::mutex> clientLock(client->lock);
			// Detached, or a socket that is not this client's: the public Client id is not the bearer.
			if (!MayCall(client->http, client->connection.load(std::memory_order_acquire),
				client->resumeDeadlineMs.load(std::memory_order_acquire), connection)) {
				refuse(ibProtocolRefusal::NoSession, wxT("the client is not connected"));
			}
			else {
			client->instance->Touch();
			// The frame the client holds — the number it was last answered with; 0 or absent: none, the frame whole.
			const s32 since = params.GetValue<s32>(wxT("Since"));

			if (method == ibProtocolMethod::Frame) {
				done = Run(*client, nullptr, since, result, refusal, error);
			}
			else if (method == ibProtocolMethod::Schema) {
				// Asked of the doc manager of the client's frame — what its application offers — and asked on the
				// client's session: what a schema lists, and what it acts on, may depend on the person's rights.
				const ibProtocolSchema kind = static_cast<ibProtocolSchema>(params.GetValue<s32>(wxT("Schema")));
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				const ibClientSchema* const schema = frame != nullptr ? frame->GetDocumentManager()->FindSchema(kind) : nullptr;
				if (frame == nullptr || session == nullptr) {
					refuse(ibProtocolRefusal::NoSession, wxT("the client has no session"));
				}
				else if (schema == nullptr) {
					refuse(ibProtocolRefusal::NotFound, wxT("no such schema"));
				}
				else if (params.FindField(wxT("Command")) == nullptr) {
					// Without the right to it, the person gets an error, not the schema.
					session->Submit([schema, &session, &result, &done, &refuse]() {
						if (!schema->AccessRight(*session))
							refuse(ibProtocolRefusal::NoRight, wxT("no right to this schema"));
						else {
							schema->Build(*session, result);
							done = true;
						}
					}).get();
				}
				else {
					// What a person did in it: refused before anything is done, or the work that does it — run as the
					// client's work, so the answer is the frame, with what it opened.
					const s32 command = params.GetValue<s32>(wxT("Command"));
					const ibDataNode* found = params.FindChild(wxT("Args"));
					const ibDataNode args = found != nullptr ? *found : ibDataNode();
					std::function<void()> work;
					session->Submit([schema, &session, command, &args, &work, &refuse, &refusal, &error]() {
						if (!schema->AccessRight(*session))
							refuse(ibProtocolRefusal::NoRight, wxT("no right to this schema"));
						else
							work = schema->Command(*session, command, args, refusal, error);
					}).get();
					if (work)
						done = Run(*client, [work](ibClientFrame*) { work(); }, since, result, refusal, error);
				}
			}
			else if (method == ibProtocolMethod::Execute) {
				// What the configuration does not have is refused here, to the client: an empty frame would say
				// nothing happened.
				const ibMetaID metaId = params.GetValue<s32>(wxT("Command"));
				s32 type = ibInterfaceCommandType::ibInterfaceCommandType_Default;
				params.GetValue(wxT("Type"), type);
				const std::shared_ptr<ibSession> session = client->instance->ShareSession();
				const ibBackendCommandItem* const command = session != nullptr ? FindCommand(*session, metaId) : nullptr;
				if (session == nullptr) {
					refuse(ibProtocolRefusal::NoSession, wxT("the client has no session"));
				}
				else if (command == nullptr) {
					refuse(ibProtocolRefusal::NotFound, wxString::Format(wxT("no command with id %d"), metaId));
				}
				else {
					const ibInterfaceCommandType commandType = static_cast<ibInterfaceCommandType>(type);
					done = Run(*client, [command, commandType](ibClientFrame*) { command->Execute(commandType); }, since,
						result, refusal, error);
				}
			}
			else if (method == ibProtocolMethod::Event) {
				const ibFormID controlId = params.GetValue<s32>(wxT("Control"));
				const ibProtocolEvent event = static_cast<ibProtocolEvent>(params.GetValue<s32>(wxT("Event")));
				const ibDataNode* found = params.FindChild(wxT("Args"));
				const ibDataNode args = found != nullptr ? *found : ibDataNode();
				const wxString formKey = params.GetValue<wxString>(wxT("Form"));
				// A TAB'S OWN VIEW, named by the tab — what the person did in a text, not in a control of a form; by the tab,
				// not the active one, as it may be handed over when the tab is being closed.
				const s32 tabId = params.GetValue<s32>(wxT("Tab"));
				done = Run(*client, [controlId, event, args, formKey, tabId](ibClientFrame* frame) {
					if (tabId != 0) {
						if (ibClientChildFrame* const tab = frame->Tab(frame->FindTab(tabId))) {
							if (ibView* const view = tab->GetView())
								view->OnClientEvent(event, args);
						}
					}
					else if (ibValueForm* const form = FormOfCall(frame, formKey))
						form->DispatchEvent(controlId, event, args);
				}, since, result, refusal, error);
			}
			else if (method == ibProtocolMethod::Activate || method == ibProtocolMethod::Close) {
				// A TAB BY ITS ID — a position shifts the moment a tab closes. One the frame does not have is refused
				// before anything is done (asked on the session, where the tabs are).
				const s32 tabId = params.GetValue<s32>(wxT("Tab"));
				const bool activate = method == ibProtocolMethod::Activate;
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				bool found = false;
				if (frame != nullptr && session != nullptr)
					session->Submit([frame, tabId, &found]() { found = frame->FindTab(tabId) < frame->TabCount(); }).get();
				if (frame == nullptr || session == nullptr)
					refuse(ibProtocolRefusal::NoSession, wxT("the client has no session"));
				else if (!found)
					refuse(ibProtocolRefusal::NotFound, wxString::Format(wxT("no tab %d"), tabId));
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
			else if (method == ibProtocolMethod::Respond) {
				// The response goes to the waiting work on the session's worker — the work runs the session's
				// tasks while it waits — and the call then settles on that work: it goes on from where it asked.
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				const std::string requestId = std::string(params.GetValue<wxString>(wxT("Id")).utf8_str());
				const ibDataNode* found = params.FindChild(wxT("Response"));
				const ibDataNode response = found != nullptr ? *found : ibDataNode();
				bool responded = false;
				if (frame != nullptr && session != nullptr)
					session->Submit([frame, &requestId, &response, &responded]() { responded = frame->Respond(requestId, response); }).get();
				if (!responded)
					refuse(ibProtocolRefusal::NotFound, wxT("no such request is waiting"));
				else
					done = Run(*client, nullptr, since, result, refusal, error);
			}
			else if (method == ibProtocolMethod::Fetch) {
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				const ibFormID controlId = params.GetValue<s32>(wxT("Control"));
				const ibDataNode* found = params.FindChild(wxT("Request"));
				const ibDataNode fetchRequest = found != nullptr ? *found : ibDataNode();
				const wxString formKey = params.GetValue<wxString>(wxT("Form"));
				// A TAB'S OWN VIEW, named by the tab — a spreadsheet document's cells, not a control's of a form.
				const s32 tabId = params.GetValue<s32>(wxT("Tab"));
				if (frame != nullptr && session != nullptr) {
					session->Submit([frame, controlId, &fetchRequest, &formKey, tabId, &result, &done]() {
						if (tabId != 0) {
							ibClientChildFrame* const tab = frame->Tab(frame->FindTab(tabId));
							ibView* const view = tab != nullptr ? tab->GetView() : nullptr;
							done = view != nullptr && view->Fetch(fetchRequest, result);
							return;
						}
						ibValueForm* const form = FormOfCall(frame, formKey);
						ibValueFrame* const control = form != nullptr ? form->FindControlByID(controlId) : nullptr;
						done = control != nullptr && control->Fetch(fetchRequest, result);
					}).get();
				}
				if (!done)
					refuse(ibProtocolRefusal::NotFound, wxT("nothing to fetch there"));
			}
			else if (method == ibProtocolMethod::Presentation) {
				// A VALUE AS A PERSON READS IT — formatted here, where the configuration is (a reference's name is
				// its), on the client's session: a value the client holds itself (a date from its calendar, a value an
				// answer carried) is shown the way the server shows any other.
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				const ibDataNode* const value = params.FindChild(wxT("Value"));
				const wxString format = params.GetValue<wxString>(wxT("Format"));
				const ibMetaData* const metaData = session != nullptr ? session->GetMetaData() : nullptr;
				if (session == nullptr)
					refuse(ibProtocolRefusal::NoSession, wxT("the client has no session"));
				else if (value == nullptr)
					refuse(ibProtocolRefusal::BadParameter, wxT("a presentation needs a Value"));
				else if (metaData == nullptr)
					refuse(ibProtocolRefusal::Failed, wxT("the session has no configuration"));
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
							refuse(ibProtocolRefusal::BadParameter, err.GetErrorDescription());
						}
					}).get();
					if (done)
						result.SetValue(wxT("Text"), text);
				}
			}
			else if (method == ibProtocolMethod::Upload) {
				// A FILE COMES IN PARTS, each a call of its own, and none is held whole here: the first names the file
				// and is answered with its id, the rest name the id. Into the client's own temporary storage.
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				ibTempStorage* const storage = ibApplicationInstance::GetTempStorage(m_applicationInstance);
				const wxString text = params.GetValue<wxString>(wxT("Data"));
				const wxMemoryBuffer data = wxBase64Decode(text);
				wxString id = params.GetValue<wxString>(wxT("File"));
				if (session == nullptr || storage == nullptr) {
					refuse(ibProtocolRefusal::NoSession, wxT("the client has no session"));
				}
				else if (!text.IsEmpty() && data.GetDataLen() == 0) {
					refuse(ibProtocolRefusal::BadParameter, wxT("Data is not base64"));
				}
				else if (data.GetDataLen() > ibTempStorage::kPartSize) {
					refuse(ibProtocolRefusal::BadParameter,
						wxString::Format(wxT("a part is at most %u bytes"), static_cast<unsigned int>(ibTempStorage::kPartSize)));
				}
				else {
					const ibGuid& owner = session->Identity().m_guid;
					const wxString name = params.GetValue<wxString>(wxT("Name"));
					const bool made = id.IsEmpty();
					if (made)
						id = storage->Create(owner, name);
					else if (storage->GetName(owner, id).IsEmpty())
						id.clear();
					if (id.IsEmpty() && !made)
						refuse(ibProtocolRefusal::NotFound, wxT("no such file"));
					else if (id.IsEmpty() && name.IsEmpty())
						refuse(ibProtocolRefusal::BadParameter, wxT("a new file needs a Name"));
					else if (id.IsEmpty())
						refuse(ibProtocolRefusal::Failed, wxT("the file was not made"));
					else if (data.GetDataLen() != 0 && !storage->WritePart(owner, id, storage->PartCount(owner, id), data.GetData(), data.GetDataLen()))
						refuse(ibProtocolRefusal::Failed, wxT("the part was not written"));
					else {
						result.SetValue(wxT("File"), id);
						done = true;
					}
				}
			}
			else if (method == ibProtocolMethod::Download) {
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				const ibTempStorage* const storage = ibApplicationInstance::GetTempStorage(m_applicationInstance);
				const wxString id = params.GetValue<wxString>(wxT("File"));
				const s32 part = params.GetValue<s32>(wxT("Part"));
				wxMemoryBuffer data;
				if (session == nullptr || storage == nullptr) {
					refuse(ibProtocolRefusal::NoSession, wxT("the client has no session"));
				}
				else {
					const ibGuid& owner = session->Identity().m_guid;
					const wxString name = storage->GetName(owner, id);
					const int parts = storage->PartCount(owner, id);
					// An empty file has no parts, and its part 0 is nothing — it is answered, empty.
					if (name.IsEmpty())
						refuse(ibProtocolRefusal::NotFound, wxT("no such file"));
					else if ((part != 0 || parts != 0) && !storage->ReadPart(owner, id, part, data))
						refuse(ibProtocolRefusal::NotFound, wxT("no such part"));
					else {
						result.SetValue(wxT("Name"), name);
						result.SetValue(wxT("Part"), part);
						result.SetValue(wxT("Parts"), static_cast<s32>(parts));
						result.SetValue(wxT("Data"), wxBase64Encode(data.GetData(), data.GetDataLen()));
						done = true;
					}
				}
			}
			else if (method == ibProtocolMethod::Open) {
				// THE DOC MANAGER OPENS IT, as the desktop's File → Open: the template the file's name says, a document
				// of it reading the file by its id (ibDocument::DoOpenDocument). Refused before anything is done when
				// the client has no such file, or nothing its application has reads it — asked on the session, whose
				// temporary storage the file is in.
				const wxString id = params.GetValue<wxString>(wxT("File"));
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				if (frame == nullptr || session == nullptr) {
					refuse(ibProtocolRefusal::NoSession, wxT("the client has no session"));
				}
				else if (params.FindField(wxT("File")) == nullptr) {
					// No file: a NEW document, as the desktop's File → New with a template picked — the template that
					// opens the Extension.
					const wxString extension = params.GetValue<wxString>(wxT("Extension"));
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
							refuse(ibProtocolRefusal::NotFound, wxString::Format(wxT("nothing here makes '%s'"), extension));
					}).get();
					if (refusal == ibProtocolRefusal::None)
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
							refuse(ibProtocolRefusal::NotFound, wxT("no such file"));
						else if (temp == nullptr || !temp->IsVisible())
							refuse(ibProtocolRefusal::NotFound, wxString::Format(wxT("nothing here opens '%s'"), file.GetName()));
					}).get();
					if (refusal == ibProtocolRefusal::None)
						done = Run(*client, [id](ibClientFrame* frame) { frame->GetDocumentManager()->CreateDocument(id); }, since,
							result, refusal, error);
				}
			}
			else if (method == ibProtocolMethod::Command) {
				// One of the commands the client's application offers, on the active tab's document — refused before
				// anything is done when it offers no such command, or the document does not take it now (asked on
				// the session, where the document is).
				const ibDocCommand command = static_cast<ibDocCommand>(params.GetValue<s32>(wxT("Command")));
				const wxString name = params.GetValue<wxString>(wxT("Name"));   // Save as's: the file's
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				bool enabled = false;
				if (frame == nullptr || session == nullptr)
					refuse(ibProtocolRefusal::NoSession, wxT("the client has no session"));
				else if (frame->GetDocumentManager()->GetCommands().count(command) == 0)
					refuse(ibProtocolRefusal::NotFound, wxT("no such command"));
				else {
					session->Submit([frame, command, &enabled]() { enabled = frame->IsCommandEnabled(command); }).get();
					if (!enabled)
						refuse(ibProtocolRefusal::NotNow, wxT("the active document does not take this command now"));
					else if (command == ibDocCommand::SaveAs && name.IsEmpty())
						refuse(ibProtocolRefusal::BadParameter, wxT("Save as needs a Name"));
					else
						done = Run(*client, [command, name](ibClientFrame* frame) { frame->DoCommand(command, name); }, since, result,
							refusal, error);
				}
			}
			}
		}
	}

	// A refusal always says why — one that slipped through unnamed is the server's failure, never «nothing».
	if (!done && refusal == ibProtocolRefusal::None)
		refuse(ibProtocolRefusal::Failed, error.IsEmpty() ? wxString(wxT("the call was not done")) : error);
	return done;
}

//*******************************************************************************************
//*                                Running the client's work                                *
//*******************************************************************************************

bool ibClientHost::Run(Client& client, std::function<void(ibClientFrame*)> work, s32 since, ibDataNode& result,
	ibProtocolRefusal& refusal, wxString& error)
{
	std::shared_ptr<ibSession> session = client.instance->ShareSession();
	if (session == nullptr) {
		refusal = ibProtocolRefusal::NoSession;
		error = wxT("the client has no session");
		return false;
	}

	// THE CALL'S PARTS, timed — the work (the script, the handlers), the frame drawn, the patch taken: where a slow call
	// spends its time, said beside the call's own line (a debug build's journal).
	const std::int64_t began = NowMs();

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
		session->Submit([instance, &drawn, &events, &drew]() {
			ibClientFrame* const frame = instance->GetFrame();
			if (frame == nullptr)
				return;
			frame->DrainPendingCloses();
			DrawFrame(frame, drawn, events);
			drew = true;
		}).get();
	}
	catch (...) {
		refusal = ibProtocolRefusal::Failed;
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
	const s32 active = drawn.GetValue<s32>(wxT("ActiveTab"));
	ibDataNode base;
	const bool fromLast = since != 0 && since == client.frame;
	if (fromLast && client.protocol >= 2) {
		const auto kept = client.views.find(active);
		const bool ownView = kept != client.views.end();
		for (const auto& field : client.sent.Fields())
			base.AddField(field.first, field.second);
		for (const auto& property : client.sent.Properties())
			if (!ownView || property.first != wxT("View"))
				base.SetProperty(property.first, property.second);
		if (ownView)
			base.SetProperty(wxT("View"), ibDataValue::Child(kept->second));
	}
	ibDataNode patch;
	const bool patched = fromLast && ibClientFramePatch(client.protocol >= 2 ? base : client.sent, drawn, patch);
	ibJournalInfo(wxT("client"), wxT("  work %lld ms, frame drawn %lld ms, %s %lld ms"), settled - began, framed - settled,
		patched ? wxT("patched") : wxT("sent whole"), NowMs() - framed);
	if (patched) {
		result.SetValue(wxT("Since"), since);
		result.Child(wxT("Patch")) = patch;
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
	result.SetValue(wxT("Frame"), ++client.frame);

	// …and the views kept as the client keeps them: this tab's is the one just sent; a frame sent whole begins the
	// store again; a tab gone takes its view with it.
	if (client.protocol >= 2) {
		if (!patched)
			client.views.clear();
		if (const ibDataValue* const view = drawn.FindProperty(wxT("View")))
			client.views[active] = view->AsChild();
		else
			client.views.erase(active);
		std::set<s32> open;
		if (const ibDataNode* const tabs = drawn.FindChild(wxT("Tabs")))
			for (const ibDataNode& tab : tabs->Children())
				open.insert(static_cast<s32>(tab.GetMetaId()));
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
	state.SetValue(wxT("Title"), frame->GetTitle());
	state.SetValue(wxT("Status"), frame->GetStatusText());
	// The active tab by its id, as every tab is named on the wire; 0 — none.
	const ibClientChildFrame* const active = frame->Tab(frame->ActiveTab());
	state.SetValue(wxT("ActiveTab"), active != nullptr ? active->GetId() : 0);

	// What the client's application offers besides its tabs — the schemas its doc manager registered.
	std::vector<ibDataValue> schemas;
	for (const ibProtocolSchema kind : frame->GetDocumentManager()->GetSchemaKinds())
		schemas.push_back(ibDataValue::Int(static_cast<s64>(kind)));
	state.AddField(wxT("Schemas"), ibDataValue::Array(schemas));

	// …and the documents it opens from a file — the templates its doc manager registered that a person picks
	// (the desktop's File → Open filter and File → New): an uploaded file opens by the one its name matches, a new one is
	// made by its extension. The client registers these and no others, each with its picture by its id.
	ibDataNode& templates = state.Child(wxT("Templates"));
	for (const ibDocTemplate* const temp : frame->GetDocumentManager()->GetTemplatesVector()) {
		if (!temp->IsVisible())
			continue;
		ibDataNode& node = templates.AddChild(0, 0);
		node.SetValue(wxT("Title"), temp->GetDescription());
		node.SetValue(wxT("Mask"), temp->GetFileFilter());
		node.SetValue(wxT("Extension"), temp->GetDefaultExtension());
		node.SetValue(wxT("Picture"), frame->SendPicture(ibPictureDescription(temp->GetPictureID()), nullptr));
		if ((temp->GetFlags() & ibTEMPLATE_ONLY_OPEN) != 0)
			node.SetValue(wxT("OnlyOpen"), true);
	}

	// …and its menus — the application's, beside the File and Edit of the client's own doc manager.
	DrawMenu(frame, frame->GetMenu().GetItems(), state.Child(wxT("Menu")));
	const std::int64_t menus = NowMs();

	// ⚠ THE ACTIVE VIEW BEFORE THE TABS: drawing it puts its form's caption on its document (ibVisualHost::
	// UpdateVisualHost → SetCaption) — the tab's title. Drawn after them, a title changed by this answer (the form editor's
	// Apply) reached the tab one frame late.
	if (ibClientChildFrame* const tab = frame->Tab(frame->ActiveTab())) {
		if (ibView* const view = tab->GetView())
			view->OnDraw(state.Child(wxT("View")));
	}
	const std::int64_t viewDrawn = NowMs();

	// Each tab by its id (NodeId) — what activate and close name it by; their order is the frame's. With the commands its
	// document takes now: the client's doc manager enables its File and Edit by them, and asks nothing.
	ibDataNode& tabs = state.Child(wxT("Tabs"));
	for (std::size_t i = 0; i < frame->TabCount(); ++i) {
		ibClientChildFrame* const tab = frame->Tab(i);
		ibDataNode& node = tabs.AddChild(0, tab->GetId());
		node.SetValue(wxT("Title"), tab->GetTitle());
		if (tab->GetIcon().IsOk())
			node.SetValue(wxT("Icon"), wxString(tab->GetIcon().GetData()));
		if (tab->IsLocked())
			node.SetValue(wxT("Locked"), true);
		// What its view shows, when it is no form — the client draws it with the view of its own it has for that.
		if (const ibView* const view = tab->GetView()) {
			if (view->GetViewKind() != ibClientViewKind::Form)
				node.SetValue(wxT("Kind"), static_cast<s32>(view->GetViewKind()));
		}
		// What its document saves as — the named lines of a file dialog, Title and Mask each, as Templates are — and its
		// file, once it has one: a file of the session's temporary storage the client takes down to where the person keeps
		// it (Save, Save as). Its document as Save as takes it: a form's active grid box's sheet, while it is that.
		if (const ibDocument* const document = frame->GetSaveAsDocument(i)) {
			const wxArrayString lines = wxSplit(document->GetSaveFilter(), wxT('|'));
			if (lines.size() >= 2) {
				ibDataNode& formats = node.Child(wxT("Formats"));
				for (std::size_t line = 0; line + 1 < lines.size(); line += 2) {
					ibDataNode& format = formats.AddChild(0, 0);
					format.SetValue(wxT("Title"), lines[line]);
					format.SetValue(wxT("Mask"), lines[line + 1]);
				}
				if (document->GetDocumentSaved())
					node.SetValue(wxT("File"), document->GetFilename());
			}
		}
		// The tab whose document owns this one's — the client raises it under that one, and it goes with it.
		if (const ibDocument* const owner = tab->GetDocument() != nullptr ? tab->GetDocument()->GetDocParent() : nullptr) {
			for (std::size_t j = 0; j < frame->TabCount(); ++j) {
				if (frame->Tab(j)->GetDocument() == owner)
					node.SetValue(wxT("Parent"), frame->Tab(j)->GetId());
			}
		}

		std::vector<ibDataValue> commands;
		for (const auto& command : frame->GetDocumentManager()->GetCommands()) {
			if (frame->IsCommandEnabled(command.first, i))
				commands.push_back(ibDataValue::Int(static_cast<s64>(command.first)));
		}
		node.AddField(wxT("Commands"), ibDataValue::Array(commands));
	}

	const std::int64_t tabsDrawn = NowMs();

	// THE EVENTS — what happened since the last answer, not what the frame is: apart, never patched.
	ibDataNode& messages = events.Child(wxT("Messages"));
	for (const ibClientFrame::PendingMessage& message : frame->DrainPendingMessages()) {
		ibDataNode& node = messages.AddChild(0, 0);
		node.SetValue(wxT("Level"), static_cast<s32>(message.level));
		node.SetValue(wxT("Text"), message.text);
	}
	if (frame->TakeClearPending())
		events.SetValue(wxT("Clear"), true);
	// Exit done — the client goes; its session goes when it logs out.
	if (frame->TakeExitPending())
		events.SetValue(wxT("Exit"), true);

	if (frame->HasPendingRequest()) {
		const ibClientFrame::PendingRequest pending = frame->PeekPendingRequest();
		ibDataNode& request = state.Child(wxT("Request"));
		request = pending.request;
		request.SetValue(wxT("Id"), wxString::FromUTF8(pending.id.c_str()));
	}

	ibJournalInfo(wxT("client"), wxT("    the frame: menus %lld ms, tabs %lld ms, the view %lld ms"), menus - began,
		tabsDrawn - viewDrawn, viewDrawn - menus);

	// …AND THE PICTURES THE FRAME NAMES THAT THE CLIENT WAS NOT GIVEN YET — beside the events, never patched: given once,
	// the client keeps them, and the frame names them by their ids (ibClientFrame::SendPicture).
	const std::vector<ibClientFrame::PendingPicture> pictures = frame->DrainPendingPictures();
	if (!pictures.empty()) {
		ibDataNode& sent = events.Child(wxT("Pictures"));
		for (const ibClientFrame::PendingPicture& picture : pictures) {
			ibDataNode& node = sent.AddChild(0, 0);
			node.SetValue(wxT("Id"), picture.id);
			node.SetValue(wxT("Picture"), picture.picture);
		}
	}
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
			const bool idle = deadline == 0
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
