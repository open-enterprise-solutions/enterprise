////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"
#include <wx/config.h>

//********************************************************************************
//*                                Hotkey support                                *
//********************************************************************************

void wxAuiDocDesignerMDIFrame::SetDefaultHotKeys()
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

void wxAuiDocDesignerMDIFrame::InitializeDefaultMenu()
{
	m_menuBar = new wxMenuBar;

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

	m_menuBar->Append(m_menuFile, wxGetStockLabel(wxID_FILE));

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

	m_menuBar->Append(m_menuEdit, wxGetStockLabel(wxID_EDIT));

	m_menuDebug = new wxMenu;
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_START, _("Start debugging"));
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING, _("Start without debugging"));
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_ATTACH_FOR_DEBUGGING, _("Attach for debugging..."));
	m_menuDebug->AppendSeparator();

	m_menuDebug->Append(wxID_DESIGNER_DEBUG_NEXT_POINT, _("Continue"))->Enable(false);
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_PAUSE, _("Pause"), _("Pause"))->Enable(false);
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_STEP_INTO, _("Step into"))->Enable(false);
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_STEP_OVER, _("Step over"))->Enable(false);
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_STOP_DEBUGGING, _("Stop debugging"), _("Stop debugging"))->Enable(false);
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_STOP_PROGRAM, _("Stop debugging program"), _("Stop program"))->Enable(false);

	m_menuDebug->AppendSeparator();

	m_menuDebug->Append(wxID_DESIGNER_DEBUG_REMOVE_ALL_DEBUGPOINTS, _("Remove all breakpoits"));

	m_menuConfiguration = new wxMenu;
	m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_RETURN_DATABASE, _("Return to database configuration"));
	m_menuConfiguration->AppendSeparator();
	m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_LOAD, _("Load configuraion"));
	m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_SAVE, _("Save configuration"));

	m_menuBar->Append(m_menuConfiguration, _("Configuration"));
	m_menuBar->Append(m_menuDebug, _("Debug"));

	m_menuAdministration = new wxMenu;
	m_menuAdministration->Append(wxID_APPLICATION_USERS, _("Users"));
	m_menuAdministration->Append(wxID_APPLICATION_ACTIVE_USERS, _("Active users"));
	m_menuBar->Append(m_menuAdministration, _("Administration"));

	m_menuSetting = new wxMenu;
	m_menuBar->Append(m_menuSetting, _("Tools"));
	m_menuSetting->Append(wxID_APPLICATION_SETTING, _("Options..."));

	// Syntax-helper items live in the Tools (Сервис) menu, NOT in Help.
	// On macOS wxWidgets routes the Help menu into the system-native
	// Help menu which intercepts custom handlers; bound commands fire
	// nowhere. Tools menu keeps the commands locally dispatched on
	// every platform. Strings are English in source; gettext .po files
	// localise to ru-RU / uk-UA at runtime.
	m_menuSetting->AppendSeparator();
	// RawCtrl on the accelerator forces the physical Ctrl key on macOS
	// (wxWidgets normally remaps "Ctrl" to Cmd on Mac). The shortcut
	// resolves to the literal Control key on every platform, matching
	// the convention this lookup binding is modelled on.
	m_menuSetting->Append(wxID_FRONTEND_SYNTAX_HELPER,
	                       _("Syntax Helper\tRawCtrl+Alt+F1"));
	m_menuSetting->Append(wxID_FRONTEND_SYNTAX_HELPER_LOOKUP,
	                       _("Look up in Syntax Helper\tRawCtrl+F1"));

	m_menuHelp = new wxMenu;
	m_menuHelp->Append(wxID_DESIGNER_ABOUT, _("About"));
	m_menuBar->Append(m_menuHelp, wxGetStockLabel(wxID_HELP, wxSTOCK_NOFLAGS));

	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { ToggleHelpPane(); },
	     wxID_FRONTEND_SYNTAX_HELPER);
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { OpenHelpForCursor(); },
	     wxID_FRONTEND_SYNTAX_HELPER_LOOKUP);

	// Editor context-menu items use frontend-shared command ids
	// (frontend.dll cannot depend on the designer header). Forward
	// the three frontend debug ids to the equivalent designer ids
	// that the existing OnRunDebugCommand handler dispatches on.
	auto forward = [this](int designerId) {
		return [this, designerId](wxCommandEvent&) {
			wxCommandEvent ev(wxEVT_MENU, designerId);
			ev.SetEventObject(this);
			ProcessWindowEvent(ev);
		};
	};
	Bind(wxEVT_MENU, forward(wxID_DESIGNER_DEBUG_STEP_INTO),
	     wxID_FRONTEND_DEBUG_STEP_INTO);
	Bind(wxEVT_MENU, forward(wxID_DESIGNER_DEBUG_STEP_OVER),
	     wxID_FRONTEND_DEBUG_STEP_OVER);
	Bind(wxEVT_MENU, forward(wxID_DESIGNER_DEBUG_REMOVE_ALL_DEBUGPOINTS),
	     wxID_FRONTEND_DEBUG_REMOVE_ALL_BREAKPOINTS);

	Bind(wxEVT_MENU, &wxAuiDocDesignerMDIFrame::OnRollbackConfiguration, this, wxID_DESIGNER_CONFIGURATION_RETURN_DATABASE);
	Bind(wxEVT_MENU, &wxAuiDocDesignerMDIFrame::OnConfiguration, this, wxID_DESIGNER_CONFIGURATION_LOAD, wxID_DESIGNER_CONFIGURATION_SAVE);

	Bind(wxEVT_MENU, &wxAuiDocDesignerMDIFrame::OnStartDebug, this, wxID_DESIGNER_DEBUG_START);
	Bind(wxEVT_MENU, &wxAuiDocDesignerMDIFrame::OnStartDebugWithoutDebug, this, wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING);
	Bind(wxEVT_MENU, &wxAuiDocDesignerMDIFrame::OnAttachForDebugging, this, wxID_DESIGNER_DEBUG_ATTACH_FOR_DEBUGGING);

	Bind(wxEVT_MENU, &wxAuiDocDesignerMDIFrame::OnRunDebugCommand, this, wxID_DESIGNER_DEBUG_EDIT_POINT, wxID_DESIGNER_DEBUG_REMOVE_ALL_DEBUGPOINTS);
	Bind(wxEVT_MENU, &wxAuiDocDesignerMDIFrame::OnToolsSettings, this, wxID_APPLICATION_SETTING);
	Bind(wxEVT_MENU, &wxAuiDocDesignerMDIFrame::OnUsers, this, wxID_APPLICATION_USERS);
	Bind(wxEVT_MENU, &wxAuiDocDesignerMDIFrame::OnActiveUsers, this, wxID_APPLICATION_ACTIVE_USERS);
	Bind(wxEVT_MENU, &wxAuiDocDesignerMDIFrame::OnAbout, this, wxID_DESIGNER_ABOUT);

	LoadOptions();
}