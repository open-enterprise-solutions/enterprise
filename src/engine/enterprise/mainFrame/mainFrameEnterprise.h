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

	wxID_ENTERPRISE_ABOUT,
	wxID_ENTERPRISE_END
};

#define mainFrame	(CDocEnterpriseMDIFrame::GetFrame())

class CDocEnterpriseMDIFrame : public CDocMDIFrame
{
	wxMenu* m_menuFile;
	wxMenu* m_menuEdit;

	wxMenu* m_menuOperations;
	wxMenu* m_menuSetting;
	wxMenu* m_menuAdministration;
	wxMenu* m_menuHelp;

protected:

	COutputWindow* m_outputWindow;

protected:

	void InitializeDefaultMenu();

	virtual void CreateBottomPane();
	virtual void CreateWideGui();

	virtual bool AllowRun() const;
	virtual bool AllowClose() const;

	/**
	* Adds the default profile to the hot keys.
	*/
	void SetDefaultHotKeys();

public:

	static CDocEnterpriseMDIFrame* GetFrame();

	CDocEnterpriseMDIFrame(const wxString& title = _("Enterprise"),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize);

	virtual void Message(const wxString& strMessage, eStatusMessage status) { m_outputWindow->SharedOutput(strMessage, status); }
	virtual void ClearMessage() { m_outputWindow->ClearAll(); }

	virtual void BackendError(const wxString& strFileName, const wxString& strDocPath, const long line, const wxString& strErrorMessage) const override;

	virtual void CreateGUI();
	virtual bool Show(bool show = true) override;

	COutputWindow* GetOutputWindow() const { return m_outputWindow; }

protected:

	//events:
	void OnClickAllOperation(wxCommandEvent& event);
	void OnToolsSettings(wxCommandEvent& event);
	void OnActiveUsers(wxCommandEvent& event);
	void OnAbout(wxCommandEvent& event);
};

#endif 