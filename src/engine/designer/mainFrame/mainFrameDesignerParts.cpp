////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"
#include "backend/metaData.h"
#include "backend/appData.h"
#include "backend/plugin/pluginManager.h"

#include "frontend/artProvider/artProvider.h"
#include "frontend/pluginWebPane/pluginWebPane.h"

#include "projectSearchPanel.h"
#include "aiTodoPanel.h"
#include "aiMarkersPanel.h"

// Project Search pane name — designer-private, parallel to the existing
// wxAUI_PANE_HELP / wxAUI_PANE_METADATA constants in frontend/mainFrame.h.
// Living in the .cpp keeps the constant scoped to designer and avoids
// touching the shared frontend header for a designer-only widget.
#define wxAUI_PANE_PROJECT_SEARCH wxT("projectSearchWindow")
#define wxAUI_PANE_AI_TODO        wxT("aiTodoWindow")
#define wxAUI_PANE_AI_MARKERS     wxT("aiMarkersWindow")

#include <wx/xml/xml.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>

#include <algorithm>

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

	// MCP concurrency Layer 3: arm the external-mutation notifier for the
	// directory the launcher loaded. Empty in SERVER mode (no file path);
	// the notifier short-circuits gracefully. Best-effort — if appData is
	// somehow null at this point we just skip wiring; the notifier can
	// also be (re)armed later from configuration-swap paths.
	if (appData != nullptr) {
		const wxString& dir = appData->GetFileDirectory();
		if (!dir.IsEmpty()) {
			StartExternalMutationNotifier(dir);
		}
	}
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

void ibFrontendDocMDIFrameDesigner::EnsureProjectSearchPane()
{
	if (m_mgr.GetPane(wxAUI_PANE_PROJECT_SEARCH).IsOk()) return;

	m_projectSearchPanel = new ibProjectSearchPanel(this);

	wxAuiPaneInfo paneInfo;
	paneInfo.Name(wxAUI_PANE_PROJECT_SEARCH);
	paneInfo.Caption(_("Поиск по метаданным"));
	paneInfo.Left();
	paneInfo.Layer(1);
	paneInfo.MinSize(280, 240);
	paneInfo.BestSize(320, 480);
	paneInfo.CloseButton(true);
	paneInfo.MaximizeButton(false);
	paneInfo.MinimizeButton(false);
	paneInfo.Show(true);

	m_mgr.AddPane(m_projectSearchPanel, paneInfo);
	m_mgr.Update();
}

void ibFrontendDocMDIFrameDesigner::ToggleProjectSearchPane()
{
	const bool firstCreate = !m_mgr.GetPane(wxAUI_PANE_PROJECT_SEARCH).IsOk();
	EnsureProjectSearchPane();
	wxAuiPaneInfo& pane = m_mgr.GetPane(wxAUI_PANE_PROJECT_SEARCH);
	if (!pane.IsOk()) return;
	if (!firstCreate) pane.Show(!pane.IsShown());
	m_mgr.Update();
}

void ibFrontendDocMDIFrameDesigner::FocusProjectSearchPane()
{
	EnsureProjectSearchPane();
	wxAuiPaneInfo& pane = m_mgr.GetPane(wxAUI_PANE_PROJECT_SEARCH);
	if (pane.IsOk() && !pane.IsShown()) {
		pane.Show(true);
		m_mgr.Update();
	}
	if (m_projectSearchPanel != nullptr) {
		m_projectSearchPanel->FocusQueryInput();
	}
}

// ---------------------------------------------------------------------------
// AI Assistant TODO list pane (Workmate parity).
// Pane state is owned by the AUI manager once created; m_aiTodoPanel
// is the typed handle the toggle/focus helpers route through so we don't
// need a wxDynamicCast on every call. Lazy-create on first toggle to
// keep cold-start cost flat for users who never open the pane.
// ---------------------------------------------------------------------------

void ibFrontendDocMDIFrameDesigner::EnsureAiTodoPane()
{
	if (m_mgr.GetPane(wxAUI_PANE_AI_TODO).IsOk()) return;

	m_aiTodoPanel = new ibAiTodoPanel(this);

	wxAuiPaneInfo paneInfo;
	paneInfo.Name(wxAUI_PANE_AI_TODO);
	paneInfo.Caption(_("Задачи AI-ассистента"));
	paneInfo.Right();
	paneInfo.Layer(1);
	paneInfo.MinSize(360, 320);
	paneInfo.BestSize(420, 480);
	paneInfo.CloseButton(true);
	paneInfo.MaximizeButton(false);
	paneInfo.MinimizeButton(false);
	paneInfo.Show(true);

	m_mgr.AddPane(m_aiTodoPanel, paneInfo);
	m_mgr.Update();
}

void ibFrontendDocMDIFrameDesigner::ToggleAiTodoPane()
{
	const bool firstCreate = !m_mgr.GetPane(wxAUI_PANE_AI_TODO).IsOk();
	EnsureAiTodoPane();
	wxAuiPaneInfo& pane = m_mgr.GetPane(wxAUI_PANE_AI_TODO);
	if (!pane.IsOk()) return;
	if (!firstCreate) pane.Show(!pane.IsShown());
	m_mgr.Update();
}

void ibFrontendDocMDIFrameDesigner::FocusAiTodoPane()
{
	EnsureAiTodoPane();
	wxAuiPaneInfo& pane = m_mgr.GetPane(wxAUI_PANE_AI_TODO);
	if (pane.IsOk() && !pane.IsShown()) {
		pane.Show(true);
		m_mgr.Update();
	}
	if (m_aiTodoPanel != nullptr) {
		m_aiTodoPanel->FocusQuickAdd();
	}
}

// ---------------------------------------------------------------------------
// AI Assistant Markers pane (Workmate parity).
// ---------------------------------------------------------------------------

void ibFrontendDocMDIFrameDesigner::EnsureAiMarkersPane()
{
	if (m_mgr.GetPane(wxAUI_PANE_AI_MARKERS).IsOk()) return;

	m_aiMarkersPanel = new ibAiMarkersPanel(this);

	wxAuiPaneInfo paneInfo;
	paneInfo.Name(wxAUI_PANE_AI_MARKERS);
	paneInfo.Caption(_("Маркеры AI-ассистента"));
	paneInfo.Bottom();
	paneInfo.Layer(1);
	paneInfo.MinSize(420, 160);
	paneInfo.BestSize(640, 220);
	paneInfo.CloseButton(true);
	paneInfo.MaximizeButton(false);
	paneInfo.MinimizeButton(false);
	paneInfo.Show(true);

	m_mgr.AddPane(m_aiMarkersPanel, paneInfo);
	m_mgr.Update();
}

void ibFrontendDocMDIFrameDesigner::ToggleAiMarkersPane()
{
	const bool firstCreate = !m_mgr.GetPane(wxAUI_PANE_AI_MARKERS).IsOk();
	EnsureAiMarkersPane();
	wxAuiPaneInfo& pane = m_mgr.GetPane(wxAUI_PANE_AI_MARKERS);
	if (!pane.IsOk()) return;
	if (!firstCreate) pane.Show(!pane.IsShown());
	m_mgr.Update();
}

void ibFrontendDocMDIFrameDesigner::FocusAiMarkersPane()
{
	EnsureAiMarkersPane();
	wxAuiPaneInfo& pane = m_mgr.GetPane(wxAUI_PANE_AI_MARKERS);
	if (pane.IsOk() && !pane.IsShown()) {
		pane.Show(true);
		m_mgr.Update();
	}
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
// Plugin WebView pane wiring. Designer registers callbacks with the plugin
// manager so RegisterWebPane / WebPaneSend / WebPaneShow trampolines in
// pluginManager.cpp can construct and drive concrete wxAuiPane instances.
//
// Any plugin (assistant bridges, custom dashboard plugins, anything that
// wants a wxWebView surface) calls
// host->RegisterWebPane("<id>", "<title>", "<html>", cb, ud) from
// oes_plugin_initialize; the lambdas below run on the UI thread and add
// the resulting ibPluginWebPane to wxAuiManager.
//
// Lookups always go through m_mgr.GetPane(paneId) rather than caching
// raw ibPluginWebPane* pointers — when the user closes a pane and the
// AUI manager destroys it, a cached pointer would dangle. The set
// m_pluginWebPaneIds tracks which ids belong to us for SaveOptions.
// ---------------------------------------------------------------------------

void ibFrontendDocMDIFrameDesigner::WirePluginWebPaneCallbacks()
{
	if (m_pluginWebPaneCallbacksRegistered) return;
	auto* pm = appData->GetPluginManager();
	if (pm == nullptr) return;

	pm->SetWebPaneCallbacks(
	    // RegisterWebPane — wraps an ibPluginWebPane inside a wxAuiPane.
	    // Single source of truth for "this paneId is live" is the AUI
	    // manager: GetPane(paneId).IsOk(). m_pluginWebPaneOrder tracks
	    // registration order ONLY (deterministic default-pane pick); it
	    // is intentionally NOT used as a uniqueness guard because the
	    // user closing a pane removes it from AUI but the id would stay
	    // in the vector — re-registration must succeed.
	    [this](const wxString& paneId, const wxString& title,
	            const wxString& htmlBundlePath,
	            ibPluginWebMsgFn onMessage, void* userData) -> int {
	        if (m_mgr.GetPane(paneId).IsOk()) return -1;
	        auto* pane = new ibPluginWebPane(this, paneId, title,
	                                          htmlBundlePath, onMessage, userData);
	        wxAuiPaneInfo info;
	        info.Name(paneId);
	        info.Caption(title);
	        info.Right();
	        info.MinSize(360, 480);
	        info.BestSize(420, 600);
	        info.CloseButton(true);
	        info.MaximizeButton(false);
	        // Replay persisted visibility from options.xml. Default hidden
	        // so a brand-new plugin install doesn't pop the pane on first
	        // launch — user opens via menu or RawCtrl+Alt+I, choice persists.
	        auto pit = m_pendingPluginWebPaneVisible.find(paneId);
	        const bool wantVisible = (pit != m_pendingPluginWebPaneVisible.end()) && pit->second;
	        info.Show(wantVisible);
	        if (!m_mgr.AddPane(pane, info)) {
	            // pane is already a wxWindow child of `this`. Bare delete
	            // would race the parent's destruction list. Destroy()
	            // schedules safe teardown through the wxWidgets event loop.
	            pane->Destroy();
	            return -1;
	        }
	        m_mgr.Update();
	        // Append to order log only on first-time registration. A plugin
	        // that unregisters+re-registers the same id keeps its slot.
	        if (std::find(m_pluginWebPaneOrder.begin(), m_pluginWebPaneOrder.end(), paneId)
		        == m_pluginWebPaneOrder.end()) {
	            m_pluginWebPaneOrder.push_back(paneId);
	        }
	        return 0;
	    },
	    // WebPaneSend — thread-safe push. Resolves the live pane through
	    // the AUI manager so a closed/destroyed pane returns -1 instead
	    // of dereferencing a cached pointer.
	    [this](const wxString& paneId, const wxString& jsonInline) -> int {
	        wxAuiPaneInfo& info = m_mgr.GetPane(paneId);
	        if (!info.IsOk()) return -1;
	        // Plain dynamic_cast — ibPluginWebPane lacks the wxRTTI macros
	        // (it's a simple wxPanel subclass, not a wxObject-declared class
	        // family). RTTI is enabled project-wide via /GR (MSVC) and the
	        // default Clang/GCC behaviour.
	        auto* pane = dynamic_cast<ibPluginWebPane*>(info.window);
	        if (pane == nullptr) return -1;
	        pane->PushMessage(jsonInline);
	        return 0;
	    },
	    // WebPaneShow — force-visible flip.
	    [this](const wxString& paneId) -> int {
	        wxAuiPaneInfo& info = m_mgr.GetPane(paneId);
	        if (!info.IsOk()) return -1;
	        if (!info.IsShown()) info.Show(true);
	        m_mgr.Update();
	        return 0;
	    });

	m_pluginWebPaneCallbacksRegistered = true;

	// Replay registrations for plugins that loaded BEFORE the designer
	// frame existed (their RegisterWebPane calls during oes_plugin_initialize
	// returned -1 because no callbacks were installed yet). Each plugin
	// stashes its registration request in ibPluginManager which we now
	// drain. Without this, the menu would have no pane to show.
	pm->ReplayPendingWebPaneRegistrations();

	// Legacy-shim bridge for ABI v3 plugins that expose LLMQuery but
	// not a v4 chat pane. Resolve the
	// bundled sample.html once and hand the path to the manager — it
	// stands up a synthetic AI provider + pane on the same protocol
	// real v4 plugins use, so chat / editor.skill / menu wiring all
	// route through one codepath. Skips silently when no LLMQuery
	// function was registered (no v3 plugin loaded, or a v4 plugin
	// already covers chat mode).
	{
		const wxString rel = wxT("assets") + wxString(wxFILE_SEP_PATH)
		    + wxT("pluginWebPane") + wxString(wxFILE_SEP_PATH)
		    + wxT("sample.html");
		wxString shimBundle = wxStandardPaths::Get().GetResourcesDir()
		    + wxFILE_SEP_PATH + rel;
		if (!wxFileExists(shimBundle)) {
			wxString dir = wxFileName(
			    wxStandardPaths::Get().GetExecutablePath()).GetPath();
			for (int i = 0; i < 12; ++i) {
				const wxString candidate = dir + wxFILE_SEP_PATH + rel;
				if (wxFileExists(candidate)) { shimBundle = candidate; break; }
				wxFileName up = wxFileName::DirName(dir);
				if (up.GetDirCount() == 0) break;
				up.RemoveLastDir();
				const wxString parent = up.GetPath();
				if (parent.IsEmpty() || parent == dir) break;
				dir = parent;
			}
		}
		// The shim no-ops cleanly when the bundle is missing — pass
		// whatever we resolved (empty path falls through inside the
		// manager). Logging there carries the diagnosis.
		pm->ScanForLegacyLLMShim(shimBundle);
	}
}
