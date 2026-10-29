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
#include <wx/splash.h>
#include <wx/generic/statusbr.h>   // the bottom bar derives from the GENERIC one — see ibDocBottomStatusBar
#include <wx/xml/xml.h>

#include "frmclient/backend/backend_mainFrame.h"   // the frame an object asks for its inspector's selection
#include "frmclient/docView/docView.h"
#include "protocol/communicator.h"
#include "frmclient/mainFrame/settings/editorsettings.h"
#include "frmclient/mainFrame/settings/fontcolorsettings.h"
#include "frmclient/mainFrame/settings/keybinder.h"

// The main window is created by the exe that has the holder, shown with
// Show() and destroyed by wx — only the lookup remains.
#define mainFrame            		 (ibFrontendMainFrame::GetFrame())

#define wxCREATE_SDI_FRAME 0x16000

class ibOutputWindow;
class ibObjectInspector;
class ibTabDocument;

//pane
#define wxAUI_PANE_BOTTOM   wxT("bottomWindow")
#define wxAUI_PANE_PROPERTY wxT("propertyWindow")

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
	ibProtocol::ibLogin::ibMode GetMode() const { return m_mode; }

	// The font and colours code is shown in — the messages' too (the desktop's window held them for its editors and its
	// output alike) — and how its editors edit.
	ibFontColorSettings GetFontColorSettings() const { return m_fontColorSettings; }
	ibEditorSettings    GetEditorSettings() const { return m_editorSettings; }

	// The window's own menu of that id — File, Edit: a document's undo names its commands in Edit (the desktop's).
	wxMenu* GetDefaultMenu(int idMenu) const;
	// …and its toolbar: the doc manager's commands — a mode's window puts its own after them (the designer's Update
	// database).
	wxAuiToolBar* GetMainFrameToolbar() const { return m_mainFrameToolbar; }

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
	// …and the window brought to the front, once the work under way is done — the desktop's (a stop, a failure sent over).
	void RaiseFrame();

	// A call of the window's, through the communicator: the frame drawn again after it, its messages shown, a
	// question it asks put to the person until the work settles. The answer as it came — a schema's tree, a fetch's
	// rows are read from it. False — refused, and the person told why.
	bool Call(ibProtocol::ibMethod method, const ibProtocolNode& params, ibProtocolNode& answer);
	// …the same, its refusal handed back and said to nobody: the caller says it where it belongs (a settings page, beside
	// the control that fixes it). A session gone closes the client all the same.
	bool Call(ibProtocol::ibMethod method, const ibProtocolNode& params, ibProtocolNode& answer,
		ibProtocol::ibRefusal& refusal, ibString& error);
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
		return wxAuiMDIParentFrame::TryBefore(event) || TryProcessEvent(event);
	}

	// The window's own parts. The base builds what every mode has — the toolbars, the File and Edit menus, the
	// messages pane and the status bar; a mode's window adds its own on top and calls this first.
	virtual void CreateGUI();
	// …the properties pane among them, on the right (the desktop's).
	virtual void CreatePropertyPane();
	// …and the notebook along the bottom, the messages' (the desktop's CreateBottomPane); the designer's adds the
	// debugger's panes beside them.
	virtual void CreateBottomPane();

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

	// THE DOCUMENT OF A TAB THE SERVER RAISED, by the tab's Kind (DrawTabs) — the desktop doc template's CreateDocument for
	// a tab, as it has no template. The base makes what both modes have (a form, the journal) and the runtime's start page;
	// a mode's window makes its own (the designer's tree of an external file) and leaves the rest to the base.
	virtual ibTabDocument* CreateDocument(ibProtocol::ibView::ibKind kind, long long tabId);

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

	class ibFrameManager : public wxAuiManager {
	public:
		ibFrameManager(wxWindow* managedWnd = nullptr,
			unsigned int flags = wxAUI_MGR_DEFAULT) :
			wxAuiManager(managedWnd, flags) {
		}

		void Refresh() { Repaint(); }
	};

	// Create frame manager
	ibFrameManager      m_mgr;
	ibOutputWindow*     m_outputWindow = nullptr;
	ibObjectInspector*  m_objectInspector = nullptr;

	wxMenu*             m_menuConfiguration = nullptr;   // a mode's own (the designer's): after Edit, before the server's
	wxMenu*             m_menuDebug = nullptr;
	wxMenu*             m_menuHelp = nullptr;   // a mode's own Help (the designer's): last on the bar, after the server's
	ibKeyBinder         m_keyBinder;            // the keys of the bar's commands (UpdateHotKeys)
	ibFontColorSettings m_fontColorSettings;    // the person's options (LoadOptions, SaveOptions)
	ibEditorSettings    m_editorSettings;

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
	void OnTabChanged(wxAuiNotebookEvent& event);

	static ibFrontendMainFrame* s_instance;

	std::unique_ptr<ibCommunicator> m_communicator;
	const ibProtocol::ibLogin::ibMode            m_mode;
	wxString                        m_user;       // the login's — the one the login window was told, when refused
	wxString                        m_password;   // until the login — cleared by it
	wxXmlDocument                   m_options;     // the person's options as read (LoadOptions) — their keys put over the
	                                               // defaults each time the menu is built

	wxAuiToolBar* m_mainFrameToolbar = nullptr;   // the window's: the doc manager's commands
	wxAuiToolBar* m_docToolbar = nullptr;         // the active view's own (ibFrontendView::OnCreateToolbar)
	wxMenu*       m_menuFile = nullptr;           // the window's File, with the files opened last
	wxMenu*       m_menuEdit = nullptr;           // …and its Edit

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
// ⭐⭐ THE INK IS OURS WHERE THE PLATFORM LETS IT BE. The generic status bar draws its field text
// with the DC's default foreground and never consults SetForegroundColour, so the colour has to be
// set in DrawFieldText — which exists only where wxStatusBar is the generic one. wx/statusbr.h
// picks that per platform: generic under macOS and GTK, NATIVE under MSW, different again under
// wxUniversal and Qt. So the override is compiled where the hook exists, and where it does not the
// native bar draws its own text in the system colour, which is what a user of that platform
// expects anyway. OES_STATUSBAR_CUSTOM_INK below is that condition, spelled once.
#if !defined(__WXUNIVERSAL__) && !defined(__WXQT__) && !(defined(__WXMSW__) && wxUSE_NATIVE_STATUSBAR)
	#define OES_STATUSBAR_CUSTOM_INK 1
#else
	#define OES_STATUSBAR_CUSTOM_INK 0
#endif

class FRMCLIENT_API ibDocBottomStatusBar : public wxStatusBar {
public:

	// The fields, left to right. Message takes what is left; Assistant is sized to its content.
	enum {
		FieldMessage   = 0,
		FieldAssistant = 1,
		FieldCount
	};

	ibDocBottomStatusBar() : wxStatusBar() {}
	ibDocBottomStatusBar(wxWindow* parent,
		wxWindowID id = wxID_ANY,
		long style = wxSTB_DEFAULT_STYLE,
		const wxString& name = wxStatusBarNameStr);

protected:

#if OES_STATUSBAR_CUSTOM_INK
	// Both halves of the fix: the colour the bar would otherwise ignore, and the lamp that only
	// the assistant field draws — lit while it says something.
	virtual void DrawFieldText(wxDC& dc, const wxRect& rect, int i, int textHeight) override;
#endif
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
