#ifndef _MAINFRAME_DESIGNER_H__
#define _MAINFRAME_DESIGNER_H__

#include "frontend/mainFrame/mainFrame.h"
#include "mainFrame/metaTree/treeConfiguration.h"

#if defined(mainFrame)
#undef mainFrame
#endif

#include "mainFrame/output/outputWindow.h"
#include "mainFrame/local/localWindow.h"
#include "mainFrame/stack/stackWindow.h"
#include "mainFrame/watch/watchWindow.h"

enum {

	wxID_DESIGNER_DEBUG_START = 10000,
	wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING,
	wxID_DESIGNER_DEBUG_START_WEB,
	wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING_WEB,
	wxID_DESIGNER_DEBUG_ATTACH_FOR_DEBUGGING,
	wxID_DESIGNER_DEBUG_EDIT_POINT,
	wxID_DESIGNER_DEBUG_STEP_OVER,
	wxID_DESIGNER_DEBUG_STEP_INTO,
	wxID_DESIGNER_DEBUG_PAUSE,
	wxID_DESIGNER_DEBUG_STOP_DEBUGGING,
	wxID_DESIGNER_DEBUG_STOP_PROGRAM,
	wxID_DESIGNER_DEBUG_NEXT_POINT,
	wxID_DESIGNER_DEBUG_REMOVE_ALL_DEBUGPOINTS,

	wxID_DESIGNER_CONFIGURATION_OPEN_DATABASE,
	wxID_DESIGNER_CONFIGURATION_UPDATE_DATABASE,
	wxID_DESIGNER_CONFIGURATION_ROLLBACK_DATABASE,

	wxID_DESIGNER_CONFIGURATION_LOAD_FROM_FILE,
	wxID_DESIGNER_CONFIGURATION_SAVE_TO_FILE,

	wxID_DESIGNER_DATABASE_LOAD_FROM_FILE,
	wxID_DESIGNER_DATABASE_SAVE_TO_FILE,

	wxID_DESIGNER_DATABASE_CLEAR,

	wxID_DESIGNER_ABOUT,
	wxID_DESIGNER_END
};
// Note: Syntax-helper command ids live in frontend/mainFrame/mainFrame.h
// (wxID_FRONTEND_SYNTAX_HELPER / wxID_FRONTEND_SYNTAX_HELPER_LOOKUP) so
// frontend widgets (e.g. ibCodeEditor's context menu) can post them
// without taking a dependency on the downstream designer header.

//menu  
enum {
	wxID_APPLICATION_DEBUG = wxID_HIGHEST + 1,
	wxID_APPLICATION_SETTING,
	wxID_APPLICATION_USERS,
	wxID_APPLICATION_ACTIVE_USERS,
	wxID_APPLICATION_CONNECTION,
};

#define mainFrame	(ibFrontendDocMDIFrameDesigner::GetFrame())

class ibFrontendDocMDIFrameDesigner : public ibFrontendDocMDIFrame {
public:

	static ibFrontendDocMDIFrameDesigner* GetFrame();

	ibFrontendDocMDIFrameDesigner(const wxString& title = _("Designer"),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize);

	virtual ~ibFrontendDocMDIFrameDesigner();

	void Message(const wxString& strMessage, ibStatusMessage status) { m_outputWindow->SharedOutput(strMessage, status); }
	void ClearMessage() { m_outputWindow->ClearAll(); }

	void BackendError(const wxString& strFileName, const wxString& strDocPath, const long line, const wxString& strErrorMessage) const {
		m_outputWindow->SharedOutput(strErrorMessage, ibStatusMessage::ibStatusMessage_Error, strFileName, strDocPath, line);
	}

	virtual void CreateGUI() override;
	virtual void Modify(bool modify) override;
	virtual bool IsModified() const override;

	ibOutputWindow* GetOutputWindow() const { return m_outputWindow; }
	ibStackWindow* GetStackWindow() const { return m_stackWindow; }
	ibWatchWindow* GetWatchWindow() const { return m_watchWindow; }
	ibLocalWindow* GetLocalWindow() const { return m_localWindow; }

	// Syntax-helper sidebar lifecycle. Pane is lazy-created on first
	// toggle / lookup so the corpus load is amortised away from
	// designer startup.
	void EnsureHelpPane();
	void ToggleHelpPane();
	void OpenHelpForCursor();

	void LoadOptions();
	void SaveOptions();

#pragma region debugger 
	void Debugger_OnSessionStart();
	void Debugger_OnSessionEnd();
	void Debugger_OnEnterLoop();
	void Debugger_OnLeaveLoop();
#pragma endregion 

	virtual bool Show(bool show = true) override;

protected:

	void InitializeDefaultMenu();

	virtual void CreateMetadataPane();
	virtual void CreateBottomPane();
	virtual void CreateWideGui();

	virtual bool AllowRun() const;
	virtual bool AllowClose() const;

	/**
	* Adds the default profile to the hot keys.
	*/
	void SetDefaultHotKeys();

	/**
	* Updates all of the open editors with the current font, color, etc.
	* options.
	*/
	void UpdateEditorOptions();

	//events 
	void OnStartDebug(wxCommandEvent& WXUNUSED(event));
	void OnStartDebugWithoutDebug(wxCommandEvent& WXUNUSED(event));
	void OnStartDebugWeb(wxCommandEvent& WXUNUSED(event));
	void OnStartDebugWithoutDebugWeb(wxCommandEvent& WXUNUSED(event));
	void OnAttachForDebugging(wxCommandEvent& WXUNUSED(event));

	void OnOpenConfiguration(wxCommandEvent& event);
	void OnRollbackConfiguration(wxCommandEvent& event);
	void OnUpdateConfiguration(wxCommandEvent& event);

	void OnLoadDatabase(wxCommandEvent& event);
	void OnSaveDatabase(wxCommandEvent& event);
	void OnClearDatabase(wxCommandEvent& event);

	void OnConfiguration(wxCommandEvent& event);
	void OnRunDebugCommand(wxCommandEvent& event);
	void OnToolsSettings(wxCommandEvent& event);
	void OnUsers(wxCommandEvent& event);
	void OnActiveUsers(wxCommandEvent& event);
	void OnConnection(wxCommandEvent& event);

	void OnAbout(wxCommandEvent& event);

private:

	wxMenu* m_menuFile;
	wxMenu* m_menuEdit;
	wxMenu* m_menuConfiguration;
	wxMenu* m_menuDebug;
	wxMenu* m_menuSetting;
	wxMenu* m_menuAdministration;
	wxMenu* m_menuHelp;

	// Syntax-helper sidebar. Created on first toggle / lookup; owned
	// by the AUI manager once added.
	class ibHelpPaneView* m_helpPane = nullptr;

	ibMetadataTree* m_metaWindow;

	ibOutputWindow* m_outputWindow;
	ibStackWindow* m_stackWindow;
	ibWatchWindow* m_watchWindow;
	ibLocalWindow* m_localWindow;
};
#endif 