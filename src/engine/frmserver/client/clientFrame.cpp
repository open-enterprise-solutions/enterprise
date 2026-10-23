#include "clientFrame.h"

#include <algorithm>
#include <atomic>
#include <sstream>

#include "backend/appData.h"   // ibApplicationInstance::GetTempStorage — a document saved as a file of the client's naming
#include "backend/backend_form.h"
#include "backend/backend_picture.h"
#include "backend/debugger/debugServer.h"   // a script's failure sent to the designer (BackendError)
#include "core/picturePredefined.h"
#include "backend/session/session.h"
#include "backend/session/workerPool.h"   // Await / Wake — a question waits in the session's pool
#include "backend/temp/tempStorage.h"     // ibTempFile — a file the client uploaded

#include "frmserver/docView/docManager.h"   // ibDocTemplate — what opens a file
#include "frmserver/visualView/visualHost.h"
#include "frmserver/visualView/ctrl/form.h"
#include "frmserver/visualView/ctrl/gridBox.h"   // ibFetchSpreadsheet — a sheet printed goes as a sheet fetched

#include "clientChildFrame.h"
#include "clientFrameDesigner.h"
#include "clientFrameRuntime.h"
#include "clientHost.h"      // NotifyChanged — the client told its frame changed by itself
#include "propertyInspector.h"   // ShowProperties — an object's rows, as the inspector writes them
#include "protocol/protocol.h"
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

std::unique_ptr<ibClientFrame> ibClientFrame::Create(ibProtocol::ibLogin::ibMode mode, ibSessionHolder&& holder,
	ibClientInstance* clientInstance)
{
	switch (mode) {
	case ibProtocol::ibLogin::ibMode::Runtime:
		return std::unique_ptr<ibClientFrame>(new ibClientFrameRuntime(std::move(holder), clientInstance));
	case ibProtocol::ibLogin::ibMode::Designer:
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

	// The status bar's first words — the desktop window's, both modes' (CreateWideGui); the client draws what is here.
	SetStatusText(_("Ready"));
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
	delete m_docManager;
	m_docManager = nullptr;

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

void ibClientFrame::RefreshFrame()
{
	ibSession* const session = GetSession();
	if (ibClientHost* const host = session != nullptr ? ibClientHost::Find(session->GetApplicationInstance()) : nullptr)
		host->NotifyChanged(m_clientInstance);
}

void ibClientFrame::RaiseFrame()
{
	{
		std::lock_guard<std::mutex> lock(m_msgMutex);
		m_raisePending = true;
	}
	RefreshFrame();
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

void ibClientFrame::BackendError(const wxString& strFileName,
	const wxString& strDocPath, const long line,
	const wxString& strErrorMessage)
{
	// THE DESKTOP'S ERROR WINDOW (ibFrontendMainFrameEnterprise::BackendError), asked of the client: the person chooses
	// what becomes of the failure. Closed without a choice — as Close window.
	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::Error));
	request.SetValue(ibProtocol::ibRequest::ibError::Picture, ibBackendPicture::SendPicture(g_picErrorCLSID));
	request.SetValue(ibProtocol::ibRequest::ibError::Text, strErrorMessage);

	ibDataNode response;
	const std::int32_t act = DoRequest(request, response) ? response.GetValue<std::int32_t>(ibProtocol::ibRequest::ibError::Act) : 0;   // 0 — closed: Close window

	// ⭐ THE ANSWERS ARE CUMULATIVE, as the desktop's: the error stays in the messages whatever was chosen; Go to designer
	// also sends it to the designer debugging this base; Close program does both, and the client goes.
	{
		std::lock_guard<std::mutex> lock(m_msgMutex);
		m_pendingMessages.push_back({ ibStatusMessage_Error, strErrorMessage });
	}

	if (act == static_cast<std::int32_t>(ibProtocol::ibRequest::ibError::ibAct::GotoDesigner) || act == static_cast<std::int32_t>(ibProtocol::ibRequest::ibError::ibAct::CloseProgram)) {
		if (ibDebuggerServer* const server = debugServer)
			server->SendErrorToClient(strFileName, strDocPath, line, strErrorMessage);
	}

	if (act == static_cast<std::int32_t>(ibProtocol::ibRequest::ibError::ibAct::CloseProgram))
		ExitClient(true);
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

bool ibClientFrame::TakeExitPending()
{
	std::lock_guard<std::mutex> lock(m_msgMutex);
	const bool was = m_exitPending;
	m_exitPending = false;
	return was;
}

bool ibClientFrame::TakeRaisePending()
{
	std::lock_guard<std::mutex> lock(m_msgMutex);
	const bool was = m_raisePending;
	m_raisePending = false;
	return was;
}

void ibClientFrame::ShowProperties(ibValueMetaObject* object, std::int32_t tabId)
{
	ibPropertyInspector inspector;
	inspector.SelectObject(object, tabId);
	ibDataNode rows;
	inspector.OnDraw(rows);
	std::lock_guard<std::mutex> lock(m_msgMutex);
	m_shownProperties.push_back(std::move(rows));
}

std::vector<ibDataNode> ibClientFrame::TakeShownProperties()
{
	std::lock_guard<std::mutex> lock(m_msgMutex);
	std::vector<ibDataNode> out;
	out.swap(m_shownProperties);
	return out;
}


bool ibClientFrame::DoRequest(const ibDataNode& request, ibDataNode& response)
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
	// …and the client told: a question asked by work it did not call (a report's failure, said when the report is
	// delivered) waits for a client that would otherwise learn of it only with its next call.
	RefreshFrame();
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

int ibClientFrame::DoShowModalMessage(const wxString& message, const wxString& caption, int style)
{
	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::Message));
	request.SetValue(ibProtocol::ibRequest::ibMessage::Text, message);
	request.SetValue(ibProtocol::ibRequest::ibMessage::Caption, caption);
	request.SetValue(ibProtocol::ibRequest::ibMessage::Style, style);

	ibDataNode response;
	return DoRequest(request, response) ? response.GetValue<std::int32_t>(ibProtocol::ibRequest::ibMessage::Button) : 0;   // 0 — no button: cancelled
}

bool ibClientFrame::HasPendingRequest() const
{
	std::lock_guard<std::mutex> lock(m_requestMutex);
	return !m_waiting.empty();
}

void ibClientFrame::ReplacePendingRequest(const ibDataNode& request)
{
	{
		std::lock_guard<std::mutex> lock(m_requestMutex);
		if (m_waiting.empty())
			return;
		m_waiting.back().request = request;
	}
	RefreshFrame();
}

ibClientFrame::PendingRequest ibClientFrame::PeekPendingRequest() const
{
	// THE NEWEST — the one the work waits on now: a question asked from a form opened under another (a choice form's
	// message) is answered first; the one under it shows again when it is.
	std::lock_guard<std::mutex> lock(m_requestMutex);
	return PendingRequest{ m_waiting.back().id, m_waiting.back().request };
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

#include "frmserver/docView/templates/docViewSpreadsheet.h"

bool ibClientFrame::ShowSpreadsheetDocument(const wxString& strTitle, std::shared_ptr<ibSpreadsheetObject>& spreadSheetDocument)
{
	class ibSpreadsheetMemoryDocument :
		public ibSheetFileDocument {
	public:

		ibSpreadsheetMemoryDocument(const wxString& strTitle, const std::shared_ptr<ibSpreadsheetObject>& spreadSheetDocument) :
			ibSheetFileDocument(spreadSheetDocument)
		{
			ibSheetFileDocument::SetTitle(strTitle);
			ibSheetFileDocument::SetFilename(strTitle);
		}
	};

	ibSpreadsheetMemoryDocument* createdDoc =
		m_docManager->CreateDocument<ibSpreadsheetMemoryDocument>(strTitle, spreadSheetDocument);
	if (createdDoc == nullptr)
		return false;

	if (createdDoc->OnCreate(wxEmptyString, 0)) {
		createdDoc->SetCommandProcessor(createdDoc->OnCreateCommandProcessor());
		m_docManager->AddDocument(createdDoc);
		return true;
	}

	// The document may be already destroyed, this happens if its view
	// creation fails as then the view being created is destroyed
	// triggering the destruction of the document as this first view is
	// also the last one. However if OnCreate() fails for any reason other
	// than view creation failure, the document is still alive and we need
	// to clean it up ourselves to avoid having a zombie document.

	createdDoc->DeleteAllViews();
	return false;
}

bool ibClientFrame::PrintSpreadsheetDocument(const std::shared_ptr<ibSpreadsheetObject>& doc, bool /*showPrintDlg*/)
{
	// THE SHEET AS A SPREADSHEET'S FETCH WRITES IT (ibFetchSpreadsheet) — its texts as shown: the client's sheet holds no
	// parameters, and prints it as the desktop's printed its own.
	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::Print));
	if (!doc || !ibFetchSpreadsheet(*doc, request))
		return false;

	ibDataNode response;
	return DoRequest(request, response) && response.GetValue<bool>(ibProtocol::ibRequest::ibPrint::Done);
}

bool ibClientFrame::ShowScheduleEditor(ibJobScheduleDescription& schedule)
{
	ibDataValue stored;
	if (!ibJobScheduleDescriptionMemory::WriteNode(stored, schedule))
		return false;

	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::Schedule));
	request.SetField(ibProtocol::ibRequest::ibSchedule::Schedule, stored);

	// CANCEL IS THE ONLY "NOTHING HAPPENED" — answered with no Schedule.
	ibDataNode response;
	if (!DoRequest(request, response))
		return false;
	const ibDataValue* const edited = response.FindField(ibProtocol::ibRequest::ibSchedule::Schedule);
	if (edited == nullptr || !ibJobScheduleDescriptionMemory::ReadNode(*edited, schedule))
		return false;

	// OK MARKS THE FORM, as the desktop's did (ibFrontendMainFrame::ShowScheduleEditor): the window that did the editing
	// holds the fact.
	if (ibBackendValueForm* const form = ActiveWindow())
		form->Modify(true);

	return true;
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
	ibPictureDescription icon = document != nullptr ? document->GetIcon() : ibPictureDescription();
	if (icon.IsEmptyPicture() && form != nullptr)
		icon = ibPictureDescription(form->GetClassType());
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

std::size_t ibClientFrame::FindTab(std::int32_t id) const
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
	// A tab already closing is closing: its form is not closed a second time (that left the tab drawn over a deleted
	// document — dump 2026-10-07, a close the client sent twice).
	if (std::find(m_pendingCloses.begin(), m_pendingCloses.end(), tab) != m_pendingCloses.end())
		return true;

	// One road for every close — the form's own command and the client's tab cross alike: the form closes
	// (BeforeClose may refuse, and then the tab stays), its tab is marked, DrainPendingCloses erases it. A tab with no
	// form closes by its own door, which asks its document about what is unsaved in this work — never later, in the
	// drawing (a new spreadsheet closed hung the client, 2026-10-07).
	ibValueForm* const form = FormOfTab(tab);
	if (form != nullptr)
		return form->CloseForm();
	return tab == nullptr || tab->Close();
}

bool ibClientFrame::CloseAll()
{
	// Each tab closed the one road every close goes — its form asks about what is unsaved, and the person may keep it:
	// then the rest stay too. The indices hold: a tab closed is only marked, and erased after the request
	// (DrainPendingCloses).
	for (std::size_t i = 0; i < m_tabs.size(); ++i) {
		if (!m_tabs[i]->IsLocked() && !CloseTab(i))
			return false;
	}
	return true;
}

bool ibClientFrame::IsSchemaAllowed(ibProtocol::ibSchema::ibKind kind) const
{
	const ibClientSchema* const schema = GetDocumentManager() != nullptr ? GetDocumentManager()->FindSchema(kind) : nullptr;
	return schema != nullptr && schema->AccessRight(*GetSession());
}

bool ibClientFrame::IsCommandEnabled(ibDocCommand command) const
{
	// The window's own, with or without a document: the client gone; its tabs closed — while one may be; a file opened —
	// when something here opens one (a template a person picks, the frame's Templates).
	if (command == ibDocCommand::Exit)
		return true;
	if (command == ibDocCommand::CloseAll) {
		for (const auto& tab : m_tabs) {
			if (!tab->IsLocked())
				return true;
		}
		return false;
	}
	if (command == ibDocCommand::Open) {
		for (const ibDocTemplate* const temp : m_docManager->GetTemplatesVector()) {
			if (temp->IsVisible())
				return true;
		}
		return false;
	}

	return IsCommandEnabled(command, m_activeTab);
}

bool ibClientFrame::IsCommandEnabled(ibDocCommand command, std::size_t i) const
{
	const ibClientChildFrame* const tab = Tab(i);
	const ibDocument* const document = tab != nullptr ? tab->GetDocument() : nullptr;
	if (document == nullptr)
		return false;

	// A form only shown — its fields take nothing typed, cut or pasted.
	const ibValueForm* const form = FormOfTab(tab);
	const bool viewOnly = form != nullptr && form->IsViewOnly();

	const ibCommandProcessor* const processor = document->GetCommandProcessor();
	switch (command) {
	case ibDocCommand::Undo:
		return processor != nullptr && processor->CanUndo();
	case ibDocCommand::Redo:
		return processor != nullptr && processor->CanRedo();
	case ibDocCommand::Save:
		return !document->AlreadySaved();
	case ibDocCommand::SaveAs:
		return !GetSaveAsDocument(i)->GetSaveFilter().IsEmpty();
	case ibDocCommand::Close:
		return !tab->IsLocked();
	case ibDocCommand::Cut:
	case ibDocCommand::Paste:
	case ibDocCommand::Delete:
		return !viewOnly;
	case ibDocCommand::Copy:
	case ibDocCommand::SelectAll:
		return true;
	default:
		break;
	}
	return false;
}

ibDocument* ibClientFrame::GetSaveAsDocument(std::size_t i) const
{
	const ibClientChildFrame* const tab = Tab(i);
	const ibValueForm* const form = FormOfTab(tab);
	const ibValueFrame* const control = form != nullptr ? form->GetActiveControl() : nullptr;
	if (const ibView* const view = control != nullptr ? control->GetControlView() : nullptr)
		return view->GetDocument();
	return tab != nullptr ? tab->GetDocument() : nullptr;
}

bool ibClientFrame::DoCommand(ibDocCommand command, const wxString& name)
{
	if (!IsCommandEnabled(command))
		return false;

	switch (command) {
	case ibDocCommand::Open:
		return OpenChosenFile();
	case ibDocCommand::Exit:
		return ExitClient(false);
	case ibDocCommand::CloseAll:
		return CloseAll();
	case ibDocCommand::Cut:
	case ibDocCommand::Copy:
	case ibDocCommand::Paste:
	case ibDocCommand::Delete:
	case ibDocCommand::SelectAll:
		return EditFocused(command);
	default:
		break;
	}

	ibDocument* const document = Tab(m_activeTab)->GetDocument();
	switch (command) {
	case ibDocCommand::Undo:
		return document->GetCommandProcessor()->Undo();
	case ibDocCommand::Redo:
		return document->GetCommandProcessor()->Redo();
	case ibDocCommand::Save:
		return document->Save();
	case ibDocCommand::SaveAs: {
		// A NEW FILE OF THE SESSION'S TEMPORARY STORAGE, of the name the client chose — written as its name says
		// (OnSaveDocument), and taken down by the client by its id, the tab's File.
		ibSession* const session = ibSession::Current();
		ibTempStorage* const storage = session != nullptr
			? ibApplicationInstance::GetTempStorage(session->GetApplicationInstance()) : nullptr;
		const wxString file = storage != nullptr && !name.IsEmpty() ? storage->Create(session->Identity().m_guid, name) : wxString();
		ibDocument* const saved = GetSaveAsDocument(m_activeTab);
		if (file.IsEmpty() || !saved->OnSaveDocument(file))
			return false;
		saved->SetTitle(name);
		return true;
	}
	case ibDocCommand::Close:
		return CloseTab(m_activeTab);
	default:
		break;
	}
	return false;
}

bool ibClientFrame::ExitClient(bool force)
{
	// Each tab closed (CloseAll) — one the person keeps keeps the client, as the desktop window stayed when a document was
	// kept. The start page goes with the client. A forced exit asks nothing: the session's work is already stopped
	// (ibSession::Close).
	if (!force && !CloseAll())
		return false;

	{
		std::lock_guard<std::mutex> lock(m_msgMutex);
		m_exitPending = true;
	}
	// …and a client that is not calling is called for it: nothing else would bring it.
	if (force)
		RefreshFrame();
	return true;
}

bool ibClientFrame::EditFocused(ibDocCommand command)
{
	// The field under the focus is the client's, and what it holds selected: the client does it there. Nothing comes
	// back — a field changed sends its Change as it does when typed into.
	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::Edit));
	request.SetValue(ibProtocol::ibRequest::ibEdit::Command, static_cast<std::int32_t>(command));

	ibDataNode response;
	return Request(request, response);
}

bool ibClientFrame::OpenChosenFile()
{
	// What may be chosen — the templates a person picks (the desktop's File → Open filter), as the frame's Templates
	// list them.
	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::File));
	request.SetValue(ibProtocol::ibRequest::ibFile::Caption, _("Open"));
	for (const ibDocTemplate* const temp : m_docManager->GetTemplatesVector()) {
		if (!temp->IsVisible())
			continue;
		ibDataNode& item = request.AddChild(0, 0);
		item.SetValue(ibProtocol::ibRequest::ibFile::Title, temp->GetDescription());
		item.SetValue(ibProtocol::ibRequest::ibFile::Mask, temp->GetFileFilter());
	}

	// Cancelled — nothing chosen, nothing opened.
	ibDataNode response;
	const wxString id = Request(request, response) ? response.GetValue<wxString>(ibProtocol::ibRequest::ibFile::File) : wxString();
	if (id.IsEmpty())
		return false;

	// Opened as `open` opens it — the template its name says; a file nothing here reads is said to the person.
	const ibTempFile file(id);
	const ibDocTemplate* const temp = file.IsOpened() ? m_docManager->FindTemplateForPath(id) : nullptr;
	if (temp == nullptr || !temp->IsVisible()) {
		ShowModalMessage(wxString::Format(_("Nothing here opens '%s'."), file.GetName()), _("Open"), wxOK | wxICON_ERROR);
		return false;
	}
	return m_docManager->CreateDocument(id) != nullptr;
}

void ibClientFrame::MarkTabForClose(const ibClientChildFrame* tab)
{
	// The same tab may be marked twice (a form's BeforeClose closing it again).
	if (std::find(m_pendingCloses.begin(), m_pendingCloses.end(), tab) == m_pendingCloses.end())
		m_pendingCloses.push_back(tab);
}

void ibClientFrame::DrainPendingCloses()
{
	// Until none is marked: a view going closes its tab (ibView), so the forms a closed form owned — closed with it
	// (ibDocument::Close) — are marked while it goes.
	while (!m_pendingCloses.empty()) {
		const ibClientChildFrame* const tab = m_pendingCloses.front();
		m_pendingCloses.erase(m_pendingCloses.begin());
		for (std::size_t i = 0; i < m_tabs.size(); ++i) {
			if (m_tabs[i].get() != tab)
				continue;
			// The view goes, and with the last view the document and its form (ibDocument::DeleteAllViews) —
			// only now, with nothing on the stack inside them. Then the tab, and the mark its view's going put on it.
			ibDocument* const document = m_tabs[i]->GetDocument();
			const ibDocument* const owner = document != nullptr ? document->GetDocParent() : nullptr;
			if (document != nullptr)
				document->DeleteAllViews();
			m_pendingCloses.erase(std::remove(m_pendingCloses.begin(), m_pendingCloses.end(), tab), m_pendingCloses.end());
			const bool wasActive = i == m_activeTab;
			m_tabs.erase(m_tabs.begin() + i);
			// ⭐ WHAT IS ACTIVE AFTER IT — the desktop's: a tab closed before the active one leaves that one active; the
			// active one closed hands it to its owner's tab (the form editor's — the object it was opened from), else to
			// the tab on its left (ibAuiDocChildFrame::Destroy). The index kept handed it to the one on the right.
			if (i < m_activeTab)
				--m_activeTab;
			else if (wasActive) {
				m_activeTab = i > 0 ? i - 1 : 0;
				for (std::size_t j = 0; owner != nullptr && j < m_tabs.size(); ++j) {
					if (m_tabs[j]->GetDocument() == owner)
						m_activeTab = j;
				}
			}
			if (m_activeTab >= m_tabs.size())
				m_activeTab = m_tabs.empty() ? 0 : m_tabs.size() - 1;
			break;
		}
	}
}
