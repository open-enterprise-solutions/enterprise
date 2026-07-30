#ifndef _MAINFRAME_ENTERPRISE_H__
#define _MAINFRAME_ENTERPRISE_H__

#include "frontend/mainFrame/mainFrame.h"

#if defined(mainFrame)
#undef mainFrame
#endif

#include "mainFrame/output/outputWindow.h"

enum {
	wxID_ENTERPRISE_ALL_OPERATIONS = wxID_HIGHEST + 1,

	wxID_ENTERPRISE_SETTING,
	wxID_ENTERPRISE_USERS,
	wxID_ENTERPRISE_ACTIVE_USERS,
	wxID_ENTERPRISE_AUDIT_LOG,

	wxID_ENTERPRISE_ABOUT,
	wxID_ENTERPRISE_END
};

#define mainFrame	(ibFrontendMainFrameEnterprise::GetFrame())

class ibFrontendMainFrameEnterprise : public ibFrontendMainFrame {
public:

	static ibFrontendMainFrameEnterprise* GetFrame();

	// The window is built around an authenticated session and owns it
	// from that moment on.
	explicit ibFrontendMainFrameEnterprise(ibSessionHolder&& holder,
		const wxString& title = _("Enterprise"),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize);
	virtual ~ibFrontendMainFrameEnterprise();

	virtual void Message(const wxString& strMessage, ibStatusMessage status) { m_outputWindow->SharedOutput(strMessage, status); }
	virtual void ClearMessage() { m_outputWindow->ClearAll(); }

	virtual void BackendError(const wxString& strFileName, const wxString& strDocPath, const long line, const wxString& strErrorMessage) const override;

	virtual void CreateGUI();
	virtual bool Show(bool show = true) override;

	ibOutputWindow* GetOutputWindow() const { return m_outputWindow; }

protected:

	void InitializeDefaultMenu();

	virtual void CreateSubSystem();
	virtual void CreateBottomPane();
	virtual void CreateWideGui();

	// This window's open/close questions, both fired on the session's
	// runtime: BeforeStart on the way up, BeforeExit on the way down.
	bool AllowRun() override;
	bool AllowClose() override;

	// The home page — built after BeforeStart / OnStart; its locked tab keeps it first.
	void CreateStartupPage() override;

	/**
	* Adds the default profile to the hot keys.
	*/
	void SetDefaultHotKeys();

	//events:
	void OnClickAllOperation(wxCommandEvent& event);
	void OnToolsSettings(wxCommandEvent& event);
	void OnActiveUsers(wxCommandEvent& event);
	void OnAuditLog(wxCommandEvent& event);
	void OnAbout(wxCommandEvent& event);

private:

	wxMenu* m_menuFile;
	wxMenu* m_menuEdit;
	wxMenu* m_menuOperations;
	wxMenu* m_menuSetting;
	wxMenu* m_menuAdministration;
	wxMenu* m_menuHelp;

	ibOutputWindow* m_outputWindow;
};

#endif 