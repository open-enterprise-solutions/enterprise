#ifndef _MAINFRAME_ENTERPRISE_H__
#define _MAINFRAME_ENTERPRISE_H__

#include "mainFrame.h"

class CMainFrameEnterprise : public CMainFrame
{
	wxMenu *m_menuFile;
	wxMenu *m_menuEdit;

	wxMenu *m_menuOperations;
	wxMenu *m_menuSetting;
	wxMenu *m_menuAdministration;
	wxMenu *m_menuHelp;

	CMetadataTree *m_metadataTree;

protected:

	void InitializeDefaultMenu();

	void CreatePropertyManager();
	void CreateMessageAndDebugBar();

	void CreateWideGui();

	/**
	* Adds the default profile to the hot keys.
	*/
	void SetDefaultHotKeys();

public:

	CMainFrameEnterprise(const wxString& title = wxT("Enterprise"),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize);

	virtual void CreateGUI();

	virtual wxMenu *GetDefaultMenu(int nTypeMenu)
	{
		return NULL;
	}

	//events:
	void OnClickAllOperation(wxCommandEvent &event);
	void OnToolsSettings(wxCommandEvent& event);
	void OnActiveUsers(wxCommandEvent& event);
	void OnAbout(wxCommandEvent& event);
};

#endif 