////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameEnterprise.h"
#include "backend/metadataConfiguration.h"

//********************************************************************************
//*                                Hotkey support                                *
//********************************************************************************

void ibFrontendMainFrameEnterprise::SetDefaultHotKeys()
{
	// Setup the hotkeys.
	m_keyBinder.SetShortcut(wxID_NEW, wxT("Ctrl+N"));
	m_keyBinder.SetShortcut(wxID_SAVE, wxT("Ctrl+S"));
	m_keyBinder.SetShortcut(wxID_UNDO, wxT("Ctrl+Z"));
	m_keyBinder.SetShortcut(wxID_REDO, wxT("Ctrl+Y"));

	m_keyBinder.SetShortcut(wxID_CUT, wxT("Ctrl+X"));
	m_keyBinder.SetShortcut(wxID_COPY, wxT("Ctrl+C"));
	m_keyBinder.SetShortcut(wxID_PASTE, wxT("Ctrl+V"));
	m_keyBinder.SetShortcut(wxID_SELECTALL, wxT("Ctrl+A"));
	m_keyBinder.SetShortcut(wxID_FIND, wxT("Ctrl+F"));

	m_keyBinder.SetShortcut(wxID_ENTERPRISE_ABOUT, wxT("F1"));
}

//********************************************************************************
//*                                Default menu                                  *
//********************************************************************************

void ibFrontendMainFrameEnterprise::InitializeDefaultMenu()
{
	m_frameMenuBar = new wxMenuBar;

	// and its menu bar
	m_menuFile = new wxMenu();

	m_menuFile->Append(wxID_NEW);
	m_menuFile->Append(wxID_OPEN);

	m_menuFile->Append(wxID_CLOSE);
	m_menuFile->Append(wxID_SAVE);
	m_menuFile->Append(wxID_SAVEAS);
	m_menuFile->Append(wxID_REVERT, _("Re&vert..."));

	m_menuFile->AppendSeparator();
	m_menuFile->Append(wxID_PRINT);
	m_menuFile->Append(wxID_PRINT_SETUP, "Print &Setup...");
	m_menuFile->Append(wxID_PREVIEW);

	m_menuFile->AppendSeparator();
	m_menuFile->Append(wxID_EXIT);

	m_frameMenuBar->Append(m_menuFile, wxGetStockLabel(wxID_FILE));

	// A nice touch: a history of files visited. Use this menu.
	m_docManager->FileHistoryUseMenu(m_menuFile);

#if wxUSE_CONFIG
	m_docManager->FileHistoryLoad(*wxConfig::Get());
#endif // wxUSE_CONFIG

	m_menuEdit = new wxMenu;
	m_menuEdit->Append(wxID_UNDO);
	m_menuEdit->Append(wxID_REDO);
	m_menuEdit->AppendSeparator();
	m_menuEdit->Append(wxID_CUT);
	m_menuEdit->Append(wxID_COPY);
	m_menuEdit->Append(wxID_PASTE);
	m_menuEdit->Append(wxID_DELETE);
	m_menuEdit->Append(wxID_SELECTALL);
	m_menuEdit->AppendSeparator();
	m_menuEdit->Append(wxID_FIND);

	m_frameMenuBar->Append(m_menuEdit, wxGetStockLabel(wxID_EDIT));

	if (activeMetaData->AccessRight_ModeAllFunction()) {
		m_menuOperations = new wxMenu;
		m_menuOperations->Append(wxID_ENTERPRISE_ALL_OPERATIONS, _("All operations..."));
		m_frameMenuBar->Append(m_menuOperations, _("Operations"));
	}

	m_menuSetting = new wxMenu;
	m_frameMenuBar->Append(m_menuSetting, _("Tools"));
	m_menuSetting->Append(wxID_ENTERPRISE_SETTING, _("Options..."));

	if (activeMetaData->AccessRight_ActiveUsers()) {
		m_menuAdministration = new wxMenu;
		m_menuAdministration->Append(wxID_ENTERPRISE_ACTIVE_USERS, _("Active users"));
		m_menuAdministration->AppendSeparator();
		m_menuAdministration->Append(wxID_ENTERPRISE_AUDIT_LOG,    _("Registration journal"));
		m_frameMenuBar->Append(m_menuAdministration, _("Administration"));
	}

	m_menuHelp = new wxMenu;
	m_menuHelp->Append(wxID_ENTERPRISE_ABOUT, _("About"));
	m_frameMenuBar->Append(m_menuHelp, wxGetStockLabel(wxID_HELP, wxSTOCK_NOFLAGS));

	wxMenuBar* mb = GetMenuBar();
	m_frameMenuBar = nullptr;
	SetMenuBar(mb);

	m_keyBinder.AddCommandsFromMenuBar(mb);

	SetDefaultHotKeys();

	Bind(wxEVT_MENU, &ibFrontendMainFrameEnterprise::OnClickAllOperation, this, wxID_ENTERPRISE_ALL_OPERATIONS);
	Bind(wxEVT_MENU, &ibFrontendMainFrameEnterprise::OnToolsSettings, this, wxID_ENTERPRISE_SETTING);
	Bind(wxEVT_MENU, &ibFrontendMainFrameEnterprise::OnActiveUsers, this, wxID_ENTERPRISE_ACTIVE_USERS);
	Bind(wxEVT_MENU, &ibFrontendMainFrameEnterprise::OnAuditLog, this, wxID_ENTERPRISE_AUDIT_LOG);
	Bind(wxEVT_MENU, &ibFrontendMainFrameEnterprise::OnAbout, this, wxID_ENTERPRISE_ABOUT);

	m_keyBinder.UpdateWindow(this);
	m_keyBinder.UpdateMenuBar(mb);
}