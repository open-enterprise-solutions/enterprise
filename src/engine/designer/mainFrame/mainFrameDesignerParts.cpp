////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"
#include "backend/metaData.h"
#include "backend/appData.h"
#include "backend/plugin/pluginManager.h"

#include "frontend/artProvider/artProvider.h"
#include "frontend/sigma/sigmaPane.h"

#include <wx/xml/xml.h>

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
	wxLogMessage(wxT("[help-host] EnsureHelpPane enter; pane.IsOk()=%d, m_helpPane=%p"),
	             m_mgr.GetPane(wxAUI_PANE_HELP).IsOk() ? 1 : 0,
	             static_cast<void*>(m_helpPane));
	if (m_mgr.GetPane(wxAUI_PANE_HELP).IsOk()) return;

	m_helpPane = new ibHelpPaneView(this);
	wxLogMessage(wxT("[help-host] created m_helpPane=%p"),
	             static_cast<void*>(m_helpPane));

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

	// Apply persisted state from options.xml (last entry id / active
	// tab / detail font boost) now that the pane exists.
	if (m_pendingHelpState.has && m_helpPane) {
		// Construct a tiny throwaway XML wrapper so we can reuse the
		// pane's LoadStateFromXml entry point — keeps the schema knowledge
		// in one place.
		wxXmlNode wrap(wxXML_ELEMENT_NODE, wxT("options"));
		auto* hp = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("helpPane"));
		hp->AddAttribute(wxT("currentId"), m_pendingHelpState.currentId);
		hp->AddAttribute(wxT("tab"),       wxString::Format(wxT("%ld"), m_pendingHelpState.tab));
		hp->AddAttribute(wxT("fontBoost"), wxString::Format(wxT("%ld"), m_pendingHelpState.fontBoost));
		wrap.AddChild(hp);
		m_helpPane->LoadStateFromXml(&wrap);
		m_pendingHelpState.has = false;  // applied; don't reapply
	}
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
	wxLogMessage(wxT("[help-host] OpenHelpForCursor enter"));
	EnsureHelpPane();
	wxAuiPaneInfo& pane = m_mgr.GetPane(wxAUI_PANE_HELP);
	wxLogMessage(wxT("[help-host] pane.IsOk=%d IsShown=%d"),
	             pane.IsOk() ? 1 : 0, pane.IsShown() ? 1 : 0);
	if (pane.IsOk() && !pane.IsShown()) {
		pane.Show(true);
		m_mgr.Update();
	}

	wxString identifier;
	wxWindow* focus = wxWindow::FindFocus();
	if (auto* edit = wxDynamicCast(focus, ibCodeEditor))
		identifier = edit->GetIdentifierUnderCursor();
	wxLogMessage(wxT("[help-host] identifier='%s' focus=%p"),
	             identifier, static_cast<void*>(focus));
	if (identifier.IsEmpty()) return;

	auto corpus = appData->GetHelpCorpus();
	wxLogMessage(wxT("[help-host] corpus=%p"),
	             static_cast<const void*>(corpus.get()));
	if (!corpus) return;
	std::vector<const ibHelpEntry*> hits =
	    ResolveByName(*corpus, identifier);
	wxLogMessage(wxT("[help-host] hits=%zu"), hits.size());
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

// ---------------------------------------------------------------------------
// Sigma AI pane — Phase 1 wiring. Designer registers callbacks with the
// plugin manager so RegisterWebPane / WebPaneSend / WebPaneShow trampolines
// in pluginManager.cpp can construct + drive concrete wxAuiPane instances.
//
// Provider plugins (pugi-oes-bridge / future Anthropic / OpenAI / Ollama
// bridges) call host->RegisterWebPane("<id>", "<title>", "<html>", cb, ud)
// from oes_plugin_initialize; the lambdas below run on the UI thread and
// add the resulting ibSigmaPane to wxAuiManager.
// ---------------------------------------------------------------------------

void ibFrontendDocMDIFrameDesigner::WireSigmaCallbacks()
{
	if (m_sigmaCallbacksRegistered) return;
	auto* pm = appData->GetPluginManager();
	if (pm == nullptr) return;

	pm->SetWebPaneCallbacks(
	    // RegisterWebPane — wraps an ibSigmaPane inside a wxAuiPane and
	    // shows it on the right dock by default.
	    [this](const wxString& paneId, const wxString& title,
	            const wxString& htmlBundlePath,
	            ibPluginWebMsgFn onMessage, void* userData) -> int {
	        if (m_sigmaPanes.count(paneId)) return -1;
	        auto* pane = new ibSigmaPane(this, paneId, title,
	                                      htmlBundlePath, onMessage, userData);
	        wxAuiPaneInfo info;
	        info.Name(paneId);
	        info.Caption(title);
	        info.Right();
	        info.MinSize(360, 480);
	        info.BestSize(420, 600);
	        info.CloseButton(true);
	        info.MaximizeButton(false);
	        // Replay persisted visibility from options.xml. Default
	        // hidden so a brand-new plugin install doesn't pop the pane
	        // on first launch — the user opens it via Tools menu or
	        // Ctrl+Alt+I, and that choice persists for next session.
	        auto pit = m_pendingSigmaVisible.find(paneId);
	        const bool wantVisible = (pit != m_pendingSigmaVisible.end()) && pit->second;
	        info.Show(wantVisible);
	        m_mgr.AddPane(pane, info);
	        m_mgr.Update();
	        m_sigmaPanes[paneId] = pane;
	        return 0;
	    },
	    // WebPaneSend — thread-safe push to the WebView; ibSigmaPane
	    // handles the wxThreadEvent marshal for off-UI callers.
	    [this](const wxString& paneId, const wxString& jsonInline) -> int {
	        auto it = m_sigmaPanes.find(paneId);
	        if (it == m_sigmaPanes.end()) return -1;
	        it->second->PushMessage(jsonInline);
	        return 0;
	    },
	    // WebPaneShow — force-visible flip; creates the AUI pane on
	    // first call if the registration step skipped Show(true).
	    [this](const wxString& paneId) -> int {
	        auto it = m_sigmaPanes.find(paneId);
	        if (it == m_sigmaPanes.end()) return -1;
	        wxAuiPaneInfo& info = m_mgr.GetPane(paneId);
	        if (!info.IsOk()) return -1;
	        if (!info.IsShown()) info.Show(true);
	        m_mgr.Update();
	        return 0;
	    });

	m_sigmaCallbacksRegistered = true;
}