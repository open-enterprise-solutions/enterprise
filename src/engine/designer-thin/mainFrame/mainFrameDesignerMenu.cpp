////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the designer's menu and keys
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"

void ibFrontendMainFrameDesigner::SetDefaultHotKeys()
{
	ibFrontendMainFrame::SetDefaultHotKeys();

	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_START, wxT("F5")); //RUN
	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING, wxT("Ctrl+F5")); // RUN WITHOUT DEBUGGER
	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_STEP_INTO, wxT("F11")); //STEP INTO
	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_STEP_OVER, wxT("F10")); // STEP OVER
	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_STEP_OUT, wxT("Shift+F11")); // STEP OUT
	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_STOP_PROGRAM, wxT("Ctrl+Break")); // STOP PROGRAM
	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_NEXT_POINT, wxT("F9"));

	m_keyBinder.SetShortcut(wxID_DESIGNER_ABOUT, wxT("F1"));

	// Syntax helper. RawCtrl forces literal Control on macOS where
	// wxWidgets otherwise rewrites "Ctrl" to Cmd; on Windows / Linux
	// RawCtrl is identical to Ctrl. Without these SetShortcut calls
	// ibKeyBinder strips the accelerator labels during LoadOptions.
	m_keyBinder.SetShortcut(wxID_FRONTEND_SYNTAX_HELPER, wxT("RawCtrl+Alt+F1"));
}

//********************************************************************************
//*                                Default menu                                  *
//********************************************************************************

#include "frmclient/artProvider/artProvider.h"   // the configuration's pictures — wxART_DATABASE…

void ibFrontendMainFrameDesigner::InitializeDefaultMenu()
{
	m_menuDebug = new wxMenu;

	// The debugger's commands carry the pictures their buttons carry on the debug toolbar (wxART_DEBUG) — one
	// command, one picture, wherever it is pressed from.
	const auto debugPicture = [](wxMenuItem* item, const wxArtID& art) {
		item->SetBitmap(wxArtProvider::GetBitmapBundle(art, wxART_DEBUG, wxSize(16, 16)));
		return item;
	};

	// "Start debugging" → GUI / Thin / Web
	wxMenu* subStart = new wxMenu;
	debugPicture(subStart->Append(wxID_DESIGNER_DEBUG_START, _("Thick client (GUI)")), wxART_DEBUG_START);
	subStart->Append(wxID_DESIGNER_DEBUG_START_THIN, _("Thin client"));
	subStart->Append(wxID_DESIGNER_DEBUG_START_WEB, _("Web client"));
	debugPicture(m_menuDebug->AppendSubMenu(subStart, _("Start debugging")), wxART_DEBUG_START);

	// "Start without debugging" → GUI / Thin / Web
	wxMenu* subStartNoDebug = new wxMenu;
	debugPicture(subStartNoDebug->Append(wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING, _("Thick client (GUI)")), wxART_DEBUG_START_WITHOUT_DEBUGGING);
	subStartNoDebug->Append(wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING_THIN, _("Thin client"));
	subStartNoDebug->Append(wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING_WEB, _("Web client"));
	debugPicture(m_menuDebug->AppendSubMenu(subStartNoDebug, _("Start without debugging")), wxART_DEBUG_START_WITHOUT_DEBUGGING);

	debugPicture(m_menuDebug->Append(wxID_DESIGNER_DEBUG_ATTACH_FOR_DEBUGGING, _("Attach for debugging...")), wxART_DEBUG_ATTACH);
	m_menuDebug->AppendSeparator();

	debugPicture(m_menuDebug->Append(wxID_DESIGNER_DEBUG_NEXT_POINT, _("Continue")), wxART_DEBUG_CONTINUE)->Enable(false);
	debugPicture(m_menuDebug->Append(wxID_DESIGNER_DEBUG_PAUSE, _("Pause"), _("Pause")), wxART_DEBUG_PAUSE)->Enable(false);
	debugPicture(m_menuDebug->Append(wxID_DESIGNER_DEBUG_STEP_INTO, _("Step into")), wxART_DEBUG_STEP_INTO)->Enable(false);
	debugPicture(m_menuDebug->Append(wxID_DESIGNER_DEBUG_STEP_OVER, _("Step over")), wxART_DEBUG_STEP_OVER)->Enable(false);
	debugPicture(m_menuDebug->Append(wxID_DESIGNER_DEBUG_STEP_OUT, _("Step out")), wxART_DEBUG_STEP_OUT)->Enable(false);
	debugPicture(m_menuDebug->Append(wxID_DESIGNER_DEBUG_STOP_DEBUGGING, _("Stop debugging"), _("Stop debugging")), wxART_DEBUG_STOP_DEBUGGING)->Enable(false);
	debugPicture(m_menuDebug->Append(wxID_DESIGNER_DEBUG_STOP_PROGRAM, _("Stop debugging program"), _("Stop program")), wxART_DEBUG_STOP_PROGRAM)->Enable(false);

	m_menuDebug->AppendSeparator();
	debugPicture(m_menuDebug->Append(wxID_DESIGNER_DEBUG_REMOVE_ALL_DEBUGPOINTS, _("Remove all breakpoints")), wxART_DEBUG_REMOVE_ALL_BREAKPOINTS);

	wxMenuItem* menuItem = nullptr;

	// ⚠ WHAT THE PERSON MAY DO IS THE SERVER'S TO SAY — its MetaData doors are refused without the right to the base, and
	// the refusal is said; the desktop's items greyed by that right stand here. Open database configuration and the
	// comparisons are not ported yet — their tabs (plan: thin-designer-editors.md §8); until they are, offered greyed.
	m_menuConfiguration = new wxMenu;

	menuItem = m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_OPEN_DATABASE, _("Open database configuration"));
	menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DATABASE, wxART_FRONTEND, wxSize(16, 16)));
	menuItem->Enable(false);
	menuItem = m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_ROLLBACK_DATABASE, _("Rollback to database configuration"));
	menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DATABASE_ROOLBACK, wxART_FRONTEND, wxSize(16, 16)));

	menuItem = m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_UPDATE_DATABASE, _("Update database configuration"));
	menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DATABASE_APPLY, wxART_FRONTEND, wxSize(16, 16)));

	m_menuConfiguration->AppendSeparator();

	m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_LOAD_FROM_FILE, _("Load configuration"));
	m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_SAVE_TO_FILE, _("Save configuration"));

	m_menuConfiguration->AppendSeparator();

	// "Compare configurations" submenu — three entry points (file / DB
	// baseline / two arbitrary files) grouped together so the parent
	// Configuration menu stays compact.
	wxMenu* menuCompare = new wxMenu;

	menuItem = menuCompare->Append(wxID_DESIGNER_CONFIGURATION_COMPARE_FILE, _("With file..."));
	menuItem->Enable(false);

	menuItem = menuCompare->Append(wxID_DESIGNER_CONFIGURATION_COMPARE_DB, _("With database configuration"));
	menuItem->Enable(false);

	menuItem = menuCompare->Append(wxID_DESIGNER_CONFIGURATION_COMPARE_TWO_FILES, _("Two files..."));
	menuItem->Enable(false);

	m_menuConfiguration->AppendSubMenu(menuCompare, _("Compare configurations"));

	m_menuHelp = new wxMenu;
	// Syntax helper pane toggle. RawCtrl forces the literal Control key on
	// every platform (wxWidgets maps "Ctrl" to Cmd on macOS).
	m_menuHelp->Append(wxID_FRONTEND_SYNTAX_HELPER,
	                   _("Syntax Helper\tRawCtrl+Alt+F1"));
	m_menuHelp->AppendSeparator();
	m_menuHelp->Append(wxID_DESIGNER_ABOUT, _("About"));

	ibFrontendMainFrame::InitializeDefaultMenu();

	Bind(wxEVT_MENU, &ibFrontendMainFrameDesigner::OnRollbackConfiguration, this, wxID_DESIGNER_CONFIGURATION_ROLLBACK_DATABASE);
	Bind(wxEVT_MENU, &ibFrontendMainFrameDesigner::OnUpdateConfiguration, this, wxID_DESIGNER_CONFIGURATION_UPDATE_DATABASE);
	Bind(wxEVT_MENU, &ibFrontendMainFrameDesigner::OnConfiguration, this, wxID_DESIGNER_CONFIGURATION_LOAD_FROM_FILE, wxID_DESIGNER_CONFIGURATION_SAVE_TO_FILE);
	Bind(wxEVT_UPDATE_UI, &ibFrontendMainFrameDesigner::OnUpdateSaveMetadata, this, wxID_DESIGNER_CONFIGURATION_ROLLBACK_DATABASE);
	Bind(wxEVT_UPDATE_UI, &ibFrontendMainFrameDesigner::OnUpdateSaveMetadata, this, wxID_DESIGNER_CONFIGURATION_UPDATE_DATABASE);

	Bind(wxEVT_MENU, &ibFrontendMainFrameDesigner::OnStartDebug, this, wxID_DESIGNER_DEBUG_START);
	Bind(wxEVT_MENU, &ibFrontendMainFrameDesigner::OnStartDebugWithoutDebug, this, wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING);
	Bind(wxEVT_MENU, &ibFrontendMainFrameDesigner::OnStartDebugWeb, this, wxID_DESIGNER_DEBUG_START_WEB);
	Bind(wxEVT_MENU, &ibFrontendMainFrameDesigner::OnStartDebugWithoutDebugWeb, this, wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING_WEB);
	Bind(wxEVT_MENU, &ibFrontendMainFrameDesigner::OnStartDebugThin, this, wxID_DESIGNER_DEBUG_START_THIN);
	Bind(wxEVT_MENU, &ibFrontendMainFrameDesigner::OnStartDebugWithoutDebugThin, this, wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING_THIN);
	Bind(wxEVT_MENU, &ibFrontendMainFrameDesigner::OnAttachForDebugging, this, wxID_DESIGNER_DEBUG_ATTACH_FOR_DEBUGGING);

	Bind(wxEVT_MENU, &ibFrontendMainFrameDesigner::OnRunDebugCommand, this, wxID_DESIGNER_DEBUG_EDIT_POINT, wxID_DESIGNER_DEBUG_REMOVE_ALL_DEBUGPOINTS);
	Bind(wxEVT_UPDATE_UI, &ibFrontendMainFrameDesigner::OnUpdateDebugCommand, this, wxID_DESIGNER_DEBUG_STEP_OVER, wxID_DESIGNER_DEBUG_NEXT_POINT);

	// Syntax helper — lambda bindings, no member fn to lose to the
	// designer header.
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { ToggleHelpPane(); },
	     wxID_FRONTEND_SYNTAX_HELPER);
	// About — the server's schema, asked for and shown as its menu item would be.
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { OpenSchema(ibProtocolSchema::About); },
	     wxID_DESIGNER_ABOUT);
}
