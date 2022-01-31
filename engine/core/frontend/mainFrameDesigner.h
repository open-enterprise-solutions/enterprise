#ifndef _MAINFRAME_DESIGNER_H__
#define _MAINFRAME_DESIGNER_H__

#include "mainFrame.h"

#include "compiler/debugger/debugEvent.h"

class CMainFrameDesigner : public CMainFrame
{
	wxMenuBar *m_menuBar;

	wxMenu *m_menuFile;
	wxMenu *m_menuEdit;
	wxMenu *m_menuConfiguration;
	wxMenu *m_menuDebug;
	wxMenu *m_menuSetting;
	wxMenu *m_menuAdministration;
	wxMenu *m_menuHelp;

	CMetadataTree *m_metadataTree;

protected:

	void InitializeDefaultMenu();

	void CreateObjectTree();
	void CreatePropertyManager();
	void CreateMessageAndDebugBar();

	void CreateWideGui();

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

	CMainFrameDesigner(const wxString& title = wxT("Designer"),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize);

	virtual ~CMainFrameDesigner();

	virtual wxMenu *GetDefaultMenu(int nTypeMenu)
	{
		switch (nTypeMenu)
		{
		case wxID_FILE: return GetMenuFile();
		case wxID_EDIT: return GetMenuEdit();

		case wxID_APPLICATION_DEBUG: return GetMenuDebug();
		case wxID_APPLICATION_SETTING: return GetMenuSetting();
		}

		return NULL;
	}

	virtual CMetadataTree *GetMetadataTree() const { return m_metadataTree; }
	virtual void CreateGUI() override;

	virtual void Modify(bool modify) override;

	wxMenu *GetMenuFile() { return m_menuFile; }
	wxMenu *GetMenuEdit() { return m_menuEdit; }
	wxMenu *GetMenuDebug() { return m_menuDebug; }
	wxMenu *GetMenuSetting() { return m_menuSetting; }

	void LoadOptions();
	void SaveOptions();

	virtual bool Show(bool show = false) override;

protected:

	//events 
	void OnStartDebug(wxCommandEvent& WXUNUSED(event));
	void OnStartDebugWithoutDebug(wxCommandEvent& WXUNUSED(event));
	void OnAttachForDebugging(wxCommandEvent& WXUNUSED(event));

	void OnRollbackConfiguration(wxCommandEvent& event);
	void OnConfiguration(wxCommandEvent& event);
	void OnRunDebugCommand(wxCommandEvent& event);
	void OnToolsSettings(wxCommandEvent& event);
	void OnUsers(wxCommandEvent& event);
	void OnActiveUsers(wxCommandEvent& event);
	void OnAbout(wxCommandEvent& event);

	void OnDebugEvent(wxDebugEvent &event);
	void OnToolbarClicked(wxEvent &event);
};

#endif 