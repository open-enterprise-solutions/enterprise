#ifndef _FRMCLIENT_MAIN_FRAME_H__
#define _FRMCLIENT_MAIN_FRAME_H__

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <wx/wx.h>
#include <wx/aui/aui.h>

#include "frmclient/backend/backend_mainFrame.h"   // the frame an object asks for its inspector's selection
#include "frmclient/docView/docView.h"
#include "protocol/communicator.h"
#include "frmclient/mainFrame/settings/editorsettings.h"
#include "frmclient/mainFrame/settings/fontcolorsettings.h"

// The main window is created by the exe that has the holder, shown with
// Show() and destroyed by wx — only the lookup remains.
#define mainFrame            		 (ibFrontendMainFrame::GetFrame())

#define wxCREATE_SDI_FRAME 0x16000

class ibOutputWindow;
class ibObjectInspector;
class ibFormVisualDocument;

//pane
#define wxAUI_PANE_BOTTOM   wxT("bottomWindow")
#define wxAUI_PANE_PROPERTY wxT("propertyWindow")

// THE MAIN WINDOW — the desktop's ibFrontendMainFrame (frontend/mainFrame) carried over to the thin client: the same
// AUI frame, the same Luna art, the same doc manager it holds (ibDocParentFrameAnyBase) with the tabs as its documents.
// It holds no session and runs nothing: it holds a communicator and DRAWS the frame the server answered — the title, the
// status, the tabs, the messages, the server's own menus beside the window's File and Edit. A question the server asks
// is asked here and answered back; what the person does goes back as a call, and the answer is drawn again.
//
// ⭐ THE ROUTINE IS THE WINDOW'S, THE DOCUMENTS ARE THE SERVER'S. The doc manager answers the File and Edit menus, the
// toolbars and what may be pressed, as the desktop's did, and asks the server nothing for that; what is done to a
// document is a call (ibFormVisualDocument, frmclient/visualView/visualHostClient.h).
//
// Each mode's window is a class of its own on top of it — the runtime's (enterprise-thin) and the designer's (designer-thin),
// as on the desktop and as the server's ibClientFrameRuntime / ibClientFrameDesigner.
class FRMCLIENT_API ibFrontendMainFrame : public ibBackendDocFrame, public wxAuiMDIParentFrame, public ibDocParentFrameAnyBase {
public:

	virtual ~ibFrontendMainFrame();

	// Show does the whole opening: build the GUI, log in — the start runs there, and may say something or ask — draw
	// the frame, put the window up. False when the login or the start was refused; the person is told why.
	virtual bool Show(bool show = true) override;

#if wxUSE_MENUS
	virtual void SetMenuBar(wxMenuBar* pMenuBar) override;
#endif // wxUSE_MENUS

	virtual wxAuiMDIClientWindow* OnCreateClient() override;

	// bring window to front
	virtual void Raise() override;

	// The client goes with the window: logged out, and its session torn down by the server.
	virtual bool Destroy() override;

	// The process's main window.
	static ibFrontendMainFrame* GetFrame() { return s_instance; }

	// THE PROPERTIES PANE — the desktop's object inspector: what a view selects in it is shown there (a sheet's cells),
	// hidden until it is asked for.
	static ibObjectInspector* GetObjectInspector() {
		if (s_instance != nullptr)
			return s_instance->m_objectInspector;
		return nullptr;
	}

	bool IsShownInspector();
	void ShowInspector();

	// …and what it shows: the object selected in it (the desktop's property slot).
	virtual ibPropertyObject* GetProperty() const override;
	virtual bool SetProperty(ibPropertyObject* prop) override;

	// THE TAB A VIEW SHOWS IN — the desktop's factory: a tab of the process's main window, the view's document's title
	// and icon on it.
	static wxWindow* CreateChildFrame(ibFrontendView* view, const wxPoint& pos, const wxSize& size,
		long style = wxDEFAULT_FRAME_STYLE);

	// What it logged in to — the thin client's process is one mode, the window's.
	ibProtocolMode GetMode() const { return m_mode; }

	// The font and colours code is shown in — the messages' too (the desktop's window held them for its editors and its
	// output alike) — and how its editors edit.
	ibFontColorSettings GetFontColorSettings() const { return m_fontColorSettings; }
	ibEditorSettings    GetEditorSettings() const { return m_editorSettings; }

	// The window's own menu of that id — File, Edit: a document's undo names its commands in Edit (the desktop's).
	wxMenu* GetDefaultMenu(int idMenu) const;

	// A view activated — its own menu merged into the window's and its toolbar shown; deactivated, both disabled (the
	// desktop's ActivateView).
	void ActivateView(ibFrontendView* view, bool activate = true);

	// The panes laid out again, once the work under way is done.
	void UpdateManager() {
		if (!m_callUpdateFrameManager) {
			m_callUpdateFrameManager = true;
			CallAfter(&ibFrontendMainFrame::UpdateFrameManager);
		}
	}

	// A call of the window's, through the communicator: the frame drawn again after it, its messages shown, a
	// question it asks put to the person until the work settles. The answer as it came — a schema's tree, a fetch's
	// rows are read from it. False — refused, and the person told why.
	bool Call(ibProtocolMethod method, const ibProtocolNode& params, ibProtocolNode& answer);
	// A call nobody waits on — what the person did in a view, a tab chosen or closed: made after the event that
	// raised it, in the order posted, and never inside another call — but inside a question a call is asking, as the
	// desktop's modal loop runs the events of the windows over it (a choice form a window opened, CreateChildFrame):
	// what the person can reach while a window asks is the modal's to say.
	void Post(ibProtocolMethod method, const ibProtocolNode& params);
	// THE QUESTION THE SERVER'S CODE WAITS ON — none (not IsNode): it waits on nothing.
	ibProtocolNode GetRequest() const;
	// AN ANSWER THAT LEAVES ITS WINDOW OPEN — a window that acts more than once before it closes (the saved settings'
	// shelf) answers the question pending with each act: the server does it and asks again, and that question is the
	// one pending then — for the window still open, never put to the person anew. False — refused, and the person told.
	bool Respond(const ibProtocolNode& response);
	// A VALUE OF THE SERVER'S TYPE CHOSEN FOR A CELL OF THE WINDOW ASKING — the window answers its question with the act
	// (a list's settings: Act Choose), the server opens the value's quick choice or its choice form (a dialog over this
	// modal: CreateChildFrame), and the value chosen comes back with the window's question asked again (Chosen), to
	// the cell's ChoiceProcessing. False — no window asks.
	bool ChooseValue(class ibControlFrame* owner, const class ibValue& current);
	// A tab closed — by the server, which asks about what is unsaved; the tab goes when the frame says so. Whatever
	// closes it comes here: its button, its window closed (Escape, the application's), the window menu, File → Close.
	void CloseTab(long long tabId);
	// A control's fetch — a table's rows, a spreadsheet's cells; `params` — the control, its request, the view it is of:
	// the answer is no frame, nothing is drawn or asked, and a refusal is only false. It speaks to the communicator alone,
	// so a control's own thread may make it.
	bool Fetch(const ibProtocolNode& params, ibProtocolNode& answer);
	// A file of the person's, up to the server in its parts — `file` the id it was given. False: not read, or refused
	// (said to the person).
	bool UploadFile(const wxString& path, wxString& file);
	// …and a file of the server's down to the person's `path`, in its parts. False: not written, or refused (said to the
	// person).
	bool DownloadFile(const wxString& file, const wxString& path);

	// The tab of that id as the frame has it now — a node with no id when the frame has no such tab (closed by the
	// server); a tab's id is never 0.
	ibProtocolNode FindTab(long long tabId) const;
	// The tab the frame has active now — 0: none.
	long long GetActiveTab() const;
	// The form of that tab — none: no such tab drawn.
	ibFormVisualDocument* FindDocument(long long tabId) const;

protected:

	// hook the document manager into event handling chain here
	virtual bool TryBefore(wxEvent& event) override {
		// It is important to send the event to the base class first as wxMDIParentFrame overrides its TryBefore() to
		// send the menu events to the currently active child and the child must get them before our own
		// TryProcessEvent() is executed, not afterwards.
		return wxAuiMDIParentFrame::TryBefore(event) || TryProcessEvent(event);
	}

	// The window's own parts. The base builds what every mode has — the toolbars, the File and Edit menus, the
	// messages pane and the status bar; a mode's window adds its own on top and calls this first.
	virtual void CreateGUI();
	// …the properties pane among them, on the right (the desktop's).
	virtual void CreatePropertyPane();

	// What the window builds BY ITSELF once the start has run — the desktop's slot of the same name (its home page;
	// the server opens that one now). Called from Show after the login has settled: what is built here may ask the
	// server, which answers only once no question of the start is pending. Default: nothing; the runtime builds its
	// section panel.
	virtual void CreateStartupPage() {}

	// May the window go down? The server's Exit closes each tab but the start page, asking about what is unsaved; a tab
	// the person kept keeps the window.
	virtual bool AllowClose();

	// A schema of the menu, asked for and answered, shown. The base shows what both modes have (Active users, About); a
	// mode's window shows its own (the runtime's All functions) and leaves the rest to the base.
	virtual void ShowSchema(ibProtocolSchema schema, const ibProtocolNode& shown);

	// The communicator comes in with the window and the window holds it — and with it the client — from then on. The
	// login is the window's (Show), so the start's messages and questions are drawn into it.
	ibFrontendMainFrame(std::unique_ptr<ibCommunicator> communicator, ibProtocolMode mode,
		const wxString& user, const wxString& password, const wxString& title,
		const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_FRAME_STYLE, const wxString& name = wxASCII_STR(wxFrameNameStr));

	ibCommunicator& GetCommunicator() const { return *m_communicator; }

	class ibFrameManager : public wxAuiManager {
	public:
		ibFrameManager(wxWindow* managedWnd = nullptr,
			unsigned int flags = wxAUI_MGR_DEFAULT) :
			wxAuiManager(managedWnd, flags) {
		}

		void Refresh() { Repaint(); }
	};

	// Create frame manager
	ibFrameManager     m_mgr;
	ibOutputWindow*    m_outputWindow = nullptr;
	ibObjectInspector* m_objectInspector = nullptr;

private:

	// What a menu item of the server's does: a command, or a schema.
	struct ibMenuItem {
		bool schema = false;
		int  number = 0;
	};

	struct ibPostedCall {
		ibProtocolMethod method;
		ibProtocolNode   params;
	};

	// What the item node of the frame's Menu does.
	static ibMenuItem MenuItemOf(const ibProtocolNode& item);

	// The File and Edit menus — the desktop's, the doc manager's commands.
	void InitializeDefaultMenu();

	// The frame the communicator holds, onto the window — what the answer's patch changed in it (all of it, sent whole);
	// the answer's events with it.
	void Draw(const ibProtocolNode& answer);
	// The server's own menus, after File and Edit.
	void DrawMenu(const ibProtocolNode& menu);
	// The documents File → New and Open offer — the server's templates, each with its picture.
	void DrawTemplates(const ibProtocolNode& templates);
	// The tabs opened and closed as the frame has them.
	void DrawTabs(const ibProtocolNode& tabs);
	// The active tab, and its view drawn by what the patch changed in it (none: whole).
	void DrawView(long long active, const ibProtocolNode& view, const ibProtocolNode& patch);
	// Drawn — and the questions the frame carries put to the person, each answered back, until none is pending.
	void Settle(const ibProtocolNode& answer);
	// …the questions alone (Settle): each put to the person until none is pending — or the one pending is the window's
	// up already: it reads it itself, and a value chosen for its cell is handed over (TakeChosen).
	void Ask();
	// A value chosen for the cell of the window asking (ChooseValue), when the question says one was — handed to it once.
	void TakeChosen(const ibProtocolNode& request);
	// A question, put to the person; what they answered in `response` (nothing — cancelled).
	void ShowRequest(const ibProtocolNode& request, ibProtocolNode& response);
	// A call refused, said to the person — and a client whose session is gone, closed.
	void Refused(ibProtocolRefusal refusal, const wxString& error);
	// The first call posted, made — unless a call is under way: that one makes it when it has settled.
	void CallPosted();

	// The form whose tab is that page of the notebook.
	ibFormVisualDocument* DocumentAt(int page) const;
	// A form gone — out of the frame's (~ibFormVisualDocument).
	void RemoveDocument(const ibFormVisualDocument* document);
	friend class ibFormVisualDocument;

	void UpdateFrameManager();

	void OnCloseWindow(wxCloseEvent& event);
	void OnExit(wxCommandEvent& event);
	void OnMenu(wxCommandEvent& event);
	// Cut, Copy, Paste, Delete, Select all — the field under the focus does them.
	void OnEditCommand(wxCommandEvent& event);
	void OnTabChanged(wxAuiNotebookEvent& event);

	static ibFrontendMainFrame* s_instance;

	std::unique_ptr<ibCommunicator> m_communicator;
	const ibProtocolMode            m_mode;
	const wxString                  m_user;
	wxString                        m_password;   // until the login — cleared by it
	ibFontColorSettings             m_fontColorSettings;
	ibEditorSettings                m_editorSettings;

	wxAuiToolBar* m_mainFrameToolbar = nullptr;   // the window's: the doc manager's commands
	wxAuiToolBar* m_docToolbar = nullptr;         // the active view's own (ibFrontendView::OnCreateToolbar)
	wxMenu*       m_menuFile = nullptr;           // the window's File, with the files opened last
	wxMenu*       m_menuEdit = nullptr;           // …and its Edit

	std::map<int, ibMenuItem>             m_menuItems;     // the server's commands and schemas, by the wx ids given them
	std::string                           m_menuShape;     // the server's menu as last built, less what is enabled
	std::deque<ibPostedCall>              m_posted;        // the calls posted, not made yet
	std::set<long long>                   m_tabsClosing;   // the tabs a close was posted for and not answered yet
	std::map<long long, ibFormVisualDocument*> m_documents;          // the forms the frame raised (DrawTabs), by their tabs' ids
	std::size_t                           m_calling = 0;             // calls under way — a posted call waits for them
	// The windows up asking, the newest last — each by the id of the question it answers, the one it was asked again
	// with when it answered along the way (Respond). A call standing at one lets a posted call go.
	std::vector<wxString>                 m_asking;
	// The cell a value is being chosen for (ChooseValue) — by the number its choice was asked under, while the window
	// it is in (the question that deep) is up.
	struct ibChoosing {
		class ibControlFrame* owner = nullptr;
		long long             choice = 0;
		std::size_t           depth = 0;
	};
	ibChoosing                            m_choosing;
	long long                             m_choice = 0;              // the last number a choice was asked under
	bool                                  m_drawing = false;         // a tab event the drawing raises is not the person's
	bool                                  m_closingWindow = false;
	bool                                  m_exitGranted = false;     // the server's Exit done: every tab closed, the client goes
	bool                                  m_callUpdateFrameManager = false;
};

#endif
