#include "webFrame.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>

#include "backend/backend_form.h"
#include "backend/session/session.h"
#include "backend/session/workerPool.h"   // Await / Wake — a question waits in the session's pool
#include "backend/compiler/value.h"
#include "backend/metaCollection/metaFormObject.h"   // ibValueMetaObjectFormBase::CreateAndBuildForm

#include "frontend/docView/docView.h"   // ibMetaView
#include "visualView/ctrl/form.h"
#include "visualView/ctrl/frame.h"
#include "visualView/visualHostClient.h"

#include "webChildFrame.h"
#include "webApplication.h"
#include "webClientSession.h"   // typed Session() + its SetFrame back-link

ibWebFrame::ibWebFrame(ibSessionHolder&& holder, ibWebApplication* app)
	: ibBackendDocFrame(std::move(holder)), m_app(app)
{
	// The session learns its window here, in one visible line, the same
	// moment it becomes ours. wes has no main-window singleton to ask
	// (many tabs at once), so unlike the desktop pair this link has to be
	// stored — but it is set exactly where ownership is taken, not
	// patched in by whoever happened to build the frame.
	//
	// Guarded: a frame built with an empty holder (tests) has no session
	// to tell, and that is not a reason to crash.
	if (auto* session = Session())
		session->SetFrame(this);
}

ibWebClientSession* ibWebFrame::Session() const
{
	return static_cast<ibWebClientSession*>(GetSession());
}

ibWebFrame::~ibWebFrame()
{
	// Undo the constructor's back-link first: the session must not point
	// at a window that is already unwinding, and a late
	// ibSession::CurrentFrame() during teardown would otherwise hand out
	// a freed pointer. Symmetric with the ctor — bound where ownership is
	// taken, cleared where it ends, with nothing to remember outside.
	if (auto* session = Session())
		session->SetFrame(nullptr);

	// The OnExit path is responsible for running DeleteAllViews on every
	// tab's doc BEFORE deleting the frame — that must happen on the
	// session worker thread so procUnit's per-thread state is valid.
	// By the time we reach this dtor (on the HTTP thread during session
	// destroy), docs/views/hosts are already gone; only the tab shells
	// remain. Dereferencing tab->GetDocument() here would dynamic_cast
	// a dangling pointer and wedge the server.
	//
	// Just clear the tab vector — ~ibWebDocChildFrame only nulls its
	// (already-dangling) m_view/m_doc, no UB.
	m_pendingCloses.clear();
	m_activeForm = nullptr;
	m_tabs.clear();
	// Answer every question still waiting with 0 — no button, read as
	// cancelled — and wake its script, so it unwinds instead of waiting
	// for a client nobody will answer for after we go away.
	std::vector<PendingModal> drained;
	{
		std::lock_guard<std::mutex> g(m_modalMutex);
		drained.swap(m_pendingModals);
	}
	for (auto& m : drained)
		m.reply->received.store(true);
	ibSession* const session = Session();
	if (ibWorkerPool* const pool = !drained.empty() && session != nullptr ? session->GetWorkerPool() : nullptr)
		pool->Wake(session);
}

void ibWebFrame::SetStatusText(const wxString& strStatus, int /*number*/)
{
	m_status = strStatus;
}

void ibWebFrame::Message(const wxString& strMessage, ibStatusMessage status)
{
	// ibStatusMessage_{Information,Warning,Error} ∈ {1,2,3} — passed
	// through unchanged for the client's level styling.
	std::lock_guard<std::mutex> g(m_msgMutex);
	m_pendingMessages.push_back({ static_cast<int>(status), strMessage });
}

void ibWebFrame::ClearMessage()
{
	// Drop any unread lines AND tell the client to wipe its panel on
	// the next poll. The flag is one-shot; consumed by TakeClearPending.
	std::lock_guard<std::mutex> g(m_msgMutex);
	m_pendingMessages.clear();
	m_clearPending = true;
}

void ibWebFrame::BackendError(const wxString& /*strFileName*/,
	const wxString& /*strDocPath*/, const long /*line*/,
	const wxString& strErrorMessage) const
{
	// Backend error → output panel as an error-level line. File / doc
	// path / line are dropped for now; future surface could format
	// them inline (e.g. "ERR <file>:<line>: <msg>").
	std::lock_guard<std::mutex> g(m_msgMutex);
	m_pendingMessages.push_back({
		static_cast<int>(ibStatusMessage_Error), strErrorMessage });
}

std::vector<ibWebFrame::PendingMessage> ibWebFrame::DrainPendingMessages() const
{
	std::lock_guard<std::mutex> g(m_msgMutex);
	std::vector<PendingMessage> out;
	out.swap(m_pendingMessages);
	return out;
}

bool ibWebFrame::TakeClearPending() const
{
	std::lock_guard<std::mutex> g(m_msgMutex);
	const bool was = m_clearPending;
	m_clearPending = false;
	return was;
}

namespace {
// Process-wide monotonic counter for modal ids. Cheap, sufficient since
// the id only needs to be unique among in-flight modals.
std::atomic<std::uint64_t> s_modalIdCounter{ 0 };

std::string MakeModalId()
{
	std::ostringstream os;
	os << "m" << s_modalIdCounter.fetch_add(1, std::memory_order_relaxed);
	return os.str();
}
} // namespace

int ibWebFrame::ShowModalMessage(const wxString& message,
	const wxString& caption, int style)
{
	// Queue the question and WAIT IN THE SESSION'S POOL (ibWorkerPool::Await). This thread holds the session, so
	// it goes on running the session's tasks meanwhile — the /session poll that carries the question to the
	// client among them. It used to park on a promise, and that poll, a task of this same session, queued behind
	// it: the question never reached the client (audit 2026-10-05).
	ibSession* const session = Session();
	ibWorkerPool* const pool = session != nullptr ? session->GetWorkerPool() : nullptr;
	if (pool == nullptr)
		return 0;   // nobody to ask — 0 == no button, the caller reads it as cancelled

	const auto reply = std::make_shared<ModalReply>();
	const std::string id = MakeModalId();
	{
		std::lock_guard<std::mutex> g(m_modalMutex);
		m_pendingModals.push_back(PendingModal{ id, message, caption, style, reply });
	}
	try {
		pool->Await(session, [reply]() { return reply->received.load(); });
	}
	catch (...) {
		// Cancelled while asking — a kick, a closed tab, the session stopping: the question is withdrawn and the
		// interruption goes on up the script.
		std::lock_guard<std::mutex> g(m_modalMutex);
		m_pendingModals.erase(std::remove_if(m_pendingModals.begin(), m_pendingModals.end(),
			[&id](const PendingModal& m) { return m.id == id; }), m_pendingModals.end());
		throw;
	}
	return reply->code.load();
}

bool ibWebFrame::HasPendingModal() const
{
	std::lock_guard<std::mutex> g(m_modalMutex);
	return !m_pendingModals.empty();
}

ibWebFrame::PendingModal ibWebFrame::PeekPendingModal() const
{
	std::lock_guard<std::mutex> g(m_modalMutex);
	return m_pendingModals.front();   // caller checked HasPendingModal()
}

bool ibWebFrame::ResolveModal(const std::string& id, int result)
{
	std::shared_ptr<ModalReply> reply;
	{
		std::lock_guard<std::mutex> g(m_modalMutex);
		for (auto it = m_pendingModals.begin(); it != m_pendingModals.end(); ++it) {
			if (it->id == id) {
				reply = it->reply;
				m_pendingModals.erase(it);
				break;
			}
		}
	}
	if (!reply) return false;   // unknown, or answered already (a duplicate reply)
	reply->code.store(result);
	reply->received.store(true);
	ibSession* const session = Session();
	if (ibWorkerPool* const pool = session != nullptr ? session->GetWorkerPool() : nullptr)
		pool->Wake(session);
	return true;
}

ibBackendValueForm* ibWebFrame::CreateNewForm(
	const ibFormRequest& request,
	const ibValueMetaObjectFormBase* creator,
	ibBackendControlFrame* backendControl,
	ibSourceDataObject*    srcObject)
{
	std::cerr << "[tabs] CreateNewForm creator=" << (void*)creator
		<< " tabs_before=" << m_tabs.size() << std::endl;
	// Low-level factory, mirror of desktop ibFrontendMainFrame::
	// CreateNewForm: just allocates the ibValueForm ref, no LoadFormData
	// / BuildForm. ibValueMetaObjectFormBase::CreateAndBuildForm calls
	// ibBackendValueForm::CreateNewForm which lands here — if we
	// re-dispatched through CreateAndBuildForm from this method we'd
	// recurse infinitely (OnStart script tab-spawn, 2026-04-19). Callers
	// that need BuildForm fallback (sidebar click via OpenFormInSession)
	// invoke CreateAndBuildForm themselves above this call.
	//
	// Parent descriptor wiring happens inside ibValueForm — it has
	// ownerControl + access to backend_mainFrame for the UI fallback.
	ibControlFrame* ownerControl = dynamic_cast<ibControlFrame*>(backendControl);
	return new ibValueForm(
		request, creator, ownerControl, srcObject);
}

ibUniqueKey ibWebFrame::CreateFormUniqueKey(
	const ibBackendControlFrame* ownerControl,
	const ibSourceDataObject* sourceObject,
	const ibUniqueKey& formGuid)
{
	// Same fallback chain desktop uses (formGuid → ownerControl
	// guid → sourceObject guid → fresh random guid). Without this,
	// ibBackendDocFrame's default returns wxNullUniqueKey for
	// every form, breaking FindDocByUniqueKey.
	return ibFormVisualDocument::CreateFormUniqueKey(ownerControl, sourceObject, formGuid);
}

void ibWebFrame::AdoptTab(std::unique_ptr<ibWebDocChildFrame> tab, ibValueForm* form)
{
	if (tab == nullptr) return;
	m_activeTab  = m_tabs.size();
	m_tabs.push_back(std::move(tab));
	if (form != nullptr) m_activeForm = form;
	std::cerr << "[tabs] AdoptTab form=" << (void*)form
		<< " now " << m_tabs.size() << " tab(s)" << std::endl;
}

ibFrontendWindow* ibWebFrame::CreateChildFrame(
	ibView* view,
	const wxPoint& /*pos*/,
	const wxSize&  /*size*/,
	long           /*style*/)
{
	// Web-side static factory — mirror of the desktop
	// ibFrontendMainFrame::CreateChildFrame. Works with any ibDocument
	// subclass (form, tabular, text, report, audit log …), not just
	// ibFormVisualDocument. Title comes from the doc's GetTitle(); any
	// per-type-specific wiring (e.g. ibValueForm tracking for
	// ActiveWindow() on form tabs) happens in a narrow downcast below,
	// leaving non-form docs untouched. Param type relaxed from
	// ibMetaView*/ibMetaDocument* to ibView*/ibDocument* after AuditLog /
	// Text / Help were rebased off the meta hierarchy.
	if (view == nullptr) return nullptr;

	// Per-tab session is pinned by the worker loop's ibSessionScope; its
	// GetFrame() is our ibWebFrame. No helper needed — direct path.
	ibSession* session = ibSession::Current();
	auto* webFrame = session != nullptr
		? dynamic_cast<ibWebFrame*>(session->GetFrame()) : nullptr;
	if (webFrame == nullptr) return nullptr;

	ibDocument* doc = view->GetDocument();
	// doc->GetTitle() is still empty at this stage — the doc was just
	// constructed and its title is set by ibFormVisualDocument::OnCreate,
	// which the caller invokes AFTER CreateChildFrame. Pull the title
	// from the ibValueForm directly when the doc is a form doc, so the
	// tab strip doesn't flash "#0/#1/..." (its fallback when both
	// t.title and t.formName are empty) during the first serialize.
	wxString title = doc != nullptr ? doc->GetTitle() : wxString();
	if (title.IsEmpty()) {
		if (auto* formDoc = dynamic_cast<ibFormVisualDocument*>(doc)) {
			if (ibValueForm* valueForm = formDoc->GetValueForm())
				title = valueForm->GetControlTitle();
		}
	}

	auto childFrame = std::make_unique<ibWebDocChildFrame>(
		doc, view, /*parent*/ webFrame, title);
	ibWebDocChildFrame* raw = childFrame.get();

	// Wire the view's web-frame back-pointer so ibMetaView::ShowFrame
	// can reach this tab and flip its shown/active state. Desktop gets
	// the same effect via ibView::SetFrame (done by ibDocChildFrame
	// ctor); web plumbs it explicitly here.
	view->SetWebFrame(raw);

	// Only form-docs feed the frame's ActiveWindow()-tracking slot;
	// other doc types (future tabular / text / report views) still
	// get a tab but don't pollute m_activeForm.
	ibValueForm* form = nullptr;
	if (auto* visualDoc = dynamic_cast<ibFormVisualDocument*>(doc))
		form = visualDoc->GetValueForm();

	// Tab icon: prefer the document's icon (set in
	// ibFormVisualDocument::OnCreate from the source meta-object —
	// Catalog gets the catalog icon, Document gets the document icon,
	// etc.). Fall back to ibValueForm::GetIcon (built-in XPM) only
	// when the doc didn't set one. Without this preference order every
	// tab showed the generic form icon regardless of source type.
	wxIcon tabIcon;
	if (doc != nullptr) tabIcon = doc->GetIcon();
	if (!tabIcon.IsOk() && form != nullptr) tabIcon = form->GetIcon();
	if (tabIcon.IsOk()) raw->SetIcon(tabIcon);

	webFrame->AdoptTab(std::move(childFrame), form);
	return raw;
}

void ibWebFrame::SetActiveTab(std::size_t i)
{
	if (i >= m_tabs.size())
		return;
	m_activeTab = i;
	ibVisualHostClient* host = m_tabs[i]->GetHost();
	m_activeForm = host != nullptr ? host->GetValueForm() : nullptr;
}

void ibWebFrame::MarkTabForCloseByForm(const ibValueForm* form)
{
	if (form == nullptr) return;
	// Dedup — the same form may be marked twice (e.g. beforeClose
	// script calls CloseForm again).
	for (const ibValueForm* q : m_pendingCloses)
		if (q == form) return;
	m_pendingCloses.push_back(form);
}

void ibWebFrame::DrainPendingCloses()
{
	if (m_pendingCloses.empty()) return;
	std::vector<const ibValueForm*> pending;
	pending.swap(m_pendingCloses);
	std::cerr << "[life] DrainPendingCloses count=" << pending.size() << std::endl;
	for (const ibValueForm* form : pending) {
		for (std::size_t i = 0; i < m_tabs.size(); ++i) {
			auto* visualDoc = dynamic_cast<ibFormVisualDocument*>(
				m_tabs[i]->GetDocument());
			if (visualDoc == nullptr || visualDoc->GetValueForm() != form)
				continue;
			std::cerr << "[life] DeleteAllViews form=" << (void*)form
				<< " doc=" << (void*)visualDoc << std::endl;
			// Now — outside any control's event handler — we can safely
			// destroy the view, which cascades into host and all its
			// child controls (toolbar, tools, textctrl etc.). ibDocument's
			// DeleteAllViews contract also deletes the doc itself when
			// the last view goes, so m_tabs[i]->m_doc becomes dangling
			// (tab dtor nulls it, doesn't delete).
			visualDoc->DeleteAllViews();
			std::cerr << "[life] after DeleteAllViews" << std::endl;

			if (i == m_activeTab)
				m_activeForm = nullptr;
			std::unique_ptr<ibWebDocChildFrame> dying =
				std::move(*(m_tabs.begin() + i));
			m_tabs.erase(m_tabs.begin() + i);
			if (m_tabs.empty()) {
				m_activeTab  = 0;
				m_activeForm = nullptr;
			} else {
				if (m_activeTab >= m_tabs.size())
					m_activeTab = m_tabs.size() - 1;
				auto* newDoc = dynamic_cast<ibFormVisualDocument*>(
					m_tabs[m_activeTab]->GetDocument());
				m_activeForm = newDoc != nullptr ? newDoc->GetValueForm() : nullptr;
			}
			break;
		}
	}
}

bool ibWebFrame::CloseTab(std::size_t i)
{
	if (i >= m_tabs.size())
		return false;

	// Single path for BOTH the toolbar ActionClose and the browser's tab-X:
	// call ibValueForm::CloseForm (which fires beforeClose with veto,
	// then onClose) and mark-for-deferred-close via MarkTabForCloseByForm.
	// Do NOT erase from m_tabs here — if we did, DrainPendingCloses' tab
	// lookup would fail (tab is already gone) and nobody would call
	// DeleteAllViews, so the view / host / ibFormVisualDocument /
	// ibValueForm would all leak. DrainPendingCloses runs at the end of
	// Dispatch and does the real teardown (DeleteAllViews cascade) plus
	// the erase + active-tab fixup.
	ibVisualHostClient* dyingHost = m_tabs[i]->GetHost();
	ibValueForm* dyingForm = dyingHost != nullptr ? dyingHost->GetValueForm() : nullptr;
	if (dyingForm != nullptr && !dyingForm->CloseForm())
		return false;   // script vetoed — tab stays
	return true;
}
