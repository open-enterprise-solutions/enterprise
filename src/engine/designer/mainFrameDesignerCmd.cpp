////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"
#include "theme/statusbar.h"
#include "frontend/metatree/metatreeWnd.h"

#include "frontend/artProvider/artProvider.h"

void wxAuiDocDesignerMDIFrame::CreateWideGui()
{
	m_mainFrameToolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_mainFrameToolbar->SetToolBitmapSize(wxSize(16, 16));
	m_mainFrameToolbar->AddTool(wxID_NEW, _("New"), wxArtProvider::GetBitmapBundle(wxART_NEW, wxART_FRAME_ICON, wxSize(16, 16)), "New", wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddTool(wxID_OPEN, _("Open"), wxArtProvider::GetBitmapBundle(wxART_FILE_OPEN, wxART_FRAME_ICON, wxSize(16, 16)), "Open", wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddTool(wxID_SAVE, _("Save"), wxArtProvider::GetBitmapBundle(wxART_FILE_SAVE, wxART_FRAME_ICON, wxSize(16, 16)), "Save", wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddTool(wxID_SAVEAS, _("Save as"), wxArtProvider::GetBitmapBundle(wxART_FILE_SAVE_AS, wxART_FRAME_ICON, wxSize(16, 16)), "Save as", wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddSeparator();
	m_mainFrameToolbar->AddTool(wxID_FIND, _("Find"), wxArtProvider::GetBitmapBundle(wxART_FIND, wxART_FRAME_ICON, wxSize(16, 16)), "Find", wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddSeparator();
	m_mainFrameToolbar->AddTool(wxID_REDO, _("Redo"), wxArtProvider::GetBitmapBundle(wxART_REDO, wxART_FRAME_ICON, wxSize(16, 16)), "Redo", wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddTool(wxID_UNDO, _("Undo"), wxArtProvider::GetBitmapBundle(wxART_UNDO, wxART_FRAME_ICON, wxSize(16, 16)), "Undo", wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddSeparator();
	m_mainFrameToolbar->AddTool(wxID_DESIGNER_UPDATE_METADATA, _("Save metadata"), wxArtProvider::GetBitmapBundle(wxART_SAVE_METADATA, wxART_METATREE, wxSize(16, 16)), _("Save metadata"), wxItemKind::wxITEM_NORMAL);
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
		new COutputWindow(this, wxID_ANY), _("Messages"), false, wxArtProvider::GetBitmapBundle(wxART_TIP, wxART_FRAME_ICON, wxSize(16, 16))
	);	
	m_stackWindow = auiNotebook->AddPage(
		new CStackWindow(this, wxID_ANY), _("Call stack"), false, wxArtProvider::GetBitmapBundle(wxART_REPORT_VIEW, wxART_FRAME_ICON, wxSize(16, 16))
	);	
	m_watchWindow = auiNotebook->AddPage(
		new CWatchWindow(this, wxID_ANY), _("Watch"), false, wxArtProvider::GetBitmapBundle(wxART_LIST_VIEW, wxART_FRAME_ICON, wxSize(16, 16))
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

#include "frontend/help/helpPaneView.h"
#include "frontend/help/helpChooserDialog.h"
#include "frontend/win/editor/codeEditor/codeEditor.h"
#include "backend/appData.h"
#include "backend/help/helpCorpus.h"
#include "backend/help/helpResolver.h"
#include "backend/help/helpEntry.h"

void wxAuiDocDesignerMDIFrame::EnsureHelpPane()
{
	if (m_mgr.GetPane(wxAUI_PANE_HELP).IsOk()) return;

	m_helpPane = new ibHelpPaneView(this);

	wxAuiPaneInfo paneInfo;
	paneInfo.Name(wxAUI_PANE_HELP);
	paneInfo.Caption(_("Синтаксис-помічник"));
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

void wxAuiDocDesignerMDIFrame::ToggleHelpPane()
{
	EnsureHelpPane();
	wxAuiPaneInfo& pane = m_mgr.GetPane(wxAUI_PANE_HELP);
	if (!pane.IsOk()) return;
	pane.Show(!pane.IsShown());
	m_mgr.Update();
}

void wxAuiDocDesignerMDIFrame::OpenHelpForCursor()
{
	EnsureHelpPane();
	wxAuiPaneInfo& pane = m_mgr.GetPane(wxAUI_PANE_HELP);
	if (pane.IsOk() && !pane.IsShown()) {
		pane.Show(true);
		m_mgr.Update();
	}

	// Identifier-under-cursor extraction is editor-dependent. Phase 3
	// ships a minimal hook: resolve the active text-control's currently
	// selected word (Scintilla's GetSelectedText falls back to the
	// word at the caret when the selection is empty). If no editor is
	// active or the resolver returns zero hits, leave the pane open
	// without changing its current detail view.
	wxString identifier;
	wxWindow* focus = wxWindow::FindFocus();
	if (auto* edit = wxDynamicCast(focus, ibCodeEditor)) {
		identifier = edit->GetSelectedText();
		if (identifier.IsEmpty()) {
			const int pos = edit->GetCurrentPos();
			const int start = edit->WordStartPosition(pos, true);
			const int end   = edit->WordEndPosition(pos,   true);
			if (end > start) identifier = edit->GetTextRange(start, end);
		}
	}
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
		// "Довідка" — open the help-on-the-helper entry. Phase 3 has
		// no such entry yet; fall back to a neutral first hit so the
		// button is functional. Phase 7 ships the dedicated topic.
		if (m_helpPane) m_helpPane->ShowEntry(hits.front()->id);
		return;
	}
	if (!dlg.GetSelectedId().IsEmpty() && m_helpPane)
		m_helpPane->ShowEntry(dlg.GetSelectedId());
}

void wxAuiDocDesignerMDIFrame::UpdateEditorOptions()
{
	for (auto doc : m_docManager->GetDocumentsVector())
		doc->UpdateAllViews();

	m_outputWindow->SetFontColorSettings(GetFontColorSettings());
}