#ifndef _MAINFRAME_DESIGNER_H__
#define _MAINFRAME_DESIGNER_H__

#include "frontend/mainFrame/mainFrame.h"
#include "mainFrame/metatree/metatreeWnd.h"

#if defined(mainFrame)
#undef mainFrame
#endif

#include "mainFrame/output/outputWindow.h"
#include "mainFrame/local/localWindow.h"
#include "mainFrame/stack/stackWindow.h"
#include "mainFrame/watch/watchWindow.h"

enum {
	wxID_DESIGNER_UPDATE_METADATA = 10000,

	wxID_DESIGNER_DEBUG_START,
	wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING,
	wxID_DESIGNER_DEBUG_ATTACH_FOR_DEBUGGING,
	wxID_DESIGNER_DEBUG_EDIT_POINT,
	wxID_DESIGNER_DEBUG_STEP_OVER,
	wxID_DESIGNER_DEBUG_STEP_INTO,
	wxID_DESIGNER_DEBUG_PAUSE,
	wxID_DESIGNER_DEBUG_STOP_DEBUGGING,
	wxID_DESIGNER_DEBUG_STOP_PROGRAM,
	wxID_DESIGNER_DEBUG_NEXT_POINT,
	wxID_DESIGNER_DEBUG_REMOVE_ALL_DEBUGPOINTS,

	wxID_DESIGNER_CONFIGURATION_RETURN_DATABASE,

	wxID_DESIGNER_CONFIGURATION_LOAD,
	wxID_DESIGNER_CONFIGURATION_SAVE,

	wxID_DESIGNER_ABOUT,
	wxID_DESIGNER_END
};

//menu  
enum {
	wxID_APPLICATION_DEBUG = wxID_HIGHEST + 1,
	wxID_APPLICATION_SETTING,
	wxID_APPLICATION_USERS,
	wxID_APPLICATION_ACTIVE_USERS,
	wxID_APPLICATION_CONNECTION,
};

#define mainFrame	(CDocDesignerMDIFrame::GetFrame())

class CDocDesignerMDIFrame : public CDocMDIFrame
{
	wxMenuBar* m_menuBar;

	wxMenu* m_menuFile;
	wxMenu* m_menuEdit;
	wxMenu* m_menuConfiguration;
	wxMenu* m_menuDebug;
	wxMenu* m_menuSetting;
	wxMenu* m_menuAdministration;
	wxMenu* m_menuHelp;

protected:

	CMetadataTree* m_metadataTree;

	COutputWindow* m_outputWindow;
	CStackWindow* m_stackWindow;
	CWatchWindow* m_watchWindow;
	CLocalWindow* m_localWindow;

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

public:

	static CDocDesignerMDIFrame* GetFrame();

	CDocDesignerMDIFrame(const wxString& title = _("Designer"),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize);

	virtual ~CDocDesignerMDIFrame();

	void Message(const wxString& strMessage, eStatusMessage status) { m_outputWindow->SharedOutput(strMessage, status); }
	void ClearMessage() { m_outputWindow->ClearAll(); }

	void BackendError(const wxString& strFileName, const wxString& strDocPath, const long line, const wxString& strErrorMessage) const { m_outputWindow->SharedOutput(strErrorMessage, eStatusMessage::eStatusMessage_Error, strFileName, strDocPath, line); }

	virtual void CreateGUI() override;
	virtual void Modify(bool modify) override;
	virtual bool IsModified() const override;

	COutputWindow* GetOutputWindow() const { return m_outputWindow; }
	CStackWindow* GetStackWindow() const { return m_stackWindow; }
	CWatchWindow* GetWatchWindow() const { return m_watchWindow; }
	CLocalWindow* GetLocalWindow() const { return m_localWindow; }

	void LoadOptions();
	void SaveOptions();

#pragma region debugger 
	void Debugger_OnSessionStart();
	void Debugger_OnSessionEnd();
	void Debugger_OnEnterLoop();
	void Debugger_OnLeaveLoop();
#pragma endregion 

	virtual bool Show(bool show = true) override;

public:
	virtual void OnInitializeConfiguration(enum eConfigType cfg);
	virtual void OnDestroyConfiguration(enum eConfigType cfg);
protected:

	//events 
	void OnToolbarClicked(wxEvent& event);

	void OnStartDebug(wxCommandEvent& WXUNUSED(event));
	void OnStartDebugWithoutDebug(wxCommandEvent& WXUNUSED(event));
	void OnAttachForDebugging(wxCommandEvent& WXUNUSED(event));

	void OnRollbackConfiguration(wxCommandEvent& event);
	void OnConfiguration(wxCommandEvent& event);
	void OnRunDebugCommand(wxCommandEvent& event);
	void OnToolsSettings(wxCommandEvent& event);
	void OnUsers(wxCommandEvent& event);
	void OnActiveUsers(wxCommandEvent& event);
	void OnConnection(wxCommandEvent& event);

	void OnAbout(wxCommandEvent& event);
};
#endif 