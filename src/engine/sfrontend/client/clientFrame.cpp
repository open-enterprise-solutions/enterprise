#include "clientFrame.h"

#include <algorithm>
#include <atomic>
#include <sstream>

#include "backend/backend_form.h"
#include "backend/session/session.h"
#include "backend/session/workerPool.h"   // Await / Wake — a question waits in the session's pool

#include "sfrontend/visualView/visualHost.h"
#include "sfrontend/visualView/ctrl/form.h"

#include "clientChildFrame.h"
#include "clientFrameDesigner.h"
#include "clientFrameRuntime.h"
#include "clientRequest.h"
#include "clientSession.h"

namespace {

// A request's id only has to differ from the others waiting; a process-wide counter is enough.
std::atomic<std::uint64_t> s_requestIdCounter{ 0 };

std::string MakeRequestId()
{
	std::ostringstream os;
	os << "r" << s_requestIdCounter.fetch_add(1, std::memory_order_relaxed);
	return os.str();
}

// The form a tab shows — null for a tab of any other document.
ibValueForm* FormOfTab(const ibClientChildFrame* tab)
{
	const ibFormVisualDocument* const document = tab != nullptr
		? dynamic_cast<const ibFormVisualDocument*>(tab->GetDocument()) : nullptr;
	return document != nullptr ? document->GetValueForm() : nullptr;
}

} // namespace

// THE DOCUMENTS ARE THE CLIENT'S — every client keeps its own (two people with the same form open hold two
// forms), so the manager the doc/view framework asks for is the current session's main frame's.
ibDocManager* ibDocManager::GetDocumentManager()
{
	ibClientFrame* const frame = ibClientFrame::GetFrame();
	return frame != nullptr ? frame->GetDocumentManager() : nullptr;
}

std::unique_ptr<ibClientFrame> ibClientFrame::Create(ibClientMode mode, ibSessionHolder&& holder,
	ibClientInstance* clientInstance)
{
	switch (mode) {
	case ibClientMode::Runtime:
		return std::unique_ptr<ibClientFrame>(new ibClientFrameRuntime(std::move(holder), clientInstance));
	case ibClientMode::Designer:
		return std::unique_ptr<ibClientFrame>(new ibClientFrameDesigner(std::move(holder), clientInstance));
	}
	return nullptr;
}

ibClientFrame::ibClientFrame(ibSessionHolder&& holder, ibClientInstance* clientInstance)
	: ibBackendDocFrame(std::move(holder)), m_clientInstance(clientInstance)
{
	// The session learns its frame here, in one visible line, the same moment it becomes ours: a process
	// holds many clients at once, so there is no single main window to ask. A frame built with an empty
	// holder (tests) has no session to tell.
	if (ibClientSession* const session = Session())
		session->SetFrame(this);
}

ibClientSession* ibClientFrame::Session() const
{
	return static_cast<ibClientSession*>(GetSession());
}

ibClientFrame::~ibClientFrame()
{
	// The constructor's back-link goes first: a late ibSession::CurrentFrame() during the teardown must not
	// hand out a frame that is already unwinding.
	if (ibClientSession* const session = Session())
		session->SetFrame(nullptr);

	// The documents were closed before this — by the manager, on the session's worker, where the runtime is
	// (ibClientInstance::OnExit). What is left are the tab shells, and the manager with nothing in it.
	m_pendingCloses.clear();
	m_tabs.clear();
	wxDELETE(m_docManager);

	// Every request still waiting is withdrawn — nothing chosen — and its script woken, so it unwinds instead
	// of waiting for a client nobody will respond for once the frame is gone.
	std::vector<Waiting> withdrawn;
	{
		std::lock_guard<std::mutex> lock(m_requestMutex);
		withdrawn.swap(m_waiting);
	}
	for (Waiting& waiting : withdrawn)
		waiting.response->received.store(true);
	ibSession* const session = GetSession();
	if (ibWorkerPool* const pool = !withdrawn.empty() && session != nullptr ? session->GetWorkerPool() : nullptr)
		pool->Wake(session);
}

void ibClientFrame::SetStatusText(const wxString& strStatus, int /*number*/)
{
	m_status = strStatus;
}

void ibClientFrame::Message(const wxString& strMessage, ibStatusMessage status)
{
	std::lock_guard<std::mutex> lock(m_msgMutex);
	m_pendingMessages.push_back({ status, strMessage });
}

void ibClientFrame::ClearMessage()
{
	// The unread lines go, and the client is told to clear what it shows: a one-shot flag (TakeClearPending).
	std::lock_guard<std::mutex> lock(m_msgMutex);
	m_pendingMessages.clear();
	m_clearPending = true;
}

void ibClientFrame::BackendError(const wxString& /*strFileName*/,
	const wxString& /*strDocPath*/, const long /*line*/,
	const wxString& strErrorMessage)
{
	std::lock_guard<std::mutex> lock(m_msgMutex);
	m_pendingMessages.push_back({ ibStatusMessage_Error, strErrorMessage });
}

std::vector<ibClientFrame::PendingMessage> ibClientFrame::DrainPendingMessages()
{
	std::lock_guard<std::mutex> lock(m_msgMutex);
	std::vector<PendingMessage> out;
	out.swap(m_pendingMessages);
	return out;
}

bool ibClientFrame::TakeClearPending()
{
	std::lock_guard<std::mutex> lock(m_msgMutex);
	const bool was = m_clearPending;
	m_clearPending = false;
	return was;
}

bool ibClientFrame::Request(const ibDataNode& request, ibDataNode& response)
{
	ibSession* const session = GetSession();
	ibWorkerPool* const pool = session != nullptr ? session->GetWorkerPool() : nullptr;
	if (pool == nullptr)
		return false;   // nobody to ask

	const auto slot = std::make_shared<Response>();
	const std::string id = MakeRequestId();
	{
		std::lock_guard<std::mutex> lock(m_requestMutex);
		m_waiting.push_back(Waiting{ id, request, slot });
	}
	try {
		pool->Await(session, [slot]() { return slot->received.load(); });
	}
	catch (...) {
		// Cancelled while waiting — the request is withdrawn and the interruption goes on up the script.
		ibJournalInfo(wxT("client"), wxT("request %s withdrawn: the wait was interrupted"), wxString::FromUTF8(id.c_str()));
		std::lock_guard<std::mutex> lock(m_requestMutex);
		m_waiting.erase(std::remove_if(m_waiting.begin(), m_waiting.end(),
			[&id](const Waiting& waiting) { return waiting.id == id; }), m_waiting.end());
		throw;
	}
	if (!slot->responded)
		return false;
	response = slot->response;
	return true;
}

int ibClientFrame::ShowModalMessage(const wxString& message, const wxString& caption, int style)
{
	ibDataNode request;
	request.SetValue(wxT("Kind"), static_cast<s32>(ibRequestKind::Message));
	request.SetValue(wxT("Text"), message);
	request.SetValue(wxT("Caption"), caption);
	request.SetValue(wxT("Style"), style);

	ibDataNode response;
	return Request(request, response) ? response.GetValue<s32>(wxT("Button")) : 0;   // 0 — no button: cancelled
}

bool ibClientFrame::HasPendingRequest() const
{
	std::lock_guard<std::mutex> lock(m_requestMutex);
	return !m_waiting.empty();
}

ibClientFrame::PendingRequest ibClientFrame::PeekPendingRequest() const
{
	std::lock_guard<std::mutex> lock(m_requestMutex);
	return PendingRequest{ m_waiting.front().id, m_waiting.front().request };
}

bool ibClientFrame::Respond(const std::string& id, const ibDataNode& response)
{
	std::shared_ptr<Response> slot;
	{
		std::lock_guard<std::mutex> lock(m_requestMutex);
		for (auto it = m_waiting.begin(); it != m_waiting.end(); ++it) {
			if (it->id == id) {
				slot = it->response;
				m_waiting.erase(it);
				break;
			}
		}
	}
	if (!slot)
		return false;   // unknown, or responded to already (a duplicate response)
	slot->response = response;
	slot->responded = true;
	slot->received.store(true);
	ibSession* const session = GetSession();
	if (ibWorkerPool* const pool = session != nullptr ? session->GetWorkerPool() : nullptr)
		pool->Wake(session);
	return true;
}

ibBackendValueForm* ibClientFrame::ActiveWindow() const
{
	return ActiveForm();
}

ibValueForm* ibClientFrame::ActiveForm() const
{
	return FormOfTab(Tab(m_activeTab));
}

ibBackendValueForm* ibClientFrame::CreateNewForm(
	const ibFormRequest& request,
	const ibValueMetaObjectFormBase* creator,
	ibBackendControlFrame* backendControl,
	ibSourceDataObject* srcObject)
{
	// The low-level factory: it makes the form and nothing more. Loading and building it is the caller's
	// (ibValueMetaObjectFormBase::CreateAndBuildForm reaches here through ibBackendValueForm::CreateNewForm —
	// building it from here as well would recurse).
	ibControlFrame* ownerControl = dynamic_cast<ibControlFrame*>(backendControl);
	wxASSERT(!(backendControl == nullptr && ownerControl != nullptr));
	return new ibValueForm(request, creator, ownerControl, srcObject);
}

ibUniqueKey ibClientFrame::CreateFormUniqueKey(const ibBackendControlFrame* ownerControl,
	const ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid)
{
	return ibFormVisualDocument::CreateFormUniqueKey(ownerControl, sourceObject, formGuid);
}

ibBackendValueForm* ibClientFrame::FindFormByUniqueKey(const ibBackendControlFrame* ownerControl,
	const ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid)
{
	return ibFormVisualDocument::FindFormByUniqueKey(ownerControl, sourceObject, formGuid);
}

ibBackendValueForm* ibClientFrame::FindFormByUniqueKey(const ibUniqueKey& guid)
{
	return ibFormVisualDocument::FindFormByUniqueKey(guid);
}

ibBackendValueForm* ibClientFrame::FindFormByControlUniqueKey(const ibUniqueKey& guid)
{
	return ibFormVisualDocument::FindFormByControlUniqueKey(guid);
}

ibBackendValueForm* ibClientFrame::FindFormBySourceUniqueKey(const ibUniqueKey& guid)
{
	return ibFormVisualDocument::FindFormBySourceUniqueKey(guid);
}

bool ibClientFrame::UpdateFormUniqueKey(const ibUniqueKeyPair& guid)
{
	return ibFormVisualDocument::UpdateFormUniqueKey(guid);
}

ibClientChildFrame* ibClientFrame::CreateChildFrame(ibView* view)
{
	ibClientFrame* const frame = GetFrame();
	if (view == nullptr || frame == nullptr)
		return nullptr;

	// The document's title is not set yet at this point (a form document sets it in its own OnCreate, which
	// runs after this), so a form's tab takes the form's title rather than open nameless for a frame.
	ibDocument* const document = view->GetDocument();
	const ibFormVisualDocument* const formDocument = dynamic_cast<const ibFormVisualDocument*>(document);
	ibValueForm* const form = formDocument != nullptr ? formDocument->GetValueForm() : nullptr;

	wxString title = document != nullptr ? document->GetTitle() : wxString();
	if (title.IsEmpty() && form != nullptr)
		title = form->GetControlTitle();
	// A file's document — its file's name (the address's own: ibTempStorage::NameOf).
	if (title.IsEmpty() && document != nullptr && !document->GetFilename().IsEmpty())
		title = document->GetUserReadableName();

	auto tab = std::make_unique<ibClientChildFrame>(frame, ++frame->m_lastTabId, document, view, title);

	// The document's own icon first (a catalog's form shows the catalog's), the form's otherwise.
	ibServerPicture icon = document != nullptr ? document->GetIcon() : ibServerPicture();
	if (!icon.IsOk() && form != nullptr)
		icon = ibBackendPicture::GetServerPicture(form->GetClassType());
	if (icon.IsOk())
		tab->SetIcon(icon);

	ibClientChildFrame* const raw = tab.get();
	frame->m_tabs.push_back(std::move(tab));
	frame->m_activeTab = frame->m_tabs.size() - 1;
	return raw;
}

ibClientFrame* ibClientFrame::GetFrame()
{
	// The server's own session (kind Service) has no frame.
	ibSession* const session = ibSession::Current();
	return session != nullptr ? dynamic_cast<ibClientFrame*>(session->GetFrame()) : nullptr;
}

void ibClientFrame::CloseDocuments()
{
	m_closingWindow = true;
	m_docManager->CloseDocuments(true);
}

void ibClientFrame::LockTab(const ibDocChildFrameAnyBase* tab)
{
	// It goes right after the tabs locked before it, ahead of every normal one; the active tab stays the one it was.
	const ibClientChildFrame* const active = Tab(m_activeTab);

	std::size_t locked = 0;
	while (locked < m_tabs.size() && m_tabs[locked]->IsLocked())
		++locked;

	for (std::size_t i = locked; i < m_tabs.size(); ++i) {
		if (m_tabs[i].get() != tab)
			continue;
		std::unique_ptr<ibClientChildFrame> moved = std::move(m_tabs[i]);
		m_tabs.erase(m_tabs.begin() + i);
		moved->Lock();
		m_tabs.insert(m_tabs.begin() + locked, std::move(moved));
		break;
	}

	for (std::size_t i = 0; i < m_tabs.size(); ++i) {
		if (m_tabs[i].get() == active)
			m_activeTab = i;
	}
}

void ibClientFrame::SetActiveTab(std::size_t i)
{
	if (i < m_tabs.size())
		m_activeTab = i;
}

std::size_t ibClientFrame::FindTab(s32 id) const
{
	for (std::size_t i = 0; i < m_tabs.size(); ++i) {
		if (m_tabs[i]->GetId() == id)
			return i;
	}
	return m_tabs.size();
}

bool ibClientFrame::ActivateTab(const ibClientChildFrame* tab)
{
	for (std::size_t i = 0; i < m_tabs.size(); ++i) {
		if (m_tabs[i].get() == tab) {
			m_activeTab = i;
			return true;
		}
	}
	return false;
}

bool ibClientFrame::CloseTab(std::size_t i)
{
	// A locked tab goes with the frame and no other way: no close reaches it.
	ibClientChildFrame* const tab = Tab(i);
	if (tab != nullptr && tab->IsLocked())
		return false;

	// One road for every close — the form's own command and the client's tab cross alike: the form closes
	// (BeforeClose may refuse, and then the tab stays), its tab is marked, DrainPendingCloses erases it.
	ibValueForm* const form = FormOfTab(tab);
	if (form != nullptr)
		return form->CloseForm();
	if (tab != nullptr)
		MarkTabForClose(tab);
	return true;
}

bool ibClientFrame::IsSchemaAllowed(ibClientSchemaKind kind) const
{
	const ibClientSchema* const schema = GetDocumentManager() != nullptr ? GetDocumentManager()->FindSchema(kind) : nullptr;
	return schema != nullptr && schema->AccessRight(GetSession()->GetApplicationInstance());
}

bool ibClientFrame::IsCommandEnabled(ibDocCommand command) const
{
	const ibClientChildFrame* const tab = Tab(m_activeTab);
	const ibDocument* const document = tab != nullptr ? tab->GetDocument() : nullptr;
	if (document == nullptr)
		return false;

	const ibCommandProcessor* const processor = document->GetCommandProcessor();
	switch (command) {
	case ibDocCommand::Undo:
		return processor != nullptr && processor->CanUndo();
	case ibDocCommand::Redo:
		return processor != nullptr && processor->CanRedo();
	case ibDocCommand::Save:
		return !document->AlreadySaved();
	case ibDocCommand::Close:
		return !tab->IsLocked();
	}
	return false;
}

bool ibClientFrame::DoCommand(ibDocCommand command)
{
	if (!IsCommandEnabled(command))
		return false;

	ibDocument* const document = Tab(m_activeTab)->GetDocument();
	switch (command) {
	case ibDocCommand::Undo:
		return document->GetCommandProcessor()->Undo();
	case ibDocCommand::Redo:
		return document->GetCommandProcessor()->Redo();
	case ibDocCommand::Save:
		return document->Save();
	case ibDocCommand::Close:
		return CloseTab(m_activeTab);
	}
	return false;
}

void ibClientFrame::MarkTabForClose(const ibClientChildFrame* tab)
{
	// The same tab may be marked twice (a form's BeforeClose closing it again).
	if (std::find(m_pendingCloses.begin(), m_pendingCloses.end(), tab) == m_pendingCloses.end())
		m_pendingCloses.push_back(tab);
}

void ibClientFrame::DrainPendingCloses()
{
	std::vector<const ibClientChildFrame*> pending;
	pending.swap(m_pendingCloses);
	for (const ibClientChildFrame* tab : pending) {
		for (std::size_t i = 0; i < m_tabs.size(); ++i) {
			if (m_tabs[i].get() != tab)
				continue;
			// The view goes, and with the last view the document and its form (ibDocument::DeleteAllViews) —
			// only now, with nothing on the stack inside them. Then the tab.
			if (ibDocument* const document = m_tabs[i]->GetDocument())
				document->DeleteAllViews();
			m_tabs.erase(m_tabs.begin() + i);
			if (m_activeTab >= m_tabs.size())
				m_activeTab = m_tabs.empty() ? 0 : m_tabs.size() - 1;
			break;
		}
	}
}
