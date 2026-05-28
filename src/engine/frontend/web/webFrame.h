#ifndef __WEB_FRAME_H__
#define __WEB_FRAME_H__

// Web analogue of the desktop "main frame" (wxAuiMDIParentFrame +
// ibBackendDocFrame). No wx MDI: tabs are a plain vector of
// ibVisualHostClient instances owned by the frame. When a script
// (OpenForm / ActionForm / ...) asks for a new form, CreateNewForm
// builds the ibValueForm via the same factory the desktop uses,
// wraps it in an ibVisualHostClient and appends it to m_tabs. Closing
// a tab erases its unique_ptr from the vector — the host destructor
// releases its ibValuePtr<ibValueForm>, and when no other reference
// holds the form it's freed automatically (RAII, no manual delete).

#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <wx/icon.h>

#include "backend/backend_mainFrame.h"
#include "backend/uniqueKey.h"

#include "frontend/frontendTypes.h"
#include "webWindow.h"

#include <wx/gdicmn.h>   // wxPoint, wxSize

class ibBackendValueForm;
class ibMetaView;
class ibWebDocChildFrame;

// Root of the web control tree for one session. Inherits both:
//   * ibBackendDocFrame — so backend code (scripts, OpenForm, ...)
//     reaches us through the existing singleton pointer;
//   * ibWebWindow — so the frame is the top node in the same
//     parent/child hierarchy as the controls it hosts and is
//     serialisable into the JSON response alongside them.
class ibWebApplication;

class ibWebFrame : public ibBackendDocFrame, public ibWebWindow {
public:
	explicit ibWebFrame(ibWebApplication* app);
	virtual ~ibWebFrame() override;

	// Back-pointer to the session's owning application. Lets
	// arbitrary web-side code reach the per-session dispatcher /
	// timer map via the thread_local main-frame singleton
	// (ibBackendDocFrame::GetDocMDIFrame), without threading a
	// second thread_local or a separate globals table.
	ibWebApplication* GetApp() const { return m_app; }

	// Per-cookie session this frame drives. Forwarded from the owning
	// ibWebApplication which has the session pointer set at Login time
	// (ibWebSession::Login → app->SetSessionContext(ticket->Session())).
	// Used by UI-originated paths (script OpenForm from OnStart, sidebar
	// clicks) where no ownerControl is available to walk a descriptor
	// parent chain up to the session.
	virtual ibSession* GetSession() const override;

	// ibWebWindow
	virtual wxString GetControlType() const override { return wxT("frame"); }

	// Back-compat surface. Desktop code reaches through this to get
	// the wx host for parent-window resolution; web has no wx frame,
	// so we return nullptr and expect callers that actually need a
	// native parent to guard against it.
	virtual wxFrame* GetFrameHandler() const override { return nullptr; }

	virtual void SetTitle(const wxString& strTitle) override { m_title = strTitle; }
	virtual void SetStatusText(const wxString& strStatus, int number = 0) override;

	// Backend → client notification primitives. Each pushes into an
	// in-memory queue drained by the next /session HTTP poll; client
	// applies title / status text / output lines on receipt. Mirrors
	// desktop's wxFrame::SetStatusText / wxLogMessage / wxMessageBox
	// destinations.
	virtual void Message(const wxString& strMessage, ibStatusMessage status) override;
	virtual void ClearMessage() override;
	virtual void BackendError(const wxString& strFileName,
		const wxString& strDocPath, const long line,
		const wxString& strErrorMessage) const override;
	// Blocking modal message box. Mirrors desktop wxMessageBox via
	// frame->ShowModalMessage(message, caption, style). The worker
	// thread that ran the script parks on a future; the next /session
	// poll surfaces the modal to the client, the client renders a
	// dialog with buttons derived from `style` (wxOK / wxYES_NO /
	// wxCANCEL), POSTs the chosen wx button code to /modal-reply/<id>,
	// the HTTP handler resolves the future, and the worker unblocks
	// returning the selected button code.
	virtual int ShowModalMessage(const wxString& message,
		const wxString& caption, int style) override;

	// Modal queue accessors. Used by:
	//   • SessionInfoFromSession — TakePendingModalForClient() returns
	//     the topmost queued modal's metadata (id/message/caption/style)
	//     without removing it; it stays in the queue until the client
	//     POSTs /modal-reply with the chosen code.
	//   • wfrontendModalReply — ResolveModal() looks up by id, sets the
	//     promise's value (waking the parked worker), removes from
	//     queue.
	struct PendingModal {
		std::string id;
		wxString    message;
		wxString    caption;
		int         style;
		std::shared_ptr<std::promise<int>> reply;
	};
	bool        HasPendingModal() const;
	PendingModal PeekPendingModal() const;   // copy of top modal; safe if HasPendingModal()
	bool        ResolveModal(const std::string& id, int result);

	// Notification queue accessors. Used by wfrontend.cpp when emitting
	// the /session JSON payload — drain returns the accumulated lines
	// and clears the queue so subsequent polls don't re-deliver. The
	// clear-pending flag is set by ClearMessage and consumed once.
	struct PendingMessage {
		int      level;     // 1=info, 2=warn, 3=error
		wxString text;
	};
	std::vector<PendingMessage> DrainPendingMessages() const;
	bool                        TakeClearPending() const;

	// Repaint/raise are desktop concepts (native window redraw, bring
	// to front). No-op here — the HTTP response itself is the "refresh".
	virtual void RefreshFrame() override {}
	virtual void RaiseFrame() override {}

	// Form support. CreateNewForm is what script OpenForm() ultimately
	// calls. Desktop opens a wxAuiMDIChildFrame + ibVisualHost; here we
	// build an ibValueForm via the same factory and wrap it in an
	// ibVisualHostClient added to the tab list. The returned pointer
	// stays owned (via ibValuePtr refcounting from the host) by the tab
	// entry — closing the tab releases it.
	virtual ibBackendValueForm* ActiveWindow() const override { return m_activeForm; }
	virtual ibBackendValueForm* CreateNewForm(
		const class ibValueMetaObjectFormBase* creator,
		class ibBackendControlFrame* ownerControl = nullptr,
		class ibSourceDataObject* srcObject = nullptr,
		const ibUniqueKey& formGuid = wxNullUniqueKey) override;

	// Override mirrors ibFrontendDocMDIFrame's — delegates to
	// ibFormVisualDocument::CreateFormUniqueKey so the form's
	// m_formKey gets a proper unique guid. Without this override,
	// the backend base returns wxNullUniqueKey and every form
	// collides on FindDocByUniqueKey — only the first tab ever
	// appears, later forms silently activate the first.
	virtual ibUniqueKey CreateFormUniqueKey(
		const class ibBackendControlFrame* ownerControl,
		const class ibSourceDataObject* sourceObject,
		const ibUniqueKey& formGuid) override;

	// Web-side analogue of ibFrontendDocMDIFrame::CreateChildFrame.
	// Static by symmetry with the desktop factory — called from the
	// shared doc/view pipeline (ibMetaDocument::OnCreate) right after
	// DoCreateView. Builds an ibWebDocChildFrame, hands it to the
	// current session's ibWebFrame via AdoptTab, and returns it as
	// ibFrontendWindow* so the signature matches desktop.
	static ibFrontendWindow* CreateChildFrame(ibMetaView* view,
		const wxPoint& pos  = wxDefaultPosition,
		const wxSize&  size = wxDefaultSize,
		long           style = 0);

	// Tab management. m_activeTab indexes m_tabs (undefined when the
	// vector is empty — callers guard on TabCount()).
	std::size_t TabCount()    const { return m_tabs.size(); }
	std::size_t ActiveTab()   const { return m_activeTab; }
	ibWebDocChildFrame* Tab(std::size_t i) const {
		return i < m_tabs.size() ? m_tabs[i].get() : nullptr;
	}
	// Take ownership of an externally-built ibWebDocChildFrame and
	// install it as the active tab. Used by the shared doc/view
	// factory (ibFrontendDocMDIFrame::CreateChildFrame on web) where
	// the child frame is spawned before the form's view OnCreate
	// attaches its host — the caller has the raw pointer it needs
	// to wire SetHost after adoption.
	void AdoptTab(std::unique_ptr<class ibWebDocChildFrame> tab,
		class ibValueForm* form);

	void SetActiveTab(std::size_t i);
	// Returns false when the form's beforeClose script vetoed; tab
	// stays open in that case, caller should leave m_activeTab alone
	// and return {} to the client.
	bool CloseTab(std::size_t i);

	// Defer-mark the tab whose host owns the given form for removal.
	// Called from ibValueForm::CloseForm on the web build. Can't
	// destroy the tab synchronously — the caller chain might still be
	// processing a wxEvent on the toolbar/control that lives inside
	// the dying tab (classic reentrancy when ActionClose fires from
	// OnTool). The tab is dropped on the next DrainPendingCloses()
	// call by the Dispatch epilogue.
	void MarkTabForCloseByForm(const class ibValueForm* form);
	// Process any queued MarkTabForCloseByForm requests. Safe to call
	// when the wxEvent chain has unwound — the toolbar/control that
	// bubbled the event back here is no longer on the stack.
	void DrainPendingCloses();

	const wxString& GetTitle()      const { return m_title; }
	const wxString& GetStatusText() const { return m_status; }

	// Frame-level window icon (desktop: wxAuiMDIParentFrame::SetIcon).
	// Surfaced via /session as the app icon so a future browser-tab
	// favicon endpoint can serve it. Unused tabs inherit this fallback
	// when they call SetIcon(nullptr) explicitly.
	void          SetIcon(const wxIcon& icon) { m_icon = icon; }
	const wxIcon& GetIcon() const             { return m_icon; }

private:
	ibWebApplication*   m_app = nullptr;  // borrowed; app owns us
	wxString            m_title;
	wxString            m_status;
	wxIcon              m_icon;
	ibBackendValueForm* m_activeForm = nullptr;

	// One entry per open form. Each child frame owns its doc/view/host
	// triad; closing a tab = erase from the vector = dtor chain releases
	// host, then view, then document (mirrors ibAuiDocChildFrame order).
	std::vector<std::unique_ptr<ibWebDocChildFrame>> m_tabs;
	std::size_t                                      m_activeTab = 0;

	// Forms pending tab removal. Populated by MarkTabForCloseByForm;
	// drained after the event handler chain unwinds.
	std::vector<const class ibValueForm*>            m_pendingCloses;

	// Backend-driven notifications waiting for the next /session poll.
	// Mutable because BackendError() on the base interface is `const`
	// (called from logging paths that promise not to mutate the frame's
	// "structural" state); appending a pending line is a benign mutation
	// to internal queues, guarded by m_msgMutex for thread safety.
	mutable std::mutex                  m_msgMutex;
	mutable std::vector<PendingMessage> m_pendingMessages;
	mutable bool                        m_clearPending = false;

	// Modal-message queue. ShowModalMessage pushes here and blocks on
	// the entry's promise; /modal-reply HTTP handler resolves the promise
	// and removes the entry. mutex shared with pending-messages would
	// cross-lock unnecessarily — modals are rarer, separate lock is
	// cheaper than coupling.
	mutable std::mutex                  m_modalMutex;
	mutable std::vector<PendingModal>   m_pendingModals;
};

#endif
