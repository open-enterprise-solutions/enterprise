#ifndef _MAIN_FRAME_H__
#define _MAIN_FRAME_H__

#include <wx/wx.h>
#include <wx/aui/aui.h>
#include <wx/splash.h>
#include <wx/stc/stc.h>

#include "frontend/docView/docView.h"   // forked ib* doc/view (replaces wx/docview.h)

#include "backend/backend_mainFrame.h"
#include "frontend/frontend.h"
#include "frontend/frontendTypes.h"   // ibFrontendWindow typedef

class ibMetaView;

// The main window is created by the exe that has the holder, shown with
// Show() and destroyed by wx — so the old create / show / destroy
// macros have no callers left and are gone. Only the lookup remains.
#define mainFrame            		 (ibFrontendMainFrame::GetFrame())

#include "objinspect/objinspect.h"

#include "settings/keybinder.h"
#include "settings/fontcolorsettings.h"
#include "settings/editorsettings.h"

//********************************************************************************
//*                                 ID's                                         *
//********************************************************************************

#define wxCREATE_SDI_FRAME 0x16000

#define wxAUI_DEFAULT_COLOUR wxColour(41, 57, 85) 
#define wxAUI_WHITE_COLOUR wxColour(255, 255, 255) 

class FRONTEND_API ibFrontendMainFrame :
	public ibBackendDocFrame, public wxAuiMDIParentFrame,
	public ibDocParentFrameAnyBase {
public:

	virtual wxMenu* GetDefaultMenu(int idMenu) const { return nullptr; }

	virtual void CreateGUI() = 0;

	virtual void Modify(bool modify) {}
	virtual bool IsModified() const { return false; }

	// GetSession() comes from ibBackendDocFrame — it answers out of the
	// holder this frame was built with. The old m_session mirror and the
	// Initialize(session) call that filled it are gone: one owner, one
	// source of truth.


	virtual ibMetaData* FindMetadataByPath(const wxString& strFileName) const;

#pragma region _frontend_call_h__

	// Form support
	virtual ibBackendValueForm* ActiveWindow() const override;
	virtual ibBackendValueForm* CreateNewForm(const ibValueMetaObjectFormBase* creator, class ibBackendControlFrame* ownerControl = nullptr,
		class ibSourceDataObject* srcObject = nullptr, const ibUniqueKey& formGuid = wxNullUniqueKey) override;

	virtual ibUniqueKey CreateFormUniqueKey(const ibBackendControlFrame* ownerControl,
		const ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid);

	virtual class ibBackendValueForm* FindFormByUniqueKey(const ibBackendControlFrame* ownerControl,
		const ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid);

	virtual class ibBackendValueForm* FindFormByUniqueKey(const ibUniqueKey& guid) override;
	virtual class ibBackendValueForm* FindFormByControlUniqueKey(const ibUniqueKey& guid) override;
	virtual class ibBackendValueForm* FindFormBySourceUniqueKey(const ibUniqueKey& guid) override;

	virtual bool UpdateFormUniqueKey(const ibUniqueKeyPair& guid) override;

	// Grid support
	virtual bool ShowSpreadsheetDocument(const wxString& strTitle, wxObjectDataPtr<ibBackendSpreadsheetObject>& spreadSheetDocument) override;
	virtual bool PrintSpreadsheetDocument(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc, bool showPrintDlg = true) override;

#pragma endregion 

	virtual void RefreshFrame() override;
	virtual void RaiseFrame() override;

	// Desktop modal-message primitive — wxMessageBox with this frame
	// as the parent so the dialog stays attached. Backend callers use
	// session->GetFrame()->ShowModalMessage(...) instead of raw
	// wxMessageBox so the backend stays wx-ignorant.
	int ShowModalMessage(const wxString& message, const wxString& caption, int style) override {
		return wxMessageBox(message, caption, style, this);
	}

	virtual wxAuiToolBar* GetMainFrameToolbar() const { return m_mainFrameToolbar; }
	virtual wxAuiToolBar* GetDocToolbar() const { return m_docToolbar; }

	virtual ibPropertyObject* GetProperty() const;
	virtual bool SetProperty(ibPropertyObject* prop);

	virtual void SetTitle(const wxString& title) override { wxAuiMDIParentFrame::SetTitle(title); }
	virtual void SetStatusText(const wxString& text, int number = 0) override { wxAuiMDIParentFrame::SetStatusText(text, number); }
	// Show does the whole opening: build the GUI, start the runtime, ask
	// AllowRun, put the window up. Callers just say Show().
	//
	// Hiding is not closing — the old `!show && AllowClose()` arm meant
	// minimising or hiding the window asked "may I close?" and could run
	// the BeforeExit script. The close question lives where closing
	// actually happens: the wxEVT_CLOSE_WINDOW handler.
	virtual bool Show(bool show = true) override;

#if wxUSE_MENUS
	virtual void SetMenuBar(wxMenuBar* pMenuBar) override;
#endif // wxUSE_MENUS

	virtual wxAuiMDIClientWindow* OnCreateClient() override;

	// bring window to front
	virtual void Raise() override;

	//destroy window
	virtual bool Destroy() override;


	// update frame manager 
	void UpdateManager() {
		if (!m_callUpdateFrameManager) {
			m_callUpdateFrameManager = true;
			CallAfter(&ibFrontendMainFrame::UpdateFrameManager);
		}
	}

protected:

	// hook the document manager into event handling chain here
	virtual bool TryBefore(wxEvent& event) override {
		// It is important to send the event to the base class first as
		// wxMDIParentFrame overrides its TryBefore() to send the menu events
		// to the currently active child valueForm and the child must get them
		// before our own TryProcessEvent() is executed, not afterwards.
		return wxAuiMDIParentFrame::TryBefore(event) || TryProcessEvent(event);
	}

	// May the window come up? Desktop default: yes. Enterprise overrides
	// to fire BeforeStart on the session's runtime.
	virtual bool AllowRun() { return true; }

public:
	// May the window go down? Asked ONLY when the answer can be honoured —
	// a forced close does not call this at all, which is why there is no
	// force parameter: "don't ask" is expressed by not asking.
	//
	// Public because the GUI session's closing sequence asks it: the
	// question belongs to the window, the sequence belongs to the session.
	//
	// Desktop default: poll the open documents, each of which may refuse.
	// Enterprise chains BeforeExit after this; the Designer chains its
	// unsaved-configuration prompt.
	virtual bool AllowClose();

protected:


	// Lazy runtime start — first Show() after LoadMetadata creates the
	// root module manager and wires per-session ProcUnits onto the
	// metadata descriptors. No-op on later shows (re-enter guarded by
	// session->GetManagerModule()) and on kinds that don't run scripts
	// (Designer / Launcher / WebServer). Called from Show().
	bool EnsureRuntime();

	// The session comes in with the window. Everything the frame needs to
	// do with it — take ownership, wire the back-link, register itself as
	// the process's main window — happens inside, so callers just build
	// the frame and hand over the holder they were given.
	ibFrontendMainFrame(ibSessionHolder&& holder,
		const wxString& title,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_FRAME_STYLE,
		const wxString& name = wxASCII_STR(wxFrameNameStr));

	bool Create(const wxString& title,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_FRAME_STYLE,
		const wxString& name = wxASCII_STR(wxFrameNameStr));

public:

	virtual ~ibFrontendMainFrame();

	// Returns ibFrontendWindow* (typedef → wxWindow on desktop,
	// ibWebWindow on web) so the signature reads the same across
	// builds even though this static is desktop-only today. Keeps the
	// door open for a shared signature if the web frame ever adopts
	// the same factory entry point.
	static ibFrontendWindow* CreateChildFrame(ibView* view,
		const wxPoint& pos, const wxSize& size, long style = wxDEFAULT_FRAME_STYLE);

	static ibObjectInspector* GetObjectInspector() {
		if (s_instance != nullptr)
			return s_instance->m_objectInspector;
		return nullptr;
	}

	// The process's main window. One per desktop process, which is why
	// the desktop session can answer GetFrame() from here instead of
	// storing a pointer of its own.
	static ibFrontendMainFrame* GetFrame() { return s_instance; }

	// Claim the singleton slot. Called from the ctor — the window that
	// was built around the holder is the process's main window.
	static void InitFrame(ibFrontendMainFrame* mf);

	ibKeyBinder             GetKeyBinder() const { return m_keyBinder; }
	ibFontColorSettings     GetFontColorSettings() const { return m_fontColorSettings; }
	ibEditorSettings        GetEditorSettings() const { return m_editorSettings; }

	/**
	* Show property in mainFrame
	*/
	bool IsShownInspector();
	void ShowInspector();

	// Activate view
	void ActivateView(ibView* view, bool activate = true);

protected:

	void UpdateFrameManager();

	// Events 
	void OnCloseWindow(wxCloseEvent& event);
	void OnExit(wxCommandEvent& WXUNUSED(event));

	virtual void CreatePropertyPane();

	class ibFrameManager : public wxAuiManager {
	public:
		ibFrameManager(wxWindow* managedWnd = nullptr,
			unsigned int flags = wxAUI_MGR_DEFAULT) :
			wxAuiManager(managedWnd, flags) {
		}

		void Refresh() { Repaint(); }
	};

	static ibFrontendMainFrame* s_instance;

	ibObjectInspector* m_objectInspector;

	ibKeyBinder             m_keyBinder;
	ibFontColorSettings     m_fontColorSettings;
	ibEditorSettings        m_editorSettings;

	bool m_callRaiseFrame, m_callUpdateFrameManager;

	wxAuiToolBar* m_mainFrameToolbar;
	wxAuiToolBar* m_docToolbar;

	// Create frame manager 
	ibFrameManager m_mgr;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////

class ibDocBottomStatusBar : public wxStatusBar {
public:

	ibDocBottomStatusBar() : wxStatusBar() {};
	ibDocBottomStatusBar(wxWindow* parent,
		wxWindowID id = wxID_ANY,
		long style = wxSTB_DEFAULT_STYLE,
		const wxString& name = wxStatusBarNameStr)
		: wxStatusBar(parent, id, style, name)
	{
		// Light dusty status bar — sits between the powder-blue
		// workspace and the cream content panes; deep-blue text reads
		// cleanly. Matches the interior-design palette (see
		// luna_dockart.cpp).
		wxStatusBar::SetBackgroundColour(wxColour(0xC8, 0xD6, 0xDF));   // #C8D6DF light dusty
		wxStatusBar::SetForegroundColour(wxColour(0x3F, 0x5C, 0x77));   // #3F5C77 deep dusty blue

		m_statusBarText = new wxStaticText(this, wxID_ANY, wxEmptyString, wxPoint(5, 5), wxDefaultSize, 0);
		m_statusBarText->Show();
	}

	virtual void DoUpdateStatusText(int field) override {
		m_statusBarText->SetLabelText(
			GetStatusText(field)
		);
	}

private:
	wxStaticText* m_statusBarText;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////

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

//pane
#define wxAUI_PANE_METADATA wxT("metadataWindow")
#define wxAUI_PANE_PROPERTY wxT("propertyWindow")
#define wxAUI_PANE_BOTTOM   wxT("bottomWindow")
#define wxAUI_PANE_HELP     wxT("syntaxHelperWindow")

// Host-frame command ids that the editor and other frontend widgets
// may post upward. Defined in a frontend header (not the designer
// header) so frontend.dll → designer.exe stays a one-way link — the
// editor's context-menu code (subphase 1.3) can reference these
// without pulling in the downstream designer module. The numeric
// values must not collide with wxStandardID or with the designer-
// private id block in designer/mainFrame/mainFrameDesigner.h.
enum {
    wxID_FRONTEND_SYNTAX_HELPER        = wxID_HIGHEST + 4500,
    wxID_FRONTEND_SYNTAX_HELPER_LOOKUP = wxID_HIGHEST + 4501,
    // Values 4502+ reserved for unrelated frontend features
    // (debug step shortcuts / plugin manager / plugin web pane) —
    // landed in separate PRs.
};

#endif 