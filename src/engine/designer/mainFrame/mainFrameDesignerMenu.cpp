////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"

//********************************************************************************
//*                                Hotkey support                                *
//********************************************************************************

void ibFrontendDocMDIFrameDesigner::SetDefaultHotKeys()
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

	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_START, wxT("F5")); //RUN 
	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING, wxT("Ctrl+F5")); // RUN WITHOUT DEBUGGER 
	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_STEP_INTO, wxT("F11")); //STEP INTO 
	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_STEP_OVER, wxT("F10")); // STEP OVER 
	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_STOP_PROGRAM, wxT("Ctrl+Break")); // STEP OVER 
	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_NEXT_POINT, wxT("F9"));

	m_keyBinder.SetShortcut(wxID_DESIGNER_ABOUT, wxT("F1"));

	// Syntax helper. RawCtrl forces literal Control on macOS where
	// wxWidgets otherwise rewrites "Ctrl" to Cmd; on Windows / Linux
	// RawCtrl is identical to Ctrl. Without these SetShortcut calls
	// ibKeyBinder strips the accelerator labels during LoadOptions.
	m_keyBinder.SetShortcut(wxID_FRONTEND_SYNTAX_HELPER,        wxT("RawCtrl+Alt+F1"));
	m_keyBinder.SetShortcut(wxID_FRONTEND_SYNTAX_HELPER_LOOKUP, wxT("RawCtrl+F1"));
}

//********************************************************************************
//*                                Default menu                                  *
//********************************************************************************

enum MDI_MENU_ID
{
	wxWINDOWCLOSE = 4001,
	wxWINDOWCLOSEALL,
	wxWINDOWNEXT,
	wxWINDOWPREV
};

#include <wx/artprov.h>
#include <wx/config.h>

#include "frontend/artProvider/artProvider.h"

void ibFrontendDocMDIFrameDesigner::InitializeDefaultMenu()
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
	m_menuFile->Append(wxID_PRINT_SETUP, _("Print &Setup..."));
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

	m_menuDebug = new wxMenu;

	// "Start debugging" → GUI / Web
	wxMenu* subStart = new wxMenu;
	subStart->Append(wxID_DESIGNER_DEBUG_START, _("Thick client (GUI)"));
	subStart->Append(wxID_DESIGNER_DEBUG_START_WEB, _("Web client"));
	m_menuDebug->AppendSubMenu(subStart, _("Start debugging"));

	// "Start without debugging" → GUI / Web
	wxMenu* subStartNoDebug = new wxMenu;
	subStartNoDebug->Append(wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING, _("Thick client (GUI)"));
	subStartNoDebug->Append(wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING_WEB, _("Web client"));
	m_menuDebug->AppendSubMenu(subStartNoDebug, _("Start without debugging"));

	m_menuDebug->Append(wxID_DESIGNER_DEBUG_ATTACH_FOR_DEBUGGING, _("Attach for debugging..."));
	m_menuDebug->AppendSeparator();

	m_menuDebug->Append(wxID_DESIGNER_DEBUG_NEXT_POINT, _("Continue"))->Enable(false);
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_PAUSE, _("Pause"), _("Pause"))->Enable(false);
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_STEP_INTO, _("Step into"))->Enable(false);
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_STEP_OVER, _("Step over"))->Enable(false);
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_STOP_DEBUGGING, _("Stop debugging"), _("Stop debugging"))->Enable(false);
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_STOP_PROGRAM, _("Stop debugging program"), _("Stop program"))->Enable(false);

	m_menuDebug->AppendSeparator();
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_REMOVE_ALL_DEBUGPOINTS, _("Remove all breakpoints"));

	m_menuConfiguration = new wxMenu;

	wxMenuItem* menuItem = nullptr;
	
	menuItem = m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_OPEN_DATABASE, _("Open database configuration"));
	menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DATABASE, wxART_FRONTEND, wxSize(16, 16)));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());	
	menuItem = m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_ROLLBACK_DATABASE, _("Rollback to database configuration"));
	menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DATABASE_ROOLBACK, wxART_FRONTEND, wxSize(16, 16)));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());

	menuItem = m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_UPDATE_DATABASE, _("Update database configuration"));
	menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DATABASE_APPLY, wxART_FRONTEND, wxSize(16, 16)));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());

	m_menuConfiguration->AppendSeparator();

	menuItem = m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_LOAD_FROM_FILE, _("Load configuration"));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());
	menuItem = m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_SAVE_TO_FILE, _("Save configuration"));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());

	m_menuConfiguration->AppendSeparator();

	menuItem = m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_COMPARE_FILE, _("Compare with file..."));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());

	menuItem = m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_COMPARE_DB, _("Compare with database configuration"));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());

	menuItem = m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_COMPARE_TWO_FILES, _("Compare two files..."));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());

	m_frameMenuBar->Append(m_menuConfiguration, _("Configuration"));
	m_frameMenuBar->Append(m_menuDebug, _("Debug"));

	m_menuAdministration = new wxMenu;
	
	menuItem = m_menuAdministration->Append(wxID_APPLICATION_USERS, _("Users"));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());
	menuItem = m_menuAdministration->Append(wxID_APPLICATION_ACTIVE_USERS, _("Active users"));
	menuItem->Enable(activeMetaData->AccessRight_ActiveUsers());
	menuItem = m_menuAdministration->Append(wxID_APPLICATION_AUDIT_LOG, _("Registration journal"));
	menuItem->Enable(activeMetaData->AccessRight_ActiveUsers());
	m_menuAdministration->AppendSeparator();
	menuItem = m_menuAdministration->Append(wxID_DESIGNER_DATABASE_LOAD_FROM_FILE, _("Restore database"));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());
	menuItem = m_menuAdministration->Append(wxID_DESIGNER_DATABASE_SAVE_TO_FILE, _("Dump database"));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());
	m_menuAdministration->AppendSeparator();
	menuItem = m_menuAdministration->Append(wxID_DESIGNER_DATABASE_CLEAR, _("Clear database"));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());

	m_frameMenuBar->Append(m_menuAdministration, _("Administration"));

	m_menuSetting = new wxMenu;
	m_frameMenuBar->Append(m_menuSetting, _("Tools"));
	m_menuSetting->Append(wxID_APPLICATION_SETTING, _("Options..."));

	m_menuHelp = new wxMenu;
	// Syntax helper pane toggle. Cursor look-up has no menu entry —
	// the editor's right-click context menu and the RawCtrl+F1
	// accelerator (m_keyBinder) cover that path; a second top-level
	// menu surface for the same action just confuses the menubar.
	// RawCtrl forces the literal Control key on every platform
	// (wxWidgets maps "Ctrl" to Cmd on macOS). NB: on macOS the Help
	// menu can be intercepted by the system-native Help search; if
	// that surfaces as a real problem this can move to Tools (Windows
	// is the primary platform now).
	m_menuHelp->Append(wxID_FRONTEND_SYNTAX_HELPER,
	                   _("Syntax Helper\tRawCtrl+Alt+F1"));
	m_menuHelp->AppendSeparator();
	m_menuHelp->Append(wxID_DESIGNER_ABOUT, _("About"));
	m_frameMenuBar->Append(m_menuHelp, wxGetStockLabel(wxID_HELP, wxSTOCK_NOFLAGS));

	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnOpenConfiguration, this, wxID_DESIGNER_CONFIGURATION_OPEN_DATABASE);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnRollbackConfiguration, this, wxID_DESIGNER_CONFIGURATION_ROLLBACK_DATABASE);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnUpdateConfiguration, this, wxID_DESIGNER_CONFIGURATION_UPDATE_DATABASE);

	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnConfiguration, this, wxID_DESIGNER_CONFIGURATION_LOAD_FROM_FILE, wxID_DESIGNER_CONFIGURATION_COMPARE_TWO_FILES);

	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnStartDebug, this, wxID_DESIGNER_DEBUG_START);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnStartDebugWithoutDebug, this, wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnStartDebugWeb, this, wxID_DESIGNER_DEBUG_START_WEB);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnStartDebugWithoutDebugWeb, this, wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING_WEB);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnAttachForDebugging, this, wxID_DESIGNER_DEBUG_ATTACH_FOR_DEBUGGING);

	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnRunDebugCommand, this, wxID_DESIGNER_DEBUG_EDIT_POINT, wxID_DESIGNER_DEBUG_REMOVE_ALL_DEBUGPOINTS);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnToolsSettings, this, wxID_APPLICATION_SETTING);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnUsers, this, wxID_APPLICATION_USERS);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnActiveUsers, this, wxID_APPLICATION_ACTIVE_USERS);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnAuditLog, this, wxID_APPLICATION_AUDIT_LOG);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnConnection, this, wxID_APPLICATION_CONNECTION);

	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnLoadDatabase, this, wxID_DESIGNER_DATABASE_LOAD_FROM_FILE);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnSaveDatabase, this, wxID_DESIGNER_DATABASE_SAVE_TO_FILE);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnClearDatabase, this, wxID_DESIGNER_DATABASE_CLEAR);

	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnAbout, this, wxID_DESIGNER_ABOUT);

	// Syntax helper — lambda bindings, no member fn to lose to the
	// designer header.
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { ToggleHelpPane(); },
	     wxID_FRONTEND_SYNTAX_HELPER);
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { OpenHelpForCursor(); },
	     wxID_FRONTEND_SYNTAX_HELPER_LOOKUP);

	LoadOptions();
}