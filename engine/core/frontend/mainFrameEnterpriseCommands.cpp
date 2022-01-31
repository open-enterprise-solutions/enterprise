////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameEnterprise.h"
#include "theme/statusbar.h"

#include <wx/artprov.h>

void CMainFrameEnterprise::CreateWideGui()
{
	m_toolbarDefault = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_toolbarDefault->SetToolBitmapSize(wxSize(16, 16));

	m_toolbarDefault->AddTool(wxID_NEW, _("New"), wxArtProvider::GetBitmap(wxART_NEW, wxART_FRAME_ICON, wxSize(16, 16)), "New", wxItemKind::wxITEM_NORMAL);
	m_toolbarDefault->AddTool(wxID_OPEN, _("Open"), wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_FRAME_ICON, wxSize(16, 16)), "Open", wxItemKind::wxITEM_NORMAL);
	m_toolbarDefault->AddTool(wxID_SAVE, _("Save"), wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_FRAME_ICON, wxSize(16, 16)), "Save", wxItemKind::wxITEM_NORMAL);
	m_toolbarDefault->AddTool(wxID_SAVEAS, _("Save as"), wxArtProvider::GetBitmap(wxART_FILE_SAVE_AS, wxART_FRAME_ICON, wxSize(16, 16)), "Save as", wxItemKind::wxITEM_NORMAL);
	m_toolbarDefault->AddSeparator();
	m_toolbarDefault->AddTool(wxID_FIND, _("Find"), wxArtProvider::GetBitmap(wxART_FIND, wxART_FRAME_ICON, wxSize(16, 16)), "Find", wxItemKind::wxITEM_NORMAL);
	m_toolbarDefault->AddSeparator();
	m_toolbarDefault->AddTool(wxID_REDO, _("Redo"), wxArtProvider::GetBitmap(wxART_REDO, wxART_FRAME_ICON, wxSize(16, 16)), "Redo", wxItemKind::wxITEM_NORMAL);
	m_toolbarDefault->AddTool(wxID_UNDO, _("Undo"), wxArtProvider::GetBitmap(wxART_UNDO, wxART_FRAME_ICON, wxSize(16, 16)), "Undo", wxItemKind::wxITEM_NORMAL);

	m_toolbarDefault->Realize();

	wxAuiPaneInfo m_infoDefault;
	m_infoDefault.Name(wxT("mainTool"));
	m_infoDefault.Caption(wxT("Default"));
	m_infoDefault.ToolbarPane();
	m_infoDefault.Top();
	m_infoDefault.Row(1);
	m_infoDefault.Position(1);
	m_infoDefault.CloseButton(false);
	m_infoDefault.DestroyOnClose(false);

	m_mgr.AddPane(m_toolbarDefault, m_infoDefault);

	m_toolbarAdditional = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_toolbarAdditional->SetToolBitmapSize(wxSize(16, 16));

	wxAuiPaneInfo m_infoAdditional;
	m_infoAdditional.Name(wxT("additional"));
	m_infoAdditional.Caption(wxT("Additional"));
	m_infoAdditional.ToolbarPane();
	m_infoAdditional.Top();
	m_infoAdditional.Row(1);
	m_infoAdditional.Position(1);
	m_infoAdditional.CloseButton(false);
	m_infoAdditional.DestroyOnClose(false);
	m_infoAdditional.Hide();

	m_mgr.AddPane(m_toolbarAdditional, m_infoAdditional);

	CreatePropertyManager();
	CreateMessageAndDebugBar();

	SetStatusBar(new CStatusBar(this));
	SetStatusText("Ready");
	GetNotebook()->GetAuiManager().GetArtProvider()->SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, DEFAULT_COLOUR);
	SetMinSize(wxSize(700, 380));

	// tell the manager to "commit" all the changes just made
	m_mgr.Update();
}

#include "stack/stackWindow.h"
#include "output/outputWindow.h"
#include "watch/watchwindow.h"

#include "theme/luna_auitabart.h"

void CMainFrameEnterprise::CreateMessageAndDebugBar()
{
	wxAuiNotebook *m_ctrl_notebook = new wxAuiNotebook(this, wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize,
		wxAUI_NB_BOTTOM | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_TAB_EXTERNAL_MOVE);

	m_ctrl_notebook->SetArtProvider(new CLunaTabArt());

	m_ctrl_notebook->AddPage(outputWindow, _("Messages"), true, wxArtProvider::GetBitmap(wxART_TIP, wxART_FRAME_ICON, wxSize(16, 16)));

	wxAuiPaneInfo m_info;
	m_info.Name("Messages");
	m_info.Bottom();
	m_info.PinButton(true);
	m_info.CloseButton(false);

	m_info.MinSize(0, 150);

	m_mgr.AddPane(m_ctrl_notebook, m_info);
}

#include "frontend/objinspect/objinspect.h"

void CMainFrameEnterprise::CreatePropertyManager()
{
	wxAuiPaneInfo m_info;
	m_info.Name("mainProperty");
	m_info.CloseButton(true);
	m_info.MinimizeButton(false);
	m_info.MaximizeButton(false);
	m_info.DestroyOnClose(false);
	m_info.Caption("Property");
	m_info.MinSize(300, 0);
	m_info.Right();

	m_info.Show(false);

	m_mgr.AddPane(objectInspector, m_info);
}