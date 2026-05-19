////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"
#include "backend/metaData.h"

#include "frontend/artProvider/artProvider.h"

void ibFrontendDocMDIFrameDesigner::CreateWideGui()
{
	m_mainFrameToolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_mainFrameToolbar->SetToolBitmapSize(wxSize(16, 16));
	m_mainFrameToolbar->AddTool(wxID_NEW, _("New"), wxArtProvider::GetBitmapBundle(wxART_NEW, wxART_MENU, wxSize(16, 16)), _("New"), wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddTool(wxID_OPEN, _("Open"), wxArtProvider::GetBitmapBundle(wxART_FILE_OPEN, wxART_FRAME_ICON, wxSize(16, 16)), _("Open"), wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddTool(wxID_SAVE, _("Save"), wxArtProvider::GetBitmapBundle(wxART_FILE_SAVE, wxART_FRAME_ICON, wxSize(16, 16)), _("Save"), wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddTool(wxID_SAVEAS, _("Save as"), wxArtProvider::GetBitmapBundle(wxART_FILE_SAVE_AS, wxART_FRAME_ICON, wxSize(16, 16)), _("Save as"), wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddSeparator();
	m_mainFrameToolbar->AddTool(wxID_FIND, _("Find"), wxArtProvider::GetBitmapBundle(wxART_FIND, wxART_FRAME_ICON, wxSize(16, 16)), _("Find"), wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddSeparator();
	m_mainFrameToolbar->AddTool(wxID_REDO, _("Redo"), wxArtProvider::GetBitmapBundle(wxART_REDO, wxART_FRAME_ICON, wxSize(16, 16)), _("Redo"), wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddTool(wxID_UNDO, _("Undo"), wxArtProvider::GetBitmapBundle(wxART_UNDO, wxART_FRAME_ICON, wxSize(16, 16)), _("Undo"), wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddSeparator();

	m_mainFrameToolbar->AddTool(wxID_DESIGNER_CONFIGURATION_UPDATE_DATABASE, _("Update database"), wxArtProvider::GetBitmapBundle(wxART_DATABASE_APPLY, wxART_FRONTEND, wxSize(16, 16)), _("Update database"), wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->Realize();

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

	SetStatusBar(new ibDocBottomStatusBar(this));
	SetStatusText(_("Ready"));
	GetNotebook()->GetAuiManager().GetArtProvider()->SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, wxAUI_DEFAULT_COLOUR);
	SetMinSize(wxSize(400, 380));

	// tell the manager to "commit" all the changes just made
	m_mgr.Update();
}

#include "frontend/win/ctrls/floatingNotebook.h"
#include "frontend/win/theme/luna_tabart.h"

void ibFrontendDocMDIFrameDesigner::CreateBottomPane()
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

void ibFrontendDocMDIFrameDesigner::CreateMetadataPane()
{
	if (m_mgr.GetPane(wxAUI_PANE_METADATA).IsOk())
		return;

	m_metaWindow = new ibMetadataTree(this, wxID_ANY);

	wxAuiPaneInfo paneInfo;
	paneInfo.Name(wxAUI_PANE_METADATA);
	paneInfo.CloseButton(false);
	paneInfo.MinimizeButton(false);
	paneInfo.MaximizeButton(false);
	paneInfo.Caption(_("Configuration"));
	paneInfo.MinSize(250, 0);

	m_mgr.AddPane(m_metaWindow, paneInfo);
}

void ibFrontendDocMDIFrameDesigner::UpdateEditorOptions()
{
	for (auto& doc : m_docManager->GetDocumentsVector())
		doc->UpdateAllViews();

	m_outputWindow->SetFontColorSettings(GetFontColorSettings());
}

#include "frontend/help/helpPaneView.h"
#include "frontend/help/helpChooserDialog.h"
#include "frontend/win/editor/codeEditor/codeEditor.h"
#include "backend/appData.h"
#include "backend/help/helpCorpus.h"
#include "backend/help/helpResolver.h"
#include "backend/help/helpEntry.h"

void ibFrontendDocMDIFrameDesigner::EnsureHelpPane()
{
	if (m_mgr.GetPane(wxAUI_PANE_HELP).IsOk()) return;

	m_helpPane = new ibHelpPaneView(this);

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

void ibFrontendDocMDIFrameDesigner::ToggleHelpPane()
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

void ibFrontendDocMDIFrameDesigner::OpenHelpForCursor()
{
	EnsureHelpPane();
	wxAuiPaneInfo& pane = m_mgr.GetPane(wxAUI_PANE_HELP);
	if (pane.IsOk() && !pane.IsShown()) {
		pane.Show(true);
		m_mgr.Update();
	}

	wxString identifier;
	wxWindow* focus = wxWindow::FindFocus();
	if (auto* edit = wxDynamicCast(focus, ibCodeEditor))
		identifier = edit->GetIdentifierUnderCursor();
	if (identifier.IsEmpty()) return;

	auto corpus = appData->GetHelpCorpus();
	if (!corpus) return;
	std::vector<const ibHelpEntry*> hits =
	    ResolveByName(*corpus, identifier);
	if (hits.empty()) return;

	if (hits.size() == 1) {
		if (m_helpPane) m_helpPane->ShowEntry(hits.front()->id);
		return;
	}

	ibHelpChooserDialog dlg(this, hits);
	if (dlg.ShowModal() != wxID_OK) return;
	if (dlg.HelpRequested()) {
		// "Help on the syntax helper" button — open the well-known
		// guide entry if it exists in the corpus; otherwise close
		// silently (showing an arbitrary candidate would mislead the
		// user). The entry id is reserved in docs/syntax-helper-design.md
		// and added when the on-helper guide ships.
		static const wxString kGuideId = wxT("guide.syntaxHelper");
		if (corpus->FindById(kGuideId) && m_helpPane)
			m_helpPane->ShowEntry(kGuideId);
		return;
	}
	if (!dlg.GetSelectedId().IsEmpty() && m_helpPane)
		m_helpPane->ShowEntry(dlg.GetSelectedId());
}