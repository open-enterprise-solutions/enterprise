////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the designer's panes
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"

#include "designer-thin/mainFrame/metaTree/treeConfiguration.h"

void ibFrontendMainFrameDesigner::CreateMetadataPane()
{
	if (m_mgr.GetPane(wxAUI_PANE_METADATA).IsOk())
		return;

	m_metaWindow = new ibConfigurationTree(this);

	wxAuiPaneInfo paneInfo;
	paneInfo.Name(wxAUI_PANE_METADATA);
	paneInfo.CloseButton(false);
	paneInfo.MinimizeButton(false);
	paneInfo.MaximizeButton(false);
	paneInfo.Caption(_("Configuration"));
	paneInfo.MinSize(250, 0);

	m_mgr.AddPane(m_metaWindow, paneInfo);
}

#include "frmclient/artProvider/artProvider.h"
#include "frmclient/win/ctrls/floatingNotebook.h"
#include "frmclient/win/theme/luna_tabart.h"
#include "frmclient/mainFrame/output/outputWindow.h"
#include "designer-thin/mainFrame/local/localWindow.h"
#include "designer-thin/mainFrame/stack/stackWindow.h"
#include "designer-thin/mainFrame/watch/watchWindow.h"

void ibFrontendMainFrameDesigner::CreateBottomPane()
{
	if (m_mgr.GetPane(wxAUI_PANE_BOTTOM).IsOk())
		return;

	wxAuiPaneInfo paneInfo;
	paneInfo.Name(wxAUI_PANE_BOTTOM);
	paneInfo.Bottom();
	paneInfo.PinButton(false);
	paneInfo.CloseButton(false);
	paneInfo.Resizable(false);
	paneInfo.Movable(false);
	paneInfo.MinSize(-1, 30);

	ibFloatingNotebook* auiNotebook = new ibFloatingNotebook(&m_mgr, paneInfo.name,
		wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize,
		wxAUI_NB_BOTTOM | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS);

	auiNotebook->SetArtProvider(new wxAuiLunaTabArt());
	auiNotebook->Freeze();

	auiNotebook->AddPage(m_outputWindow, _("Messages"), false, wxArtProvider::GetBitmapBundle(wxART_MESSAGE, wxART_SERVICE, wxSize(16, 16)));
	auiNotebook->AddPage(m_localWindow, _("Local variable"), false, wxArtProvider::GetBitmapBundle(wxART_LOCAL_VARIABLE, wxART_SERVICE, wxSize(16, 16)));
	auiNotebook->AddPage(m_stackWindow, _("Call stack"), false, wxArtProvider::GetBitmapBundle(wxART_STACK, wxART_SERVICE, wxSize(16, 16)));
	auiNotebook->AddPage(m_watchWindow, _("Watch"), false, wxArtProvider::GetBitmapBundle(wxART_WATCH, wxART_SERVICE, wxSize(16, 16)));

	auiNotebook->Refresh();
	auiNotebook->SetNullSelection();
	auiNotebook->Thaw();

	m_mgr.AddPane(auiNotebook, paneInfo);
}

// ⭐ THE BUTTONS ARE THE MENU'S COMMANDS, NOT COPIES OF THEM. Each carries the id of its Debug-menu item, so the
// press lands in the same handler (OnStartDebug, OnRunDebugCommand...) and the button is lit or dimmed by the same
// OnUpdateDebugCommand that lights the menu item - the frame binds those to the ids, and the toolbar's events climb to
// the frame. Nothing here decides what a command does or when it is available.
//
// What is left out on purpose: the web-client starts (they are a submenu of the two starts, and a button per
// client would double the row) - they stay in the menu.
void ibFrontendMainFrameDesigner::CreateDebugToolbar()
{
	m_debugToolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_debugToolbar->SetToolBitmapSize(wxSize(16, 16));

	const auto add = [this](int id, const wxArtID& art, const wxString& label) {
		m_debugToolbar->AddTool(id, label,
			wxArtProvider::GetBitmapBundle(art, wxART_DEBUG, wxSize(16, 16)), label, wxItemKind::wxITEM_NORMAL);
	};

	add(wxID_DESIGNER_DEBUG_START, wxART_DEBUG_START, _("Start debugging"));
	add(wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING, wxART_DEBUG_START_WITHOUT_DEBUGGING, _("Start without debugging"));
	add(wxID_DESIGNER_DEBUG_ATTACH_FOR_DEBUGGING, wxART_DEBUG_ATTACH, _("Attach for debugging..."));
	m_debugToolbar->AddSeparator();
	add(wxID_DESIGNER_DEBUG_NEXT_POINT, wxART_DEBUG_CONTINUE, _("Continue"));
	add(wxID_DESIGNER_DEBUG_PAUSE, wxART_DEBUG_PAUSE, _("Pause"));
	add(wxID_DESIGNER_DEBUG_STEP_INTO, wxART_DEBUG_STEP_INTO, _("Step into"));
	add(wxID_DESIGNER_DEBUG_STEP_OVER, wxART_DEBUG_STEP_OVER, _("Step over"));
	add(wxID_DESIGNER_DEBUG_STEP_OUT, wxART_DEBUG_STEP_OUT, _("Step out"));
	add(wxID_DESIGNER_DEBUG_STOP_DEBUGGING, wxART_DEBUG_STOP_DEBUGGING, _("Stop debugging"));
	add(wxID_DESIGNER_DEBUG_STOP_PROGRAM, wxART_DEBUG_STOP_PROGRAM, _("Stop debugging program"));
	m_debugToolbar->AddSeparator();
	add(wxID_DESIGNER_DEBUG_REMOVE_ALL_DEBUGPOINTS, wxART_DEBUG_REMOVE_ALL_BREAKPOINTS, _("Remove all breakpoints"));
	m_debugToolbar->Realize();

	wxAuiPaneInfo paneInfoDebugTool;
	paneInfoDebugTool.Name(wxT("debugTool"));
	paneInfoDebugTool.Caption(_("Debug"));
	paneInfoDebugTool.ToolbarPane();
	paneInfoDebugTool.Top();
	paneInfoDebugTool.Row(1);
	paneInfoDebugTool.Position(2);
	paneInfoDebugTool.CloseButton(false);
	paneInfoDebugTool.DestroyOnClose(false);
	m_mgr.AddPane(m_debugToolbar, paneInfoDebugTool);
}

// ---------------------------------------------------------------------------
// Syntax-helper sidebar — lazy AUI pane, the desktop's (designer/mainFrame/mainFrameDesignerParts.cpp). The corpus is
// the core's, read on this side: the pane holds it.
// ---------------------------------------------------------------------------

#include "designer-thin/syntaxHelper/helpPaneView.h"

void ibFrontendMainFrameDesigner::EnsureHelpPane()
{
	if (m_mgr.GetPane(wxAUI_PANE_HELP).IsOk()) return;

	// The corpus speaks the application's language — the desktop's process locale, here the server's (its About).
	ibProtocolNode params, answer;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::About));
	const wxString locale = Call(ibProtocolMethod::Schema, params, answer) ? answer.GetString(ibProtocolName::Locale) : wxString();

	m_helpPane = new ibHelpPaneView(this, locale);

	wxAuiPaneInfo paneInfo;
	paneInfo.Name(wxAUI_PANE_HELP);
	paneInfo.Caption(_("Syntax Helper"));
	paneInfo.Right();
	paneInfo.Layer(1);
	paneInfo.MinSize(320, 480);
	paneInfo.BestSize(360, 600);
	paneInfo.CloseButton(true);
	paneInfo.MaximizeButton(false);
	paneInfo.MinimizeButton(false);
	paneInfo.Show(true);

	m_mgr.AddPane(m_helpPane, paneInfo);
	m_mgr.Update();
}

void ibFrontendMainFrameDesigner::ToggleHelpPane()
{
	const bool firstCreate = !m_mgr.GetPane(wxAUI_PANE_HELP).IsOk();
	EnsureHelpPane();
	wxAuiPaneInfo& pane = m_mgr.GetPane(wxAUI_PANE_HELP);
	if (!pane.IsOk()) return;
	// EnsureHelpPane already adds the pane visible. On the first
	// invocation a naive "flip" would immediately hide it; only toggle
	// on subsequent invocations.
	if (!firstCreate) pane.Show(!pane.IsShown());
	m_mgr.Update();
}
