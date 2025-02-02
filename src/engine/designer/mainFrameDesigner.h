#ifndef _MAINFRAME_DESIGNER_H__
#define _MAINFRAME_DESIGNER_H__

#include "mainFrame.h"
#include "core/compiler/debugger/debugEvent.h"

class wxAuiDocDesignerMDIFrame : public wxAuiDocMDIFrame
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

protected:

	void InitializeDefaultMenu();

	virtual void CreateMetadataPane();
	virtual void CreateBottomPane();
	virtual void CreateWideGui();

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

	wxAuiDocDesignerMDIFrame(const wxString& title = _("Designer"),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize);

	virtual ~wxAuiDocDesignerMDIFrame();

	virtual wxMenu* GetDefaultMenu(int idMenu) const {
		
		switch (idMenu)
		{
		case wxID_FILE:
			return GetMenuFile();
		case wxID_EDIT:
			return GetMenuEdit();
		case wxID_APPLICATION_DEBUG:
			return GetMenuDebug();
		case wxID_APPLICATION_SETTING:
			return GetMenuSetting();
		}

		return NULL;
	}

	virtual COutputWindow* GetOutputWindow() const {
		return m_outputWindow;
	}

	virtual CStackWindow* GetStackWindow() const {
		return m_stackWindow;
	}

	virtual CWatchWindow* GetWatchWindow() const {
		return m_watchWindow;
	}

	virtual CMetadataTree* GetMetadataTree() const {
		return m_metadataTree;
	}
	
	virtual void CreateGUI() override;
	
	virtual void Modify(bool modify) override;
	virtual bool IsModified() const override;

	wxMenu* GetMenuFile()const {
		return m_menuFile;
	}

	wxMenu* GetMenuEdit() const {
		return m_menuEdit;
	}

	wxMenu* GetMenuDebug() const {
		return m_menuDebug;
	}

	wxMenu* GetMenuSetting() const {
		return m_menuSetting;
	}

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

	void OnDebugEvent(wxDebugEvent& event);
	void OnToolbarClicked(wxEvent& event);
};

#endif 