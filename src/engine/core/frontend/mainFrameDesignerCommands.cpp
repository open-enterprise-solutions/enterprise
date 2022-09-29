////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"
#include "theme/statusbar.h"
#include "frontend/metatree/metatreeWnd.h"

#include <wx/artprov.h>

#include "core/resources/saveMetadata.xpm"

void CMainFrameDesigner::CreateWideGui()
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

	m_toolbarDefault->AddSeparator();
	m_toolbarDefault->AddTool(wxID_DESIGNER_UPDATE_METADATA, _("Save metadata"), wxBitmap(s_saveMetadata_xpm), _("Save metadata"), wxItemKind::wxITEM_NORMAL);

	m_toolbarDefault->Realize();

	m_toolbarDefault->Connect(wxEVT_MENU, wxEventHandler(CMainFrameDesigner::OnToolbarClicked), NULL, this);

	wxAuiPaneInfo paneInfoDefault;
	paneInfoDefault.Name(wxT("mainTool"));
	paneInfoDefault.Caption(wxT("Default"));
	paneInfoDefault.ToolbarPane();
	paneInfoDefault.Top();
	paneInfoDefault.Row(1);
	paneInfoDefault.Position(1);
	paneInfoDefault.CloseButton(false);
	paneInfoDefault.DestroyOnClose(false);

	m_mgr.AddPane(m_toolbarDefault, paneInfoDefault);

	m_toolbarAdditional = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_toolbarAdditional->SetToolBitmapSize(wxSize(16, 16));

	wxAuiPaneInfo paneInfoAdditional;
	paneInfoAdditional.Name(wxT("additional"));
	paneInfoAdditional.Caption(wxT("Additional"));
	paneInfoAdditional.ToolbarPane();
	paneInfoAdditional.Top();
	paneInfoAdditional.Row(1);
	paneInfoAdditional.Position(1);
	paneInfoAdditional.CloseButton(false);
	paneInfoAdditional.DestroyOnClose(false);
	paneInfoAdditional.Hide();

	m_mgr.AddPane(m_toolbarAdditional, paneInfoAdditional);

	CreateObjectTree();
	CreatePropertyManager();
	CreateMessageAndDebugBar();

	SetStatusBar(new CBottomStatusBar(this));
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

void CMainFrameDesigner::CreateMessageAndDebugBar()
{
	wxAuiNotebook *m_ctrl_notebook = new wxAuiNotebook(this, wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize,
		wxAUI_NB_BOTTOM | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_TAB_EXTERNAL_MOVE);

	m_ctrl_notebook->SetArtProvider(new CLunaTabArt());

	m_ctrl_notebook->AddPage(outputWindow, _("Messages"), true, wxArtProvider::GetBitmap(wxART_TIP, wxART_FRAME_ICON, wxSize(16, 16)));
	m_ctrl_notebook->AddPage(stackWindow, _("Call stack"), false, wxArtProvider::GetBitmap(wxART_REPORT_VIEW, wxART_FRAME_ICON, wxSize(16, 16)));
	m_ctrl_notebook->AddPage(watchWindow, _("Watch"), false, wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxART_FRAME_ICON, wxSize(16, 16)));

	wxAuiPaneInfo paneInfo;
	paneInfo.Name("Messages");
	paneInfo.Bottom();
	paneInfo.PinButton(true);
	paneInfo.CloseButton(false);

	paneInfo.MinSize(0, 150);

	m_mgr.AddPane(m_ctrl_notebook, paneInfo);
}

void CMainFrameDesigner::CreateObjectTree()
{
	m_metadataTree = new CMetadataTree(this, wxID_ANY);

	wxAuiPaneInfo paneInfo;
	paneInfo.Name("metadata");
	paneInfo.CloseButton(false);
	paneInfo.MinimizeButton(false);
	paneInfo.MaximizeButton(false);
	paneInfo.Caption(_("Configuration"));
	paneInfo.MinSize(300, 0);

	m_mgr.AddPane(m_metadataTree, paneInfo);
}

#include "frontend/objinspect/objinspect.h"

void CMainFrameDesigner::CreatePropertyManager()
{
	wxAuiPaneInfo paneInfo;
	paneInfo.Name("mainProperty");
	paneInfo.CloseButton(true);
	paneInfo.MinimizeButton(false);
	paneInfo.MaximizeButton(false);
	paneInfo.DestroyOnClose(false);
	paneInfo.Caption(_("Property"));
	paneInfo.MinSize(300, 0);
	paneInfo.Right();

	paneInfo.Show(false);

	m_mgr.AddPane(objectInspector, paneInfo);
}

#include "frontend/output/outputWindow.h"

void CMainFrameDesigner::UpdateEditorOptions()
{
	for (auto document : m_docManager->GetDocumentsVector()) {
		document->UpdateAllViews();
	}

	outputWindow->SetFontColorSettings(GetFontColorSettings());
}