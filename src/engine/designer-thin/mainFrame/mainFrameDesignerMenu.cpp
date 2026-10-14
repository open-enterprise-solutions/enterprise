////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the designer's menu and keys
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"

void ibFrontendMainFrameDesigner::SetDefaultHotKeys()
{
	ibFrontendMainFrame::SetDefaultHotKeys();

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

void ibFrontendMainFrameDesigner::InitializeDefaultMenu()
{
	m_menuHelp = new wxMenu;
	// Syntax helper pane toggle. RawCtrl forces the literal Control key on
	// every platform (wxWidgets maps "Ctrl" to Cmd on macOS).
	m_menuHelp->Append(wxID_FRONTEND_SYNTAX_HELPER,
	                   _("Syntax Helper\tRawCtrl+Alt+F1"));
	m_menuHelp->AppendSeparator();
	m_menuHelp->Append(wxID_DESIGNER_ABOUT, _("About"));

	ibFrontendMainFrame::InitializeDefaultMenu();

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
