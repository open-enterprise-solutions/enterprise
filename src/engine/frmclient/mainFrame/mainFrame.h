#ifndef _FRMCLIENT_MAIN_FRAME_H__
#define _FRMCLIENT_MAIN_FRAME_H__

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <wx/wx.h>
#include "deskgui/univ/aui/aui.h"
#include <wx/splash.h>
#include "deskgui/univ/ctrl/statusBar.h"   // the bottom bar derives from the theme's — see ibDocBottomStatusBar
#include <wx/xml/xml.h>

#include "frmclient/backend/backend_mainFrame.h"   // the frame an object asks for its inspector's selection
#include "frmclient/communicator.h"
#include "frmclient/docView/docView.h"
#include "frmclient/mainFrame/settings/editorsettings.h"
#include "frmclient/mainFrame/settings/fontcolorsettings.h"
#include "frmclient/mainFrame/settings/keybinder.h"

// The main window is created by the exe that has the holder, shown with
// Show() and destroyed by wx — only the lookup remains.
#define mainFrame            		 (ibFrontendMainFrame::GetFrame())

class ibMenu;
class ibOutputWindow;
class ibObjectInspector;
class ibTabDocument;
class ibWelcomeWindow;

//pane
#define ibAUI_PANE_BOTTOM   wxT("bottomWindow")
#define ibAUI_PANE_PROPERTY wxT("propertyWindow")

// THE MAIN WINDOW — the old desktop's ibFrontendMainFrame carried over to the thin client: the same
// AUI frame, the same Luna art, the same doc manager it holds (ibDocParentFrameAnyBase) with the tabs as its documents.
// It holds no session and runs nothing: it holds a communicator and DRAWS the frame the server answered — the title, the
// status, the tabs, the messages, the server's own menus beside the window's File and Edit. A question the server asks
// is asked here and answered back; what the person does goes back as a call, and the answer is drawn again.
//
// ⭐ THE ROUTINE IS THE WINDOW'S, THE DOCUMENTS ARE THE SERVER'S. The doc manager answers the File and Edit menus, the
// toolbars and what may be pressed, as the desktop's did, and asks the server nothing for that; what is done to a
// document is a call (ibTabDocument, frmclient/docView/tabDocument.h).
//
// Each mode's window is a class of its own on top of it — the runtime's (enterprise-thin) and the designer's (designer-thin),
// as on the desktop and as the server's ibClientFrameRuntime / ibClientFrameDesigner.
class FRMCLIENT_API ibFrontendMainFrame : public ibBackendDocFrame, public ibAuiMDIParentFrame, public ibDocParentFrameAnyBase {
public:

	// The process's main window.
	static ibFrontendMainFrame* GetFrame() { return s_instance; }

	// THE PROPERTIES PANE — the desktop's object inspector: what a view selects in it is shown there (a sheet's cells),
	// hidden until it is asked for.
	static ibObjectInspector* GetObjectInspector() {
		if (s_instance != nullptr)
			return s_instance->m_objectInspector;
		return nullptr;
	}

	virtual ~ibFrontendMainFrame();

	// Show does the whole opening: build the GUI, log in — the start runs there, and may say something or ask — draw
	// the frame, put the window up. False when the login or the start was refused; the person is told why.
	virtual bool Show(bool show = true) override;

#if wxUSE_MENUS
	virtual void SetMenuBar(wxMenuBar* pMenuBar) override;
#endif // wxUSE_MENUS

	// bring window to front
	virtual void Raise() override;

	// The client goes with the window: logged out, and its session torn down by the server.
	virtual bool Destroy() override;

	// THE TAB A VIEW SHOWS IN — the desktop's factory: a tab of the process's main window, the view's document's title
	// and icon on it.
	virtual wxWindow* CreateChildFrame(ibFrontendView* view, const wxPoint& pos, const wxSize& size, long style) override;

	// A view activated — its own menu merged into the window's and its toolbar shown; deactivated, both disabled (the
	// desktop's ActivateView).
	virtual void ActivateView(ibFrontendView* view, bool activate) override;

	// A METAOBJECT OPENS ON THE SERVER — the desktop's door (ibDocManager::OpenForm), by what the client holds of the
	// object: its id and the configuration it is in (the tab); the server opens it as the tree's Edit does (schema
	// MetaData, Open) and its document comes with the frame. False: not sent. Its message is the designer's protocol:
	// the designer's window sends it (ibFrontendMainFrameDesigner), the base sends nothing.
	virtual bool OpenForm(ibMetaID WXUNUSED(id), const class ibMetaData& WXUNUSED(metaData)) { return false; }

	// …and what it shows: the object selected in it (the desktop's property slot).
	virtual ibPropertyObject* GetProperty() const override;
	virtual bool SetProperty(ibPropertyObject* prop) override;

	
	
	bool IsShownInspector();
	void ShowInspector();

	// The panes laid out again, once the work under way is done — the window frozen until then, so what the work
	// changes is shown once (UpdateFrameManager thaws it).
	void UpdateManager() {
		if (!m_callUpdateFrameManager) {
			m_callUpdateFrameManager = true;
			Freeze();
			CallAfter(&ibFrontendMainFrame::UpdateFrameManager);
		}
	}
	// …and the window brought to the front, once the work under way is done — the desktop's (a stop, a failure sent over).
	void RaiseFrame();

	// What it logged in to — the thin client's process is one mode, the window's.
	ibProtocol::ibLogin::ibMode GetMode() const { return m_mode; }

	// The font and colours code is shown in — the messages' too (the desktop's window held them for its editors and its
	// output alike) — and how its editors edit.
	ibFontColorSettings GetFontColorSettings() const { return m_fontColorSettings; }
	ibEditorSettings    GetEditorSettings() const { return m_editorSettings; }

	// The window's own menu of that id — File, Edit: a document's undo names its commands in Edit (the desktop's).
	wxMenu* GetDefaultMenu(int idMenu) const;
	// …and its toolbar: the doc manager's commands — a mode's window puts its own after them (the designer's Update
	// database).
	ibAuiToolBar* GetMainFrameToolbar() const { return m_mainFrameToolbar; }


	// A call of the window's, through the communicator: the frame drawn again after it, its messages shown, a
	// question it asks put to the person until the work settles. The answer as it came — a schema's tree, a fetch's
	// rows are read from it. False — refused, and the person told why.
	bool Call(ibProtocol::ibMethod method, const ibProtocolNode& params, ibProtocolNode& answer);
	// …the same, its refusal handed back and said to nobody: the caller says it where it belongs (a settings page, beside
	// the control that fixes it). A session gone closes the client all the same.
	bool Call(ibProtocol::ibMethod method, const ibProtocolNode& params, ibProtocolNode& answer,
		ibProtocol::ibRefusal& refusal, ibString& error);
	// …and the assistant's own connection, the window's doors to it (ibCommunicator::ListenMcp, Mcp): called on the
	// transport's threads too, and never draws the frame.
	bool ListenMcp(std::function<void(const ibProtocolNode& news)> told, ibProtocol::ibRefusal& refusal, ibString& error);
	bool Mcp(const ibProtocolNode& params, ibProtocolNode& result, ibProtocol::ibRefusal& refusal, ibString& error);
	// A call nobody waits on — what the person did in a view, a tab chosen or closed: made after the event that
	// raised it, in the order posted, and never inside another call — but inside a question a call is asking, as the
	// desktop's modal loop runs the events of the windows over it (a choice form a window opened, CreateChildFrame):
	// what the person can reach while a window asks is the modal's to say.
	void Post(ibProtocol::ibMethod method, const ibProtocolNode& params);
	// …or a state event left for the next call to carry (ibCommunicator::Hold) — after the calls posted before it, which
	// would carry it ahead of what was done first. False — the server takes none held: it is posted as its own call.
	bool Hold(const ibProtocolNode& event);
	// THE QUESTION THE SERVER'S CODE WAITS ON — none (not IsNode): it waits on nothing.
	ibProtocolNode GetRequest() const;
	// AN ANSWER THAT LEAVES ITS WINDOW OPEN — a window that acts more than once before it closes (the saved settings'
	// shelf) answers the question pending with each act: the server does it and asks again, and that question is the
	// one pending then — for the window still open, never put to the person anew. False — refused, and the person told.
	bool Respond(const ibProtocolNode& response);
	// A VALUE OF THE SERVER'S TYPE CHOSEN FOR A CELL OF THE WINDOW ASKING — the window answers its question with the act
	// (a list's settings: Act Choose), the server opens the value's quick choice or its choice form (a dialog over this
	// modal: CreateChildFrame), and the value chosen comes back with the window's question asked again (Chosen), to
	// the cell's ChoiceProcessing. No window asks — the cell is on the active tab (a designer's composer panel): the same
	// act goes to the tab (event Select), whose view chooses through its configuration and says the value with the view
	// (State Chosen). False — neither is there.
	bool ChooseValue(class ibControlFrame* owner, const class ibValue& current);
	// …and the value chosen, when the window's question or the tab's view state says one was (Chosen) — handed to the
	// cell once.
	void TakeChosen(const ibProtocolNode& asked);
	// A tab closed — by the server, which asks about what is unsaved; the tab goes when the frame says so. Whatever
	// closes it comes here: its button, its window closed (Escape, the application's), the window menu, File → Close.
	void CloseTab(long long tabId);
	// …and true while the WINDOW takes its tabs down (AllowClose, Destroy), false when a person closes one (the desktop's).
	bool IsClosingWindow() const { return m_closingWindow; }
	// A fetch — a control's (Fetch: a table's rows, a spreadsheet's cells; `params` — the control, its request, the view
	// it is of) or the pictures of their ids (Picture): the answer is no frame, nothing is drawn or asked, and a refusal is
	// only false — but the engine's exception (Exception), thrown here in its words: the caller says it where it belongs
	// (a table's page, by its data view), else the application's loop journals it. It speaks to the communicator alone,
	// so a control's own thread may make it.
	bool Fetch(ibProtocol::ibMethod method, const ibProtocolNode& params, ibProtocolNode& answer);
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
	// …and the frame's number (ibCommunicator::GetFrameNumber): moved by every call the server answers with a frame —
	// whatever may change something goes that way. What was read at this number is still true.
	long long GetFrameNumber() const;
	// The document of that tab — none: no such tab drawn.
	ibTabDocument* FindDocument(long long tabId) const;

protected:

	// hook the document manager into event handling chain here
	virtual bool TryBefore(wxEvent& event) override {
		// It is important to send the event to the base class first as wxMDIParentFrame overrides its TryBefore() to
		// send the menu events to the currently active child and the child must get them before our own
		// TryProcessEvent() is executed, not afterwards.
		return ibAuiMDIParentFrame::TryBefore(event) || TryProcessEvent(event);
	}

	// The window's own parts. The base builds what every mode has — the toolbars, the File and Edit menus, the
	// messages pane and the status bar; a mode's window adds its own on top and calls this first.
	virtual void CreateGUI();
	// …the properties pane among them, on the right (the desktop's).
	virtual void CreatePropertyPane();
	// …and the notebook along the bottom, the messages' (the desktop's CreateBottomPane); the designer's adds the
	// debugger's panes beside them.
	virtual void CreateBottomPane();
	// …and the workspace while no tab is open, in the client window `parent` (wx's OnCreateClient for the client): made
	// once the login has said who the person is (m_fullName). Default: the greeting alone; the designer's adds its keys.
	virtual ibWelcomeWindow* OnCreateWelcome(wxWindow* parent);

	// WHAT A MODE'S FRAME CARRIES BESIDE THE BASE'S — drawn by what the patch changed in it (not IsNode: the frame sent
	// whole). Default: nothing; the designer draws its configuration's tree (MetaData).
	virtual void DrawFrame(const ibProtocolNode& WXUNUSED(frame), const ibProtocolNode& WXUNUSED(patch)) {}

	// What the window builds BY ITSELF once the start has run — the desktop's slot of the same name (its home page;
	// the server opens that one now). Called from Show after the login has settled: what is built here may ask the
	// server, which answers only once no question of the start is pending. Default: nothing; the runtime builds its
	// section panel.
	virtual void CreateStartupPage() {}

	// May the window go down? The server's Exit closes each tab but the start page, asking about what is unsaved; a tab
	// the person kept keeps the window.
	virtual bool AllowClose();

	// The schemas the window shows at its start — read in the login (its Read), each answered beside the frame and shown
	// (ShowSchema) once the window is up, as a call of its own would have been. Default: none.
	virtual std::vector<ibProtocol::ibSchema::ibKind> GetStartSchemas() const { return {}; }
	// A schema of the menu, asked for and answered, shown — or one of the start's (GetStartSchemas). The base shows what
	// both modes have (Active users, About); a mode's window shows its own (the runtime's All functions, the designer's
	// debugger store) and leaves the rest to the base.
	virtual void ShowSchema(ibProtocol::ibSchema::ibKind schema, const ibProtocolNode& shown);
	// A question, put to the person; what they answered in `response` (nothing — cancelled). The base asks what both
	// modes are asked; a mode's window asks its own (the designer's restructure ledger) and leaves the rest to the base.
	virtual void ShowRequest(const ibProtocolNode& request, ibProtocolNode& response);

	// The File and Edit menus — the desktop's, the doc manager's commands — and the bar they start it. A mode's window
	// makes its own menu (the designer's Help) and calls this.
	virtual void InitializeDefaultMenu();
	// The keys of the window's own commands, File's and Edit's — the desktop's defaults; a mode's window adds its own
	// commands' and calls this.
	virtual void SetDefaultHotKeys();

	// The communicator comes in with the window and the window holds it — and with it the client — from then on. The
	// login is the window's (Show), so the start's messages and questions are drawn into it.
	ibFrontendMainFrame(std::unique_ptr<ibCommunicator> communicator, ibProtocol::ibLogin::ibMode mode,
		const wxString& user, const wxString& password, const wxString& title,
		const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_FRAME_STYLE, const wxString& name = wxASCII_STR(wxFrameNameStr));

	ibCommunicator& GetCommunicator() const { return *m_communicator; }

	// A schema of the menu opened — asked for and shown (ShowSchema); the journal's tab opened by its command. What an
	// item of the server's menu does, and of the window's own (the designer's About).
	void OpenSchema(ibProtocol::ibSchema::ibKind schema);

	// THE PERSON'S OPTIONS WRITTEN BACK — the designer's Options: the font and colours, how editors edit, the keys as
	// they stand, into the file LoadOptions reads; and kept as read, so a menu built again keeps the keys.
	void SaveOptions();
	// Tools → Options, the one dialog of both modes: the schema its item asked (the designer's McpServer, the
	// application's Debugging) says which pages it holds besides the common ones.
	void ShowOptions(ibProtocol::ibSchema::ibKind schema, const ibProtocolNode& shown);

	class ibFrameManager : public ibAuiManager {
	public:
		ibFrameManager(wxWindow* managedWnd = nullptr,
			unsigned int flags = ibAUI_MGR_DEFAULT) :
			ibAuiManager(managedWnd, flags) {
		}

		void Refresh() { Repaint(); }
	};

	// Create frame manager
	ibFrameManager      m_mgr;
	ibOutputWindow*     m_outputWindow = nullptr;
	ibObjectInspector*  m_objectInspector = nullptr;

	ibMenu*             m_menuConfiguration = nullptr;   // a mode's own (the designer's): after Edit, before the server's
	ibMenu*             m_menuDebug = nullptr;
	ibMenu*             m_menuHelp = nullptr;   // a mode's own Help (the designer's): last on the bar, after the server's
	ibKeyBinder         m_keyBinder;            // the keys of the bar's commands (UpdateHotKeys)
	ibFontColorSettings m_fontColorSettings;    // the person's options (LoadOptions, SaveOptions)
	ibEditorSettings    m_editorSettings;
	wxString            m_fullName;             // the person the session is — the login's answer (FullName)

private:

	// What a menu item of the server's does: a command, or a schema — shown, or its command done — and the key the server
	// gives it by default.
	struct ibMenuItem {
		bool     schema = false;
		int      number = 0;
		int      command = 0;   // a schema's: the command it does (0: the schema shown)
		wxString shortcut;
	};

	struct ibPostedCall {
		ibProtocol::ibMethod            method;
		ibProtocolNode              params;
		std::vector<ibProtocolNode> held;   // the state events held while it waited — held once it is made (Hold)
	};

	// What the item node of the frame's Menu does.
	static ibMenuItem MenuItemOf(const ibProtocolNode& item);

	// THE PERSON'S OPTIONS — the font and colours code is shown in, how its editors edit, the keys they chose: read once,
	// from their own directory (the desktop designer's LoadOptions). A server keeps none of them.
	void LoadOptions();
	// THE KEYS OF THE MENU AS IT NOW STANDS: each command's default — the window's own, the one the server gives its
	// own — and the person's over them (LoadOptions), onto the window and the menu's labels.
	void UpdateHotKeys();

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
	// A call refused, said to the person — and a client whose session is gone, closed.
	void Refused(ibProtocol::ibRefusal refusal, const wxString& error);
	// …and once a call has settled: the client gone when the server's Exit was done, the calls posted made.
	void CallSettled();
	// The first call posted, made — unless a call is under way: that one makes it when it has settled.
	void CallPosted();

	// The document whose tab is that page of the notebook.
	ibTabDocument* DocumentAt(int page) const;
	// A tab's document gone — out of the frame's (~ibTabDocument).
	void RemoveDocument(const ibTabDocument* document);
	friend class ibTabDocument;

	void UpdateFrameManager();

	void OnCloseWindow(wxCloseEvent& event);
	void OnExit(wxCommandEvent& event);
	// Window → Close All — every tab, the server's CloseAll.
	void OnCloseAll(wxCommandEvent& event);
	void OnMenu(wxCommandEvent& event);
	// Cut, Copy, Paste, Delete, Select all — the field under the focus does them.
	void OnEditCommand(wxCommandEvent& event);
	void OnTabChanged(ibAuiNotebookEvent& event);

	static ibFrontendMainFrame* s_instance;

	std::unique_ptr<ibCommunicator> m_communicator;
	const ibProtocol::ibLogin::ibMode            m_mode;
	wxString                        m_user;       // the login's — the one the login window was told, when refused
	wxString                        m_password;   // until the login — cleared by it
	wxXmlDocument                   m_options;     // the person's options as read (LoadOptions) — their keys put over the
	                                               // defaults each time the menu is built

	ibAuiToolBar* m_mainFrameToolbar = nullptr;   // the window's: the doc manager's commands
	ibAuiToolBar* m_docToolbar = nullptr;         // the active view's own (ibFrontendView::OnCreateToolbar)
	ibWelcomeWindow* m_welcome = nullptr;         // in the client window, shown while it holds no tab (DrawTabs)
	// the theme's menus, held as such: an item appended by its stock id alone goes ibMenu::Append and takes the
	// dictionary's words - held as wxMenu* it went wx's own factory and kept wx's English (2026-11-13)
	ibMenu*       m_menuFile = nullptr;           // the window's File, with the files opened last
	ibMenu*       m_menuEdit = nullptr;           // …and its Edit
	std::vector<wxMenu*> m_viewMenus;             // the active view's own, in the window's bar after Edit (ActivateView)

	std::map<int, ibMenuItem>             m_menuItems;     // the server's commands and schemas, by their wx ids (MenuIdOf)
	ibString                              m_menuShape;     // the server's menu as last built, less what is enabled
	std::deque<ibPostedCall>              m_posted;        // the calls posted, not made yet
	std::set<long long>                   m_tabsClosing;   // the tabs a close was posted for and not answered yet
	std::map<long long, ibTabDocument*>   m_documents;   // the tabs' documents the frame raised (DrawTabs), by their ids
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
	bool                                  m_closingWindow = false;   // see IsClosingWindow()
	bool                                  m_exitGranted = false;     // the server's Exit done: every tab closed, the client goes
	bool                                  m_callUpdateFrameManager = false;
	bool                                  m_callRaiseFrame = false;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////

// The bar along the bottom — what the application is doing on the left,
// whether an assistant can reach it on the right. Both fields are the frame's, drawn with its answer (DrawFrame): the
// server's Status, and the base's assistant access by where it listens (Endpoint). The desktop's bar asked the server
// once a second; here that is a call a second, and the frame's patch already says when it changed.
//
// The bar is the theme's (ibStatusBar), the same on every port, its colours and its text the theme's: the port's own
// was the generic bar under macOS and GTK, the native one under MSW - the lamp drawn on two of the three, the native
// bar painted apart from the frame (2026-11-11).
class FRMCLIENT_API ibDocBottomStatusBar : public ibStatusBar {
public:

	// The fields, left to right. Message takes what is left; Assistant is sized to its content.
	enum {
		FieldMessage   = 0,
		FieldAssistant = 1,
		FieldCount
	};

	ibDocBottomStatusBar() = default;
	ibDocBottomStatusBar(wxWindow* parent,
		wxWindowID id = wxID_ANY,
		long style = wxSTB_DEFAULT_STYLE,
		const wxString& name = wxStatusBarNameStr);

protected:

	// The lamp that only the assistant field draws — lit while it says something.
	virtual void DrawStatusField(wxDC& dc, const wxRect& rect, int n, int flags) override;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////

// The picture shown while the base opens and the client logs in.
class ibProcessSplashScreen : public wxSplashScreen {
public:
	ibProcessSplashScreen(const wxBitmap& bitmap, long splashStyle = wxSPLASH_CENTRE_ON_SCREEN, int milliseconds = -1,
		wxWindow* parent = nullptr, wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxSIMPLE_BORDER | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP) :
		wxSplashScreen(bitmap, splashStyle, milliseconds,
			parent, id,
			pos, size, style
		)
	{
		wxTheApp->SetTopWindow(this);

		//Needed to get the splashscreen to paint
		wxSplashScreen::Update();
	}

	virtual int FilterEvent(wxEvent& event) wxOVERRIDE { return Event_Skip; }
};

// The host frame's command ids the editor posts upward — the syntax helper, and the word under the caret looked up in
// it (the code editor's context menu). The values are the old desktop's.
enum {
	wxID_FRONTEND_SYNTAX_HELPER        = wxID_HIGHEST + 4500,
	wxID_FRONTEND_SYNTAX_HELPER_LOOKUP = wxID_HIGHEST + 4501,
};

#endif
