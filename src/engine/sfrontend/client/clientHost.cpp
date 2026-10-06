#include "clientHost.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <vector>

#include <wx/base64.h>

#include "backend/appData.h"
#include "backend/backend_picture.h"     // ibBackendPicture::CreateBase64Image — a tab's icon as it travels
#include "backend/guid.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaFormObject.h"   // ibBackendCommandItem::Execute
#include "backend/rpc/rpcMessage.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/session/session.h"
#include "backend/temp/tempStorage.h"   // the client's files

#include "sfrontend/docView/docManager.h"   // ibDocTemplate — what opens a file
#include "sfrontend/visualView/visualHost.h"

#include "clientChildFrame.h"
#include "clientFrame.h"
#include "clientInstance.h"
#include "clientMethod.h"
#include "clientPatch.h"

namespace {

std::mutex                                                s_hostsMutex;
std::map<const ibApplicationInstance*, ibClientHost*>     s_hosts;

// An instance nobody has heard from for this long is taken down.
constexpr std::chrono::minutes kIdleLimit(30);

wxString IconForFrame(const wxIcon& icon)
{
	return icon.IsOk() ? ibBackendPicture::CreateBase64Image(wxBitmap(icon).ConvertToImage()) : wxString();
}

std::int64_t NowMs()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// WHAT A CLIENT EXECUTES IS WHAT THE DESKTOP'S NAVIGATION DOES: an object of the configuration — a catalog, a
// document, a common form, a command — found by its metadata id among the configuration's own and executed as a
// command item (enterprise's OnMenuItemClicked). An object's own forms are reached through the object: it hands
// out its form for the command type, the configuration's or the one the form builder makes.
const ibBackendCommandItem* FindCommand(const ibApplicationInstance* applicationInstance, ibMetaID metaId)
{
	const ibMetaDataConfigurationBase* metaData = ibApplicationInstance::GetActiveMetaData(applicationInstance);
	return metaData != nullptr ? dynamic_cast<const ibBackendCommandItem*>(metaData->FindAnyObjectByFilter(metaId)) : nullptr;
}

// THE MENU AS IT STANDS NOW — each item with what it does, a command with its name and key, and whether it is
// enabled now (the desktop's EVT_UPDATE_UI): a command by the active tab's document, a schema by its own right —
// the same questions a call of it is refused by.
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

void ibClientHost::Disconnect(const void* connection)
{
	if (connection == nullptr)
		return;

	std::vector<std::shared_ptr<Client>> gone;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto it = m_clients.begin(); it != m_clients.end();) {
			if (it->second->connection == connection) {
				gone.push_back(it->second);
				it = m_clients.erase(it);
			}
			else
				++it;
		}
	}
	for (const std::shared_ptr<Client>& client : gone)
		client->instance->OnExit();
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

	const ibClientMethod method = ibClientMethodFromName(request.m_method);
	if (method == ibClientMethod::Unknown) {
		return request.WantsAnswer()
			? ibRpcWriteError(request.m_id, ibRpcError::MethodNotFound, wxString::Format(wxT("no method named '%s'"), request.m_method))
			: wxString();
	}

	ibDataNode result;
	ibClientRefusal refusal = ibClientRefusal::None;
	const bool done = Call(method, request.m_params, result, refusal, error, connection, address);
	if (!request.WantsAnswer())
		return wxString();
	return done ? ibRpcWriteResult(request.m_id, result) : ibRpcWriteError(request.m_id, static_cast<s32>(refusal), error);
}

bool ibClientHost::Call(ibClientMethod method, const ibDataNode& params, ibDataNode& result, ibClientRefusal& refusal,
	wxString& error, const void* connection, const wxString& address)
{
	bool done = false;
	refusal = ibClientRefusal::None;
	error.clear();
	// Why, and its text — said together by every refusal below.
	const auto refuse = [&refusal, &error](ibClientRefusal why, const wxString& text) { refusal = why; error = text; };

	if (method == ibClientMethod::Unknown) {
		refuse(ibClientRefusal::NotFound, wxT("no such method"));
	}
	else if (method == ibClientMethod::Login) {
		// The instance is made and logged in; the start runs as work of its own, so a start that asks the person
		// something returns here with the request — and the client's id, without which it could not respond.
		s32 protocol = 1;
		params.GetValue(wxT("Protocol"), protocol);
		auto client = std::make_shared<Client>();
		const wxString id = ibGuid(ibGuid::newGuid()).str();
		s32 mode = static_cast<s32>(ibClientMode::Runtime);
		params.GetValue(wxT("Mode"), mode);
		client->instance = std::make_shared<ibClientInstance>(m_applicationInstance, id, address, static_cast<ibClientMode>(mode));
		client->connection = connection;
		if (protocol < 1) {
			refuse(ibClientRefusal::BadParameter, wxString::Format(wxT("no protocol version %d"), protocol));
		}
		else if (!client->instance->Login(params.GetValue<wxString>(wxT("User")), params.GetValue<wxString>(wxT("Password")))) {
			refuse(ibClientRefusal::LoginRefused, wxT("the login was refused"));
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
				refuse(ibClientRefusal::StartRefused, wxT("the start was refused"));
			}
			if (!done)
				RemoveClient(id);
			else {
				// THE VERSION BOTH SPEAK — the older of the two — and what the server offers at it beyond the version
				// itself: nothing yet; a feature is switched on by being listed here, so an older client never meets it.
				result.SetValue(wxT("Client"), id);
				result.SetValue(wxT("Protocol"), std::min(protocol, ibClientProtocolVersion));
				result.AddField(wxT("Features"), ibDataValue::Array(std::vector<ibDataValue>()));
			}
		}
	}
	else if (method == ibClientMethod::Logout) {
		RemoveClient(params.GetValue<wxString>(wxT("Client")));
		done = true;
	}
	else {
		const std::shared_ptr<Client> client = FindClient(params.GetValue<wxString>(wxT("Client")));
		if (client == nullptr) {
			refuse(ibClientRefusal::NoSession, wxT("no such client"));
		}
		else {
			std::lock_guard<std::mutex> clientLock(client->lock);
			client->instance->Touch();
			// The frame the client holds — the number it was last answered with; 0 or absent: none, the frame whole.
			const s32 since = params.GetValue<s32>(wxT("Since"));

			if (method == ibClientMethod::Frame) {
				done = Run(*client, nullptr, since, result, refusal, error);
			}
			else if (method == ibClientMethod::Schema) {
				// Asked of the doc manager of the client's frame — what its application offers — and asked on the
				// client's session: what a schema lists, and what it acts on, may depend on the person's rights.
				const ibClientSchemaKind kind = static_cast<ibClientSchemaKind>(params.GetValue<s32>(wxT("Schema")));
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				const ibClientSchema* const schema = frame != nullptr ? frame->GetDocumentManager()->FindSchema(kind) : nullptr;
				const ibApplicationInstance* const applicationInstance = m_applicationInstance;
				if (frame == nullptr || session == nullptr) {
					refuse(ibClientRefusal::NoSession, wxT("the client has no session"));
				}
				else if (schema == nullptr) {
					refuse(ibClientRefusal::NotFound, wxT("no such schema"));
				}
				else if (params.FindField(wxT("Command")) == nullptr) {
					// Without the right to it, the person gets an error, not the schema.
					session->Submit([schema, applicationInstance, &result, &done, &refuse]() {
						if (!schema->AccessRight(applicationInstance))
							refuse(ibClientRefusal::NoRight, wxT("no right to this schema"));
						else {
							schema->Build(applicationInstance, result);
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
					session->Submit([schema, applicationInstance, command, &args, &work, &refuse, &refusal, &error]() {
						if (!schema->AccessRight(applicationInstance))
							refuse(ibClientRefusal::NoRight, wxT("no right to this schema"));
						else
							work = schema->Command(applicationInstance, command, args, refusal, error);
					}).get();
					if (work)
						done = Run(*client, [work](ibClientFrame*) { work(); }, since, result, refusal, error);
				}
			}
			else if (method == ibClientMethod::Execute) {
				// What the configuration does not have is refused here, to the client: an empty frame would say
				// nothing happened.
				const ibMetaID metaId = params.GetValue<s32>(wxT("Command"));
				s32 type = ibInterfaceCommandType::ibInterfaceCommandType_Default;
				params.GetValue(wxT("Type"), type);
				const ibBackendCommandItem* const command = FindCommand(m_applicationInstance, metaId);
				if (command == nullptr) {
					refuse(ibClientRefusal::NotFound, wxString::Format(wxT("no command with id %d"), metaId));
				}
				else {
					const ibInterfaceCommandType commandType = static_cast<ibInterfaceCommandType>(type);
					done = Run(*client, [command, commandType](ibClientFrame*) { command->Execute(commandType); }, since,
						result, refusal, error);
				}
			}
			else if (method == ibClientMethod::Event) {
				const ibFormID controlId = params.GetValue<s32>(wxT("Control"));
				const ibClientEvent event = static_cast<ibClientEvent>(params.GetValue<s32>(wxT("Event")));
				const ibDataNode* found = params.FindChild(wxT("Args"));
				const ibDataNode args = found != nullptr ? *found : ibDataNode();
				const wxString formKey = params.GetValue<wxString>(wxT("Form"));
				done = Run(*client, [controlId, event, args, formKey](ibClientFrame* frame) {
					if (ibValueForm* const form = FormOfCall(frame, formKey))
						form->DispatchEvent(controlId, event, args);
				}, since, result, refusal, error);
			}
			else if (method == ibClientMethod::Activate || method == ibClientMethod::Close) {
				// A TAB BY ITS ID — a position shifts the moment a tab closes. One the frame does not have is refused
				// before anything is done (asked on the session, where the tabs are).
				const s32 tabId = params.GetValue<s32>(wxT("Tab"));
				const bool activate = method == ibClientMethod::Activate;
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				bool found = false;
				if (frame != nullptr && session != nullptr)
					session->Submit([frame, tabId, &found]() { found = frame->FindTab(tabId) < frame->TabCount(); }).get();
				if (frame == nullptr || session == nullptr)
					refuse(ibClientRefusal::NoSession, wxT("the client has no session"));
				else if (!found)
					refuse(ibClientRefusal::NotFound, wxString::Format(wxT("no tab %d"), tabId));
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
			else if (method == ibClientMethod::Respond) {
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
					refuse(ibClientRefusal::NotFound, wxT("no such request is waiting"));
				else
					done = Run(*client, nullptr, since, result, refusal, error);
			}
			else if (method == ibClientMethod::Fetch) {
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				const ibFormID controlId = params.GetValue<s32>(wxT("Control"));
				const ibDataNode* found = params.FindChild(wxT("Request"));
				const ibDataNode fetchRequest = found != nullptr ? *found : ibDataNode();
				const wxString formKey = params.GetValue<wxString>(wxT("Form"));
				if (frame != nullptr && session != nullptr) {
					session->Submit([frame, controlId, &fetchRequest, &formKey, &result, &done]() {
						ibValueForm* const form = FormOfCall(frame, formKey);
						ibValueFrame* const control = form != nullptr ? form->FindControlByID(controlId) : nullptr;
						done = control != nullptr && control->Fetch(fetchRequest, result);
					}).get();
				}
				if (!done)
					refuse(ibClientRefusal::NotFound, wxT("nothing to fetch there"));
			}
			else if (method == ibClientMethod::Upload) {
				// A FILE COMES IN PARTS, each a call of its own, and none is held whole here: the first names the file
				// and is answered with its id, the rest name the id. Into the client's own temporary storage.
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				ibTempStorage* const storage = ibApplicationInstance::GetTempStorage(m_applicationInstance);
				const wxString text = params.GetValue<wxString>(wxT("Data"));
				const wxMemoryBuffer data = wxBase64Decode(text);
				wxString id = params.GetValue<wxString>(wxT("File"));
				if (session == nullptr || storage == nullptr) {
					refuse(ibClientRefusal::NoSession, wxT("the client has no session"));
				}
				else if (!text.IsEmpty() && data.GetDataLen() == 0) {
					refuse(ibClientRefusal::BadParameter, wxT("Data is not base64"));
				}
				else if (data.GetDataLen() > ibTempStorage::kPartSize) {
					refuse(ibClientRefusal::BadParameter,
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
						refuse(ibClientRefusal::NotFound, wxT("no such file"));
					else if (id.IsEmpty() && name.IsEmpty())
						refuse(ibClientRefusal::BadParameter, wxT("a new file needs a Name"));
					else if (id.IsEmpty())
						refuse(ibClientRefusal::Failed, wxT("the file was not made"));
					else if (data.GetDataLen() != 0 && !storage->WritePart(owner, id, storage->PartCount(owner, id), data.GetData(), data.GetDataLen()))
						refuse(ibClientRefusal::Failed, wxT("the part was not written"));
					else {
						result.SetValue(wxT("File"), id);
						done = true;
					}
				}
			}
			else if (method == ibClientMethod::Download) {
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				const ibTempStorage* const storage = ibApplicationInstance::GetTempStorage(m_applicationInstance);
				const wxString id = params.GetValue<wxString>(wxT("File"));
				const s32 part = params.GetValue<s32>(wxT("Part"));
				wxMemoryBuffer data;
				if (session == nullptr || storage == nullptr) {
					refuse(ibClientRefusal::NoSession, wxT("the client has no session"));
				}
				else {
					const ibGuid& owner = session->Identity().m_guid;
					const wxString name = storage->GetName(owner, id);
					const int parts = storage->PartCount(owner, id);
					// An empty file has no parts, and its part 0 is nothing — it is answered, empty.
					if (name.IsEmpty())
						refuse(ibClientRefusal::NotFound, wxT("no such file"));
					else if ((part != 0 || parts != 0) && !storage->ReadPart(owner, id, part, data))
						refuse(ibClientRefusal::NotFound, wxT("no such part"));
					else {
						result.SetValue(wxT("Name"), name);
						result.SetValue(wxT("Part"), part);
						result.SetValue(wxT("Parts"), static_cast<s32>(parts));
						result.SetValue(wxT("Data"), wxBase64Encode(data.GetData(), data.GetDataLen()));
						done = true;
					}
				}
			}
			else if (method == ibClientMethod::Open) {
				// THE DOC MANAGER OPENS IT, as the desktop's File → Open: the template the file's name says, a document
				// of it reading the file by its id (ibDocument::DoOpenDocument). Refused before anything is done when
				// the client has no such file, or nothing its application has reads it — asked on the session, whose
				// temporary storage the file is in.
				const wxString id = params.GetValue<wxString>(wxT("File"));
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				if (frame == nullptr || session == nullptr) {
					refuse(ibClientRefusal::NoSession, wxT("the client has no session"));
				}
				else {
					session->Submit([frame, &id, &refuse]() {
						const ibTempFile file(id);
						const ibDocTemplate* const temp = file.IsOpened() ? frame->GetDocumentManager()->FindTemplateForPath(id) : nullptr;
						if (!file.IsOpened())
							refuse(ibClientRefusal::NotFound, wxT("no such file"));
						else if (temp == nullptr || !temp->IsVisible())
							refuse(ibClientRefusal::NotFound, wxString::Format(wxT("nothing here opens '%s'"), file.GetName()));
					}).get();
					if (refusal == ibClientRefusal::None)
						done = Run(*client, [id](ibClientFrame* frame) { frame->GetDocumentManager()->CreateDocument(id); }, since,
							result, refusal, error);
				}
			}
			else if (method == ibClientMethod::Command) {
				// One of the commands the client's application offers, on the active tab's document — refused before
				// anything is done when it offers no such command, or the document does not take it now (asked on
				// the session, where the document is).
				const ibDocCommand command = static_cast<ibDocCommand>(params.GetValue<s32>(wxT("Command")));
				ibClientFrame* const frame = client->instance->GetFrame();
				std::shared_ptr<ibSession> session = client->instance->ShareSession();
				bool enabled = false;
				if (frame == nullptr || session == nullptr)
					refuse(ibClientRefusal::NoSession, wxT("the client has no session"));
				else if (frame->GetDocumentManager()->GetCommands().count(command) == 0)
					refuse(ibClientRefusal::NotFound, wxT("no such command"));
				else {
					session->Submit([frame, command, &enabled]() { enabled = frame->IsCommandEnabled(command); }).get();
					if (!enabled)
						refuse(ibClientRefusal::NotNow, wxT("the active document does not take this command now"));
					else
						done = Run(*client, [command](ibClientFrame* frame) { frame->DoCommand(command); }, since, result, refusal,
							error);
				}
			}
		}
	}

	// A refusal always says why — one that slipped through unnamed is the server's failure, never «nothing».
	if (!done && refusal == ibClientRefusal::None)
		refuse(ibClientRefusal::Failed, error.IsEmpty() ? wxString(wxT("the call was not done")) : error);
	return done;
}

//*******************************************************************************************
//*                                Running the client's work                                *
//*******************************************************************************************

bool ibClientHost::Run(Client& client, std::function<void(ibClientFrame*)> work, s32 since, ibDataNode& result,
	ibClientRefusal& refusal, wxString& error)
{
	std::shared_ptr<ibSession> session = client.instance->ShareSession();
	if (session == nullptr) {
		refusal = ibClientRefusal::NoSession;
		error = wxT("the client has no session");
		return false;
	}

	if (work) {
		// WHILE THE PERSON IS ASKED SOMETHING, nothing else is done: the request is answered first. The waiting
		// work runs this session's tasks, so anything let through here would run UNDER the one that asked.
		ibClientFrame* const frame = client.instance->GetFrame();
		if (frame != nullptr && frame->HasPendingRequest()) {
			refusal = ibClientRefusal::Pending;
			error = wxT("a request to the person is pending: respond to it first");
			return false;
		}
		ibClientInstance* const instance = client.instance.get();
		client.unsettled = session->Submit([instance, work]() { work(instance->GetFrame()); }).share();
	}
	Settle(client);

	// The frame drawn on the worker — inside the waiting work when it waits for a response (it runs the session's
	// tasks), after it otherwise; the tabs that closed go first, unless something still waits inside them.
	ibClientInstance* const instance = client.instance.get();
	ibDataNode drawn, events;
	bool drew = false;
	try {
		session->Submit([instance, &drawn, &events, &drew]() {
			ibClientFrame* const frame = instance->GetFrame();
			if (frame == nullptr)
				return;
			if (!frame->HasPendingRequest())
				frame->DrainPendingCloses();
			DrawFrame(frame, drawn, events);
			drew = true;
		}).get();
	}
	catch (...) {
		refusal = ibClientRefusal::Failed;
		error = wxT("the frame could not be drawn");
		return false;
	}
	if (!drew)
		return true;

	// ⭐ THE ANSWER IS NUMBERED, and a client that names the frame it holds (Since — the last number it was given)
	// is answered with the patch from it to this one; any other gets the frame whole. The events go beside either:
	// they are not state, and a message said twice is two messages, never «unchanged».
	ibDataNode patch;
	const bool patched = since != 0 && since == client.frame && ibClientFramePatch(client.sent, drawn, patch);
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
	state.SetValue(wxT("Title"), frame->GetTitle());
	state.SetValue(wxT("Status"), frame->GetStatusText());
	// The active tab by its id, as every tab is named on the wire; 0 — none.
	const ibClientChildFrame* const active = frame->Tab(frame->ActiveTab());
	state.SetValue(wxT("ActiveTab"), active != nullptr ? active->GetId() : 0);

	// What the client's application offers besides its tabs — the schemas its doc manager registered.
	std::vector<ibDataValue> schemas;
	for (const ibClientSchemaKind kind : frame->GetDocumentManager()->GetSchemaKinds())
		schemas.push_back(ibDataValue::Int(static_cast<s64>(kind)));
	state.AddField(wxT("Schemas"), ibDataValue::Array(schemas));

	// …and the documents it opens from a file — the templates its doc manager registered that a person picks
	// (the desktop's File → Open filter): an uploaded file opens by the one its name matches.
	ibDataNode& templates = state.Child(wxT("Templates"));
	for (const ibDocTemplate* const temp : frame->GetDocumentManager()->GetTemplatesVector()) {
		if (!temp->IsVisible())
			continue;
		ibDataNode& node = templates.AddChild(0, 0);
		node.SetValue(wxT("Title"), temp->GetDescription());
		node.SetValue(wxT("Mask"), temp->GetFileFilter());
	}

	// …and its menu — a thin client learns its commands and their keys here, it keeps no list of its own.
	DrawMenu(frame, frame->GetMenu().GetItems(), state.Child(wxT("Menu")));

	// Each tab by its id (NodeId) — what activate and close name it by; their order is the frame's.
	ibDataNode& tabs = state.Child(wxT("Tabs"));
	for (std::size_t i = 0; i < frame->TabCount(); ++i) {
		ibClientChildFrame* const tab = frame->Tab(i);
		ibDataNode& node = tabs.AddChild(0, tab->GetId());
		node.SetValue(wxT("Title"), tab->GetTitle());
		const wxString icon = IconForFrame(tab->GetIcon());
		if (!icon.IsEmpty())
			node.SetValue(wxT("Icon"), icon);
		if (tab->IsLocked())
			node.SetValue(wxT("Locked"), true);
	}

	// THE EVENTS — what happened since the last answer, not what the frame is: apart, never patched.
	ibDataNode& messages = events.Child(wxT("Messages"));
	for (const ibClientFrame::PendingMessage& message : frame->DrainPendingMessages()) {
		ibDataNode& node = messages.AddChild(0, 0);
		node.SetValue(wxT("Level"), static_cast<s32>(message.level));
		node.SetValue(wxT("Text"), message.text);
	}
	if (frame->TakeClearPending())
		events.SetValue(wxT("Clear"), true);

	if (frame->HasPendingRequest()) {
		const ibClientFrame::PendingRequest pending = frame->PeekPendingRequest();
		ibDataNode& request = state.Child(wxT("Request"));
		request = pending.request;
		request.SetValue(wxT("Id"), wxString::FromUTF8(pending.id.c_str()));
	}

	if (ibClientChildFrame* const tab = frame->Tab(frame->ActiveTab())) {
		if (ibView* const view = tab->GetView())
			view->OnDraw(state.Child(wxT("View")));
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
			const bool idle = now - instance->LastActiveMs() > std::chrono::duration_cast<std::chrono::milliseconds>(kIdleLimit).count();
			if (instance->IsCloseRequested() || idle) {
				RemoveClient(entry.first);
				continue;
			}
			// The forms' due idle handlers, handed to the session — they run where the forms' scripts run, in order
			// with everything else the session does, and not waited for here.
			if (std::shared_ptr<ibSession> session = instance->ShareSession()) {
				if (instance->GetFrame() != nullptr) {
					(void)session->Submit([]() { ibFormVisualDocument::RunIdleHandlers(); });
				}
			}
		}
	}
}
