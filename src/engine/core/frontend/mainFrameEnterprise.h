#ifndef _MAINFRAME_ENTERPRISE_H__
#define _MAINFRAME_ENTERPRISE_H__

#include "mainFrame.h"

class wxAuiDocEnterpriseMDIFrame : public wxAuiDocMDIFrame
{
	wxMenu *m_menuFile;
	wxMenu *m_menuEdit;

	wxMenu *m_menuOperations;
	wxMenu *m_menuSetting;
	wxMenu *m_menuAdministration;
	wxMenu *m_menuHelp;

protected:

	COutputWindow* m_outputWindow;

protected:

	virtual COutputWindow* GetOutputWindow() const {
		return m_outputWindow;
	}

	virtual CStackWindow* GetStackWindow() const {
		return NULL; 
	}
	
	virtual CWatchWindow* GetWatchWindow() const {
		return NULL;
	}

	void InitializeDefaultMenu();

	virtual void CreateBottomPane();
	virtual void CreateWideGui();

	/**
	* Adds the default profile to the hot keys.
	*/
	void SetDefaultHotKeys();

public:

	wxAuiDocEnterpriseMDIFrame(const wxString& title = _("Enterprise"),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize);

	virtual void CreateGUI();

	//events:
	void OnClickAllOperation(wxCommandEvent &event);
	void OnToolsSettings(wxCommandEvent& event);
	void OnActiveUsers(wxCommandEvent& event);
	void OnAbout(wxCommandEvent& event);
};

#endif 