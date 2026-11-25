////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the studio's menu and keys
////////////////////////////////////////////////////////////////////////////

#include "mainFrameStudio.h"

void ibFrontendMainFrameStudio::SetDefaultHotKeys()
{
	ibFrontendMainFrame::SetDefaultHotKeys();

	m_keyBinder.SetShortcut(wxID_STUDIO_DEBUG_START, wxT("F5")); //RUN
	m_keyBinder.SetShortcut(wxID_STUDIO_DEBUG_START_WITHOUT_DEBUGGING, wxT("Ctrl+F5")); // RUN WITHOUT DEBUGGER
	m_keyBinder.SetShortcut(wxID_STUDIO_DEBUG_STEP_INTO, wxT("F11")); //STEP INTO
	m_keyBinder.SetShortcut(wxID_STUDIO_DEBUG_STEP_OVER, wxT("F10")); // STEP OVER
	m_keyBinder.SetShortcut(wxID_STUDIO_DEBUG_STEP_OUT, wxT("Shift+F11")); // STEP OUT
	m_keyBinder.SetShortcut(wxID_STUDIO_DEBUG_STOP_PROGRAM, wxT("Ctrl+Break")); // STOP PROGRAM
	m_keyBinder.SetShortcut(wxID_STUDIO_DEBUG_NEXT_POINT, wxT("F9"));

	m_keyBinder.SetShortcut(wxID_STUDIO_ABOUT, wxT("F1"));

	// Syntax helper. RawCtrl forces literal Control on macOS where
	// wxWidgets otherwise rewrites "Ctrl" to Cmd; on Windows / Linux
	// RawCtrl is identical to Ctrl. Without these SetShortcut calls
	// ibKeyBinder strips the accelerator labels during LoadOptions.
	m_keyBinder.SetShortcut(wxID_FRONTEND_SYNTAX_HELPER, wxT("RawCtrl+Alt+F1"));
}

//********************************************************************************
//*                                Default menu                                  *
//********************************************************************************

#include "deskgui/artProvider.h"   // the application's pictures — wxART_DATABASE…

void ibFrontendMainFrameStudio::InitializeDefaultMenu()
{
	m_menuDebug = new ibMenu;

	// The debugger's commands carry the pictures their buttons carry on the debug toolbar (wxART_DEBUG) — one
	// command, one picture, wherever it is pressed from.
	const auto debugPicture = [](wxMenuItem* item, const wxArtID& art) {
		item->SetBitmap(wxArtProvider::GetBitmapBundle(art, wxART_DEBUG, wxSize(16, 16)));
		return item;
	};

	// "Start debugging" → the workspace, on the desktop or in the browser - the launcher's words for the same two
	// (2026-11-13, Max: "Thin client" says nothing - everything is thin)
	ibMenu* subStart = new ibMenu;
	debugPicture(subStart->Append(wxID_STUDIO_DEBUG_START, _("Workspace (GUI)")), wxART_DEBUG_START);
	subStart->Append(wxID_STUDIO_DEBUG_START_WEB, _("Workspace (Web)"));
	debugPicture(m_menuDebug->AppendSubMenu(subStart, _("Start debugging")), wxART_DEBUG_START);

	// "Start without debugging" → the same two
	ibMenu* subStartNoDebug = new ibMenu;
	debugPicture(subStartNoDebug->Append(wxID_STUDIO_DEBUG_START_WITHOUT_DEBUGGING, _("Workspace (GUI)")), wxART_DEBUG_START_WITHOUT_DEBUGGING);
	subStartNoDebug->Append(wxID_STUDIO_DEBUG_START_WITHOUT_DEBUGGING_WEB, _("Workspace (Web)"));
	debugPicture(m_menuDebug->AppendSubMenu(subStartNoDebug, _("Start without debugging")), wxART_DEBUG_START_WITHOUT_DEBUGGING);

	debugPicture(m_menuDebug->Append(wxID_STUDIO_DEBUG_ATTACH_FOR_DEBUGGING, _("Attach for debugging...")), wxART_DEBUG_ATTACH);
	m_menuDebug->AppendSeparator();

	debugPicture(m_menuDebug->Append(wxID_STUDIO_DEBUG_NEXT_POINT, _("Continue")), wxART_DEBUG_CONTINUE)->Enable(false);
	debugPicture(m_menuDebug->Append(wxID_STUDIO_DEBUG_PAUSE, _("Pause"), _("Pause")), wxART_DEBUG_PAUSE)->Enable(false);
	debugPicture(m_menuDebug->Append(wxID_STUDIO_DEBUG_STEP_INTO, _("Step into")), wxART_DEBUG_STEP_INTO)->Enable(false);
	debugPicture(m_menuDebug->Append(wxID_STUDIO_DEBUG_STEP_OVER, _("Step over")), wxART_DEBUG_STEP_OVER)->Enable(false);
	debugPicture(m_menuDebug->Append(wxID_STUDIO_DEBUG_STEP_OUT, _("Step out")), wxART_DEBUG_STEP_OUT)->Enable(false);
	debugPicture(m_menuDebug->Append(wxID_STUDIO_DEBUG_STOP_DEBUGGING, _("Stop debugging"), _("Stop debugging")), wxART_DEBUG_STOP_DEBUGGING)->Enable(false);
	debugPicture(m_menuDebug->Append(wxID_STUDIO_DEBUG_STOP_PROGRAM, _("Stop debugging program"), _("Stop program")), wxART_DEBUG_STOP_PROGRAM)->Enable(false);

	m_menuDebug->AppendSeparator();
	m_menuDebug->AppendCheckItem(wxID_STUDIO_DEBUG_PROFILE, _("Performance measurement"))->Enable(false);

	m_menuDebug->AppendSeparator();
	debugPicture(m_menuDebug->Append(wxID_STUDIO_DEBUG_REMOVE_ALL_DEBUGPOINTS, _("Remove all breakpoints")), wxART_DEBUG_REMOVE_ALL_BREAKPOINTS);

	wxMenuItem* menuItem = nullptr;

	// ⚠ WHAT THE PERSON MAY DO IS THE SERVER'S TO SAY — its MetaData doors are refused without the right to the base, and
	// the refusal is said; the desktop's items greyed by that right stand here. Open the database's application and the
	// comparisons are the desktop's windows (docViewMetaFile, docViewApplicationCompare): the first is MetaData's Browse,
	// Walk/Merge go through schema ApplicationCompare.
	m_menuApplication = new ibMenu;

	menuItem = m_menuApplication->Append(wxID_STUDIO_APPLICATION_OPEN_DATABASE, _("Open the database's application"));
	menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DATABASE, wxART_FRONTEND, wxSize(16, 16)));
	menuItem = m_menuApplication->Append(wxID_STUDIO_APPLICATION_ROLLBACK_DATABASE, _("Revert to the database's application"));
	menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DATABASE_ROOLBACK, wxART_FRONTEND, wxSize(16, 16)));

	menuItem = m_menuApplication->Append(wxID_STUDIO_APPLICATION_UPDATE_DATABASE, _("Update database"));
	menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DATABASE_APPLY, wxART_FRONTEND, wxSize(16, 16)));

	m_menuApplication->AppendSeparator();

	m_menuApplication->Append(wxID_STUDIO_APPLICATION_LOAD_FROM_FILE, _("Load application"));
	m_menuApplication->Append(wxID_STUDIO_APPLICATION_SAVE_TO_FILE, _("Save application"));

	m_menuApplication->AppendSeparator();

	// …and as a directory of files, one a part — for a repository and its diffs.
	m_menuApplication->Append(wxID_STUDIO_APPLICATION_LOAD_FROM_FILES, _("Load application from files..."));
	m_menuApplication->Append(wxID_STUDIO_APPLICATION_SAVE_TO_FILES, _("Save application to files..."));

	m_menuApplication->AppendSeparator();

	// "Compare applications" submenu — three entry points (file / DB
	// baseline / two arbitrary files) grouped together so the parent
	// Application menu stays compact.
	ibMenu* menuCompare = new ibMenu;

	menuCompare->Append(wxID_STUDIO_APPLICATION_COMPARE_FILE, _("With file..."));
	menuCompare->Append(wxID_STUDIO_APPLICATION_COMPARE_DB, _("With the database's application"));
	menuCompare->Append(wxID_STUDIO_APPLICATION_COMPARE_TWO_FILES, _("Two files..."));

	m_menuApplication->AppendSubMenu(menuCompare, _("Compare applications"));

	m_menuHelp = new ibMenu;
	// Syntax helper pane toggle. RawCtrl forces the literal Control key on
	// every platform (wxWidgets maps "Ctrl" to Cmd on macOS).
	m_menuHelp->Append(wxID_FRONTEND_SYNTAX_HELPER,
	                   _("Syntax Helper\tRawCtrl+Alt+F1"));
	m_menuHelp->AppendSeparator();
	m_menuHelp->Append(wxID_STUDIO_ABOUT, _("About"));

	ibFrontendMainFrame::InitializeDefaultMenu();

	Bind(wxEVT_MENU, &ibFrontendMainFrameStudio::OnOpenApplication, this, wxID_STUDIO_APPLICATION_OPEN_DATABASE);
	Bind(wxEVT_MENU, &ibFrontendMainFrameStudio::OnRollbackApplication, this, wxID_STUDIO_APPLICATION_ROLLBACK_DATABASE);
	Bind(wxEVT_MENU, &ibFrontendMainFrameStudio::OnUpdateApplication, this, wxID_STUDIO_APPLICATION_UPDATE_DATABASE);
	Bind(wxEVT_MENU, &ibFrontendMainFrameStudio::OnApplication, this, wxID_STUDIO_APPLICATION_LOAD_FROM_FILE, wxID_STUDIO_APPLICATION_COMPARE_TWO_FILES);
	Bind(wxEVT_MENU, &ibFrontendMainFrameStudio::OnApplication, this, wxID_STUDIO_APPLICATION_LOAD_FROM_FILES, wxID_STUDIO_APPLICATION_SAVE_TO_FILES);
	Bind(wxEVT_UPDATE_UI, &ibFrontendMainFrameStudio::OnUpdateSaveMetadata, this, wxID_STUDIO_APPLICATION_ROLLBACK_DATABASE);
	Bind(wxEVT_UPDATE_UI, &ibFrontendMainFrameStudio::OnUpdateSaveMetadata, this, wxID_STUDIO_APPLICATION_UPDATE_DATABASE);

	Bind(wxEVT_MENU, &ibFrontendMainFrameStudio::OnStartDebug, this, wxID_STUDIO_DEBUG_START);
	Bind(wxEVT_MENU, &ibFrontendMainFrameStudio::OnStartDebugWithoutDebug, this, wxID_STUDIO_DEBUG_START_WITHOUT_DEBUGGING);
	Bind(wxEVT_MENU, &ibFrontendMainFrameStudio::OnStartDebugWeb, this, wxID_STUDIO_DEBUG_START_WEB);
	Bind(wxEVT_MENU, &ibFrontendMainFrameStudio::OnStartDebugWithoutDebugWeb, this, wxID_STUDIO_DEBUG_START_WITHOUT_DEBUGGING_WEB);
	Bind(wxEVT_MENU, &ibFrontendMainFrameStudio::OnAttachForDebugging, this, wxID_STUDIO_DEBUG_ATTACH_FOR_DEBUGGING);

	Bind(wxEVT_MENU, &ibFrontendMainFrameStudio::OnRunDebugCommand, this, wxID_STUDIO_DEBUG_EDIT_POINT, wxID_STUDIO_DEBUG_REMOVE_ALL_DEBUGPOINTS);
	Bind(wxEVT_UPDATE_UI, &ibFrontendMainFrameStudio::OnUpdateDebugCommand, this, wxID_STUDIO_DEBUG_STEP_OVER, wxID_STUDIO_DEBUG_NEXT_POINT);
	Bind(wxEVT_MENU, &ibFrontendMainFrameStudio::OnRunDebugCommand, this, wxID_STUDIO_DEBUG_PROFILE);
	Bind(wxEVT_UPDATE_UI, &ibFrontendMainFrameStudio::OnUpdateDebugCommand, this, wxID_STUDIO_DEBUG_PROFILE);
	Bind(wxEVT_MENU, &ibFrontendMainFrameStudio::OnConnection, this, wxID_APPLICATION_CONNECTION);

	// Syntax helper — lambda bindings, no member fn to lose to the
	// studio header.
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { ToggleHelpPane(); },
	     wxID_FRONTEND_SYNTAX_HELPER);
	// About — the server's schema, asked for and shown as its menu item would be.
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { OpenSchema(ibProtocol::ibSchema::ibKind::About); },
	     wxID_STUDIO_ABOUT);
}
