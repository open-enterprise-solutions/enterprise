////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"
#include "theme/statusbar.h"
#include "frontend/metatree/metatreeWnd.h"

#include "core/art/artProvider.h"

void wxAuiDocDesignerMDIFrame::CreateWideGui()
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
	m_mainFrameToolbar->AddSeparator();
	m_mainFrameToolbar->AddTool(wxID_DESIGNER_UPDATE_METADATA, _("Save metadata"), wxArtProvider::GetBitmap(wxART_SAVE_METADATA, wxART_METATREE, wxSize(16, 16)), _("Save metadata"), wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->Realize();
	m_mainFrameToolbar->Connect(wxEVT_MENU, wxEventHandler(wxAuiDocDesignerMDIFrame::OnToolbarClicked), NULL, this);

	wxAuiPaneInfo paneInfoMainTool;
	paneInfoMainTool.Name(wxT("mainTool"));
	paneInfoMainTool.Caption(wxT("Default"));
	paneInfoMainTool.ToolbarPane();
	paneInfoMainTool.Top();
	paneInfoMainTool.Row(1);
	paneInfoMainTool.Position(1);
	paneInfoMainTool.CloseButton(false);
	paneInfoMainTool.DestroyOnClose(false);
	m_mgr.AddPane(m_mainFrameToolbar, paneInfoMainTool);

	m_docToolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_docToolbar->SetToolBitmapSize(wxSize(16, 16));

	wxAuiPaneInfo paneInfoDocTool;
	paneInfoDocTool.Name(wxT("docTool"));
	paneInfoDocTool.Caption(wxT("Additional"));
	paneInfoDocTool.ToolbarPane();
	paneInfoDocTool.Top();
	paneInfoDocTool.Row(1);
	paneInfoDocTool.Position(1);
	paneInfoDocTool.CloseButton(false);
	paneInfoDocTool.DestroyOnClose(false);
	paneInfoDocTool.Hide();
	m_mgr.AddPane(m_docToolbar, paneInfoDocTool);

	CreateMetadataPane();
	CreatePropertyPane();
	CreateBottomPane();

	InitializeDefaultMenu();

	SetStatusBar(new CBottomStatusBar(this));
	SetStatusText("Ready");
	GetNotebook()->GetAuiManager().GetArtProvider()->SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, DEFAULT_COLOUR);
	SetMinSize(wxSize(400, 380));

	// tell the manager to "commit" all the changes just made
	m_mgr.Update();
}

#include "controls/floatingNotebook.h"
#include "theme/luna_auitabart.h"

void wxAuiDocDesignerMDIFrame::CreateBottomPane()
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
	m_stackWindow = auiNotebook->AddPage(
		new CStackWindow(this, wxID_ANY), _("Call stack"), false, wxArtProvider::GetBitmap(wxART_REPORT_VIEW, wxART_FRAME_ICON, wxSize(16, 16))
	);	
	m_watchWindow = auiNotebook->AddPage(
		new CWatchWindow(this, wxID_ANY), _("Watch"), false, wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxART_FRAME_ICON, wxSize(16, 16))
	);
	
	auiNotebook->SetNullSelection();
	auiNotebook->Thaw();

	m_mgr.AddPane(auiNotebook, paneInfo);
}

void wxAuiDocDesignerMDIFrame::CreateMetadataPane()
{
	if (m_mgr.GetPane(wxAUI_PANE_METADATA).IsOk())
		return;

	m_metadataTree = new CMetadataTree(this, wxID_ANY);

	wxAuiPaneInfo paneInfo;
	paneInfo.Name(wxAUI_PANE_METADATA);
	paneInfo.CloseButton(false);
	paneInfo.MinimizeButton(false);
	paneInfo.MaximizeButton(false);
	paneInfo.Caption(_("Configuration"));
	paneInfo.MinSize(300, 0);

	m_mgr.AddPane(m_metadataTree, paneInfo);
}

void wxAuiDocDesignerMDIFrame::UpdateEditorOptions()
{
	for (auto doc : m_docManager->GetDocumentsVector())
		doc->UpdateAllViews();

	m_outputWindow->SetFontColorSettings(GetFontColorSettings());
}