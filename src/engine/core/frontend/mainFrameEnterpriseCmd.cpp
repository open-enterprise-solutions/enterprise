////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameEnterprise.h"
#include "theme/statusbar.h"

#include <wx/artprov.h>

#include "frontend/theme/luna_auitoolbar.h"

void wxAuiDocEnterpriseMDIFrame::CreateWideGui()
{
	m_mainFrameToolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_mainFrameToolbar->SetToolBitmapSize(wxSize(16, 16));

	m_mainFrameToolbar->AddTool(wxID_NEW, _("New"), wxArtProvider::GetBitmap(wxART_NEW, wxART_FRAME_ICON, wxSize(16, 16)), "New", wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddTool(wxID_OPEN, _("Open"), wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_FRAME_ICON, wxSize(16, 16)), "Open", wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddTool(wxID_SAVE, _("Save"), wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_FRAME_ICON, wxSize(16, 16)), "Save", wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddTool(wxID_SAVEAS, _("Save as"), wxArtProvider::GetBitmap(wxART_FILE_SAVE_AS, wxART_FRAME_ICON, wxSize(16, 16)), "Save as", wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddSeparator();
	m_mainFrameToolbar->AddTool(wxID_FIND, _("Find"), wxArtProvider::GetBitmap(wxART_FIND, wxART_FRAME_ICON, wxSize(16, 16)), "Find", wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddSeparator();
	m_mainFrameToolbar->AddTool(wxID_REDO, _("Redo"), wxArtProvider::GetBitmap(wxART_REDO, wxART_FRAME_ICON, wxSize(16, 16)), "Redo", wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddTool(wxID_UNDO, _("Undo"), wxArtProvider::GetBitmap(wxART_UNDO, wxART_FRAME_ICON, wxSize(16, 16)), "Undo", wxItemKind::wxITEM_NORMAL);

	m_mainFrameToolbar->Realize();

	wxAuiPaneInfo m_infoDefault;
	m_infoDefault.Name(wxT("mainTool"));
	m_infoDefault.Caption(wxT("Default"));
	m_infoDefault.ToolbarPane();
	m_infoDefault.Top();
	m_infoDefault.Row(1);
	m_infoDefault.Position(1);
	m_infoDefault.CloseButton(false);
	m_infoDefault.DestroyOnClose(false);

	m_mgr.AddPane(m_mainFrameToolbar, m_infoDefault);

	m_docToolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_docToolbar->SetToolBitmapSize(wxSize(16, 16));

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

	m_mgr.AddPane(m_docToolbar, m_infoAdditional);

	CreatePropertyPane();
	CreateBottomPane();

	InitializeDefaultMenu();

	SetStatusBar(new CBottomStatusBar(this));
	SetStatusText(_("Ready"));
	GetNotebook()->GetAuiManager().GetArtProvider()->SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, DEFAULT_COLOUR);
	SetMinSize(wxSize(700, 380));

	// tell the manager to "commit" all the changes just made
	m_mgr.Update();
}

#include "controls/floatingNotebook.h"
#include "theme/luna_auitabart.h"

void wxAuiDocEnterpriseMDIFrame::CreateBottomPane()
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

	wxFloatingNotebook* auiNotebook = new wxFloatingNotebook(&m_mgr, paneInfo.name,
		wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize,
		wxAUI_NB_BOTTOM | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS);

	auiNotebook->SetArtProvider(new CLunaTabArt());
	auiNotebook->Freeze();

	m_outputWindow = auiNotebook->AddPage(
		new COutputWindow(this, wxID_ANY), _("Messages"), false, wxArtProvider::GetBitmap(wxART_TIP, wxART_FRAME_ICON, wxSize(16, 16))
	);

	auiNotebook->SetNullSelection();
	auiNotebook->Thaw();

	m_mgr.AddPane(auiNotebook, paneInfo);
}