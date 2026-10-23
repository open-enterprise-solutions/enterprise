#ifndef __CLIENT_FRAME_H__
#define __CLIENT_FRAME_H__

// THE FRAME OF ONE CLIENT — what a person sees, held on the server: the tabs (each a document's view), the
// messages, the requests waiting for a response, the title and the status line. It is the ibBackendDocFrame
// the backend reaches through ibSession::CurrentFrame() — a script's OpenForm, Message, Question all land
// here — and the parent frame of the doc/view framework, so it holds this client's document manager, as the
// desktop's main window held its own. A client (a browser, the wx renderer, an assistant over MCP) draws it
// from what the protocol answers; nothing here is a window.
//
// ⭐ AND IT IS THE PROTOCOL THE CLIENT SPEAKS. The desktop had a main window per program (designer.exe,
// enterprise.exe), each with the doc manager of its application — what it may open — and a start of its own. A
// client's frame is made for one mode in the same way: the mode's frame puts in its application's doc manager
// (its templates, its schemas) and starts it its own way.

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <wx/icon.h>

#include "backend/backend_mainFrame.h"
#include "backend/uniqueKey.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — a request and its response

#include "frmserver/docView/docView.h"     // ibDocParentFrameAnyBase — the document manager's slot

#include "clientMenu.h"

class ibValueForm;
class ibValueMetaObject;
class ibMetaData;
class ibClientChildFrame;
class ibClientDebugger;
class ibClientInstance;
class ibClientSession;

// What every mode's frame shares; each mode's frame is a class of its own (ibClientFrameRuntime,
// ibClientFrameDesigner).
class FRMSERVER_API ibClientFrame : public ibBackendDocFrame, public ibDocParentFrameAnyBase {
public:

	// The frame of a mode, built around an authenticated session — null for a mode the server does not have.
	static std::unique_ptr<ibClientFrame> Create(ibProtocol::ibLogin::ibMode mode, ibSessionHolder&& holder,
		ibClientInstance* clientInstance);

	virtual ~ibClientFrame() override;

	//***************************************************************************************
	//*           The start, as the desktop window's (ibFrontendMainFrame::Show)            *
	//***************************************************************************************

	// THE MODE'S VETO POINT — the runtime runs the configuration's start, the designer asks for the right to
	// edit it. False: the client is refused, and the frame goes with its session.
	virtual bool AllowRun() = 0;

	// The mode's own tabs, after the start — the runtime's start page.
	virtual void CreateStartupPage() {}

	// THE MODE'S OWN PART OF THE FRAME, beside its tabs — the designer's configuration tree: written into the frame's
	// state with every answer (ibClientHost::DrawFrame), as a view writes its own (ibView::OnDraw). Nothing by default.
	virtual void OnDraw(ibDataNode& /*state*/) {}

	// …and THE MODE'S DEBUGGER — the designer's, a listener of the engine's (ibClientDebugger): what the client's debug
	// calls go to (ibClientHost::Debug). None by default.
	virtual ibClientDebugger* GetDebugger() const { return nullptr; }

	//***************************************************************************************
	//*           ibBackendDocFrame — the backend's door into this client                   *
	//***************************************************************************************

	virtual void SetTitle(const wxString& strTitle) override { m_title = strTitle; }
	virtual void SetStatusText(const wxString& strStatus, int number = 0) override;

	// Messages for the client, kept until it takes them. The desktop's message pane is the same thing.
	virtual void Message(const wxString& strMessage, ibStatusMessage status) override;
	virtual void ClearMessage() override;
	// A script's failure — the desktop's error window, asked of the client (ibProtocol::ibRequest::ibKind::Error).
	virtual void BackendError(const wxString& strFileName,
		const wxString& strDocPath, const long line,
		const wxString& strErrorMessage) override;

	// A REPAINT the backend asks for, the frame having changed by itself (a report delivered, a debugger stopped): the
	// client is told so (ibClientHost::NotifyChanged) and asks for it — the next frame it is sent is the repaint.
	virtual void RefreshFrame() override;
	// …and a RAISE — a debugger left its loop (ibDebuggerServer::DoDebugLoop): the window is the client's, so it is told
	// (Raise in the next answer, one-shot as Clear) and called for that answer (RefreshFrame).
	virtual void RaiseFrame() override;

	// Forms. CreateNewForm is what a script's OpenForm() ultimately reaches: the form is made here and
	// opened into a tab by its own ShowForm. ActiveWindow is the active tab's form.
	virtual ibBackendValueForm* ActiveWindow() const override;
	virtual ibBackendValueForm* CreateNewForm(
		const ibFormRequest& request,
		const class ibValueMetaObjectFormBase* creator,
		class ibBackendControlFrame* ownerControl = nullptr,
		class ibSourceDataObject* srcObject = nullptr) override;

	// The form registry — which open form is the one a key names. Asked of THIS client's documents: two
	// clients opening the same object hold two forms.
	virtual ibUniqueKey CreateFormUniqueKey(const ibBackendControlFrame* ownerControl,
		const ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid) override;
	virtual ibBackendValueForm* FindFormByUniqueKey(const ibBackendControlFrame* ownerControl,
		const ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid) override;
	virtual ibBackendValueForm* FindFormByUniqueKey(const ibUniqueKey& guid) override;
	virtual ibBackendValueForm* FindFormByControlUniqueKey(const ibUniqueKey& guid) override;
	virtual ibBackendValueForm* FindFormBySourceUniqueKey(const ibUniqueKey& guid) override;
	virtual bool UpdateFormUniqueKey(const ibUniqueKeyPair& guid) override;

	// A SHEET SHOWN — the desktop's (ibFrontendMainFrame::ShowSpreadsheetDocument): a spreadsheet document of its own
	// over it, in a tab of its own, under that title. What a list output and a script's Show() reach.
	virtual bool ShowSpreadsheetDocument(const wxString& strTitle, std::shared_ptr<ibSpreadsheetObject>& spreadSheetDocument) override;
	// …and a script's sheet printed, by the client's printer (ibProtocol::ibRequest::ibKind::Print) — with its print dialog,
	// as the desktop's always was.
	virtual bool PrintSpreadsheetDocument(const std::shared_ptr<ibSpreadsheetObject>& doc, bool showPrintDlg = true) override;

	// A SCHEDULE EDITED — the desktop's editor, asked of the client (ibProtocol::ibRequest::ibKind::Schedule). OK marks the active
	// form modified, as the desktop's did: the edit happened in a window of its own, nothing else would notice it.
	virtual bool ShowScheduleEditor(ibJobScheduleDescription& schedule) override;

	//***************************************************************************************
	//*                       The client's side of the frame                                *
	//***************************************************************************************

	// THE TAB A VIEW SHOWS IN — called by the doc/view pipeline (ibDocument::OnCreate) for a view that is not
	// composed into its parent. Made in the CURRENT session's frame and made its active tab; null when the
	// current session has no client frame.
	static ibClientChildFrame* CreateChildFrame(ibView* view);

	// THE CURRENT SESSION'S FRAME, when it is a client's — what the desktop's main window answered from its own
	// GetFrame, in a process that holds many clients.
	static ibClientFrame* GetFrame();

	// The client is leaving: whatever is still open goes down unconditionally, as the desktop window's Destroy
	// closed its documents. The forced close still ASKS each view (DeleteAllViews → Close(false)), so the
	// documents that belong to the frame — the start page — see that it is the frame closing them
	// (IsClosingWindow), not the person's tab cross.
	void CloseDocuments();
	bool IsClosingWindow() const { return m_closingWindow; }

	// A LOCKED tab — the start page's: kept ahead of every normal tab whenever it joined, and closed only with the
	// frame (CloseTab refuses it), as the desktop's notebook kept wxAuiTabKind::Locked.
	void LockTab(const ibDocChildFrameAnyBase* tab);

	// The client instance this frame belongs to.
	ibClientInstance* GetClientInstance() const { return m_clientInstance; }

	// The active tab's form, as the frame holds it — what ActiveWindow answers the backend, without the
	// backend's narrowing; null for a tab of any other document, or none.
	ibValueForm* ActiveForm() const;

	// GetSession() comes from ibBackendDocFrame — the holder this frame owns. This is the typed view of the
	// same thing, the ONE place a client's session pointer is narrowed.
	ibClientSession* Session() const;

	const wxString& GetTitle()      const { return m_title; }
	const wxString& GetStatusText() const { return m_status; }

	// The application's icon, when the configuration has one; a tab without an icon of its own shows it.
	void          SetIcon(const wxIcon& icon) { m_icon = icon; }
	const wxIcon& GetIcon() const             { return m_icon; }

	struct PendingMessage {
		ibStatusMessage level;
		wxString        text;
	};
	std::vector<PendingMessage> DrainPendingMessages();
	bool                        TakeClearPending();
	// The client is to go — Exit done: every tab closed. A one-shot flag, as Clear, told with the next answer.
	bool                        TakeExitPending();
	// …and its window to be raised (RaiseFrame), one-shot as well.
	bool                        TakeRaisePending();
	// ⭐ THE CLIENT TOLD TO GO — its own Exit (DoCommand): every tab but the locked ones closed (CloseAll), each may keep
	// itself, and then the client stays. FORCED — the session closed under it (a debugger's Stop, an administrator's
	// kick: OnClose): nothing is asked, and a client at rest is called for it (RefreshFrame) — it hears Exit in the next
	// answer, or in the answer of the call it is in, and goes as from its own Exit: its logout takes the session.
	bool                        ExitClient(bool force);
	// THE INSPECTOR'S ROWS OF AN OBJECT A DOOR ACTED ON — an edit's, a new object's, the one the tree goes to after a
	// removal, a form editor's own: written now, as the inspector's read writes them (ibSchemaProperties::Build), and told
	// with the next answer (Properties) — the client shows them without reading them again. `tabId` — the tab whose tree
	// the client holds the object of (0: the configuration's).
	void                        ShowProperties(ibValueMetaObject* object, std::int32_t tabId);
	std::vector<ibDataNode>     TakeShownProperties();

	struct PendingRequest {
		std::string id;
		ibDataNode  request;
	};
	bool           HasPendingRequest() const;
	PendingRequest PeekPendingRequest() const;   // the newest — the one waited on now; safe once HasPendingRequest()
	bool           Respond(const std::string& id, const ibDataNode& response);
	// The newest question said anew, as it is still waiting — what a form opened over its window chose, put into it
	// (the window reads it on the next frame).
	void           ReplacePendingRequest(const ibDataNode& request);

	// Tabs, in the order they were opened.
	std::size_t         TabCount()  const { return m_tabs.size(); }
	std::size_t         ActiveTab() const { return m_activeTab; }
	ibClientChildFrame* Tab(std::size_t i) const { return i < m_tabs.size() ? m_tabs[i].get() : nullptr; }

	// Where the tab of that id stands — TabCount() when the frame has none (a client names a tab by its id).
	std::size_t FindTab(std::int32_t id) const;

	void SetActiveTab(std::size_t i);

	// Close the tab's form the way its own Close does — the form's BeforeClose may refuse, and then the tab
	// stays. The tab itself is erased later, by DrainPendingCloses.
	bool CloseTab(std::size_t i);
	// …and every tab but the locked ones, each that road — one kept keeps the rest, as the desktop's Window → Close All
	// (wxAuiMDIParentFrame::CloseAll) stopped at a child that refused. In one call: the client's AUI loop closed one per
	// press, its tab's Close only posted. Exit begins with it (ExitClient).
	bool CloseAll();

	// The tabs that closed go now — at the end of a request, when nothing on the stack is still inside them:
	// each one's view first, and with the last view the document (ibDocument::DeleteAllViews), then the tab.
	void DrainPendingCloses();

	// A COMMAND ON THE ACTIVE TAB'S DOCUMENT — what the desktop's doc manager did with its current document from the
	// menu (OnUndo, OnFileSave, …) and what it enabled there (OnUpdate…): the active tab is the frame's, so it is
	// asked here. Close goes the one road every close goes (CloseTab). The window's own — Open, Exit, Close All, the edit
	// commands — are done here too: what of them is the client's, the frame asks of the client (a request).
	bool IsCommandEnabled(ibDocCommand command) const;
	// `name` — Save as's: the file's, as the client chose it.
	bool DoCommand(ibDocCommand command, const wxString& name);
	// …and the same question of any tab's document — what the frame tells the client of each tab (its Commands), so the
	// client's own doc manager enables its File and Edit without asking. The window's own (Open, Exit, Close All) are no
	// tab's.
	bool IsCommandEnabled(ibDocCommand command, std::size_t tab) const;
	// THE DOCUMENT SAVE AS IS ASKED OF — the desktop's (ibFormVisualEditView::OnActiveControlCommand): the one behind the
	// form's active control while it holds a view of its own, a grid box's sheet; the tab's otherwise.
	ibDocument* GetSaveAsDocument(std::size_t tab) const;

	// The menu — the application's, as the desktop's main frame had its menu bar beside File and Edit: those are the
	// client's own doc manager's (frmclient/docView).
	const ibClientMenu& GetMenu() const { return m_menu; }

	// The person may use the schema of that kind: the application has it, and its own right lets them through
	// (ibClientSchema::AccessRight) — what the menu offers, and what a call of it is refused by.
	bool IsSchemaAllowed(ibProtocol::ibSchema::ibKind kind) const;

protected:

	// Built around an authenticated session and owning it from that moment: the frame going is what ends the
	// session — no separate logout bookkeeping. Made by a mode's frame, which puts its own doc manager in, and
	// builds its menu.
	ibClientFrame(ibSessionHolder&& holder, ibClientInstance* clientInstance);

	// ⭐ A REQUEST TO THE CLIENT — queued for it and waited for IN THE SESSION'S POOL (ibWorkerPool::Await):
	// this thread holds the session and goes on running its tasks meanwhile, the one that carries the request
	// to the client among them. Responded to, cancelled (a kick, a closed tab, the session stopping: the
	// request is withdrawn and the interruption goes on up), or never — while a request is pending, the frame
	// refuses everything else a client asks of it (HasPendingRequest).
	virtual bool DoRequest(const ibDataNode& request, ibDataNode& response) override;

	// A message box is one kind of request (ibProtocol::ibRequest::ibKind::Message: Text, Caption, Style — the wx button
	// flags; the response's Button is the wx code of the button pressed, 0 for none).
	virtual int DoShowModalMessage(const wxString& message, const wxString& caption, int style) override;

	ibClientMenu m_menu;

private:

	// What a tab asks of its frame: to become the active one (ibClientChildFrame::Show), to be closed at the
	// end of the request (ibClientChildFrame::Close).
	bool ActivateTab(const ibClientChildFrame* tab);
	void MarkTabForClose(const ibClientChildFrame* tab);
	friend class ibClientChildFrame;

	// The window's own commands (DoCommand; Exit is ExitClient, above). Edit: the client asked to do it to the field
	// under its focus. Open: the person asked for a file of theirs, which the client uploads, and the file opened by the
	// template its name says.
	bool EditFocused(ibDocCommand command);
	bool OpenChosenFile();

	ibClientInstance* m_clientInstance = nullptr;   // borrowed; the instance owns the frame
	wxString             m_title;
	wxString             m_status;
	wxIcon               m_icon;

	// One entry per tab. A tab goes after its document's views (DeleteAllViews takes the view and the
	// document), never before.
	std::vector<std::unique_ptr<ibClientChildFrame>> m_tabs;
	std::size_t                                      m_activeTab = 0;
	std::int32_t                                              m_lastTabId = 0;   // the last id a tab was given; never reused

	std::vector<const ibClientChildFrame*> m_pendingCloses;

	// Raised by CloseDocuments — who is closing the documents: the frame, not a person (IsClosingWindow).
	bool m_closingWindow = false;

	// Messages waiting for the client, guarded by m_msgMutex: the session's worker appends, the host drains.
	std::mutex                  m_msgMutex;
	std::vector<PendingMessage> m_pendingMessages;
	bool                        m_clearPending = false;
	bool                        m_exitPending = false;
	bool                        m_raisePending = false;
	std::vector<ibDataNode>     m_shownProperties;

	// The requests waiting, oldest first. A response is a slot of its own, shared with the waiting script —
	// responded to, the script reads the slot and not the frame.
	struct Response {
		std::atomic<bool> received{ false };
		bool              responded = false;   // written before `received` — false: withdrawn, nothing chosen
		ibDataNode        response;
	};
	struct Waiting {
		std::string               id;
		ibDataNode                request;
		std::shared_ptr<Response> response;
	};
	mutable std::mutex   m_requestMutex;
	std::vector<Waiting> m_waiting;
};

#endif
