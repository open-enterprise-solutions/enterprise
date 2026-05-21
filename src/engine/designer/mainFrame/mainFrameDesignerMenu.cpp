////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"
#include "backend/appData.h"
#include "backend/plugin/pluginManager.h"
#include "backend/plugin/metaBridge.h"

#include "mainFrame/pluginManagerDialog.h"

#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>

//********************************************************************************
//*                                Hotkey support                                *
//********************************************************************************

void ibFrontendDocMDIFrameDesigner::SetDefaultHotKeys()
{
	// Setup the hotkeys.
	m_keyBinder.SetShortcut(wxID_NEW, wxT("Ctrl+N"));
	m_keyBinder.SetShortcut(wxID_SAVE, wxT("Ctrl+S"));
	m_keyBinder.SetShortcut(wxID_UNDO, wxT("Ctrl+Z"));
	m_keyBinder.SetShortcut(wxID_REDO, wxT("Ctrl+Y"));

	m_keyBinder.SetShortcut(wxID_CUT, wxT("Ctrl+X"));
	m_keyBinder.SetShortcut(wxID_COPY, wxT("Ctrl+C"));
	m_keyBinder.SetShortcut(wxID_PASTE, wxT("Ctrl+V"));
	m_keyBinder.SetShortcut(wxID_SELECTALL, wxT("Ctrl+A"));
	m_keyBinder.SetShortcut(wxID_FIND, wxT("Ctrl+F"));

	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_START, wxT("F5")); //RUN 
	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING, wxT("Ctrl+F5")); // RUN WITHOUT DEBUGGER 
	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_STEP_INTO, wxT("F11")); //STEP INTO 
	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_STEP_OVER, wxT("F10")); // STEP OVER 
	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_STOP_PROGRAM, wxT("Ctrl+Break")); // STEP OVER 
	m_keyBinder.SetShortcut(wxID_DESIGNER_DEBUG_NEXT_POINT, wxT("F9"));

	m_keyBinder.SetShortcut(wxID_DESIGNER_ABOUT, wxT("F1"));

	// Syntax helper. RawCtrl forces literal Control on macOS where
	// wxWidgets otherwise rewrites "Ctrl" to Cmd; on Windows / Linux
	// RawCtrl is identical to Ctrl. Without these SetShortcut calls
	// ibKeyBinder strips the accelerator labels during LoadOptions.
	m_keyBinder.SetShortcut(wxID_FRONTEND_SYNTAX_HELPER,        wxT("RawCtrl+Alt+F1"));
	m_keyBinder.SetShortcut(wxID_FRONTEND_SYNTAX_HELPER_LOOKUP, wxT("RawCtrl+F1"));

	// Default plugin-driven AI chat pane toggle. RawCtrl for the same
	// macOS-vs-Cmd reason as the Syntax Helper entries above.
	m_keyBinder.SetShortcut(wxID_FRONTEND_PLUGIN_WEB_PANE,      wxT("RawCtrl+Alt+I"));

	// Project Search panel (Workmate parity). Ctrl+Shift+F so it lives
	// next to wxID_FIND (Ctrl+F) without colliding with the editor's
	// own document-scope find dialog. RawCtrl keeps the literal Control
	// key on macOS — Cmd+F is the system find pattern and we don't want
	// to steal it.
	m_keyBinder.SetShortcut(wxID_DESIGNER_PROJECT_SEARCH,      wxT("RawCtrl+Shift+F"));
}

//********************************************************************************
//*                                Default menu                                  *
//********************************************************************************

enum MDI_MENU_ID
{
	wxWINDOWCLOSE = 4001,
	wxWINDOWCLOSEALL,
	wxWINDOWNEXT,
	wxWINDOWPREV
};

#include <wx/artprov.h>
#include <wx/config.h>

#include "frontend/artProvider/artProvider.h"

void ibFrontendDocMDIFrameDesigner::InitializeDefaultMenu()
{
	m_frameMenuBar = new wxMenuBar;

	// and its menu bar
	m_menuFile = new wxMenu();

	m_menuFile->Append(wxID_NEW);
	m_menuFile->Append(wxID_OPEN);

	m_menuFile->Append(wxID_CLOSE);
	m_menuFile->Append(wxID_SAVE);
	m_menuFile->Append(wxID_SAVEAS);
	m_menuFile->Append(wxID_REVERT, _("Re&vert..."));

	m_menuFile->AppendSeparator();
	m_menuFile->Append(wxID_PRINT);
	m_menuFile->Append(wxID_PRINT_SETUP, _("Print &Setup..."));
	m_menuFile->Append(wxID_PREVIEW);

	m_menuFile->AppendSeparator();
	m_menuFile->Append(wxID_EXIT);

	m_frameMenuBar->Append(m_menuFile, wxGetStockLabel(wxID_FILE));

	// A nice touch: a history of files visited. Use this menu.
	m_docManager->FileHistoryUseMenu(m_menuFile);

#if wxUSE_CONFIG
	m_docManager->FileHistoryLoad(*wxConfig::Get());
#endif // wxUSE_CONFIG

	m_menuEdit = new wxMenu;
	m_menuEdit->Append(wxID_UNDO);
	m_menuEdit->Append(wxID_REDO);
	m_menuEdit->AppendSeparator();
	m_menuEdit->Append(wxID_CUT);
	m_menuEdit->Append(wxID_COPY);
	m_menuEdit->Append(wxID_PASTE);
	m_menuEdit->Append(wxID_DELETE);
	m_menuEdit->Append(wxID_SELECTALL);
	m_menuEdit->AppendSeparator();
	m_menuEdit->Append(wxID_FIND);
	// Workmate-parity "Project Search by metadata". Ctrl+Shift+F so it
	// doesn't collide with the editor's wxID_FIND (Ctrl+F) — the latter
	// scopes the current document, this one scopes the configuration.
	m_menuEdit->Append(wxID_DESIGNER_PROJECT_SEARCH,
	                    _("Поиск по метаданным…\tRawCtrl+Shift+F"));

	// Workmate-parity AI Assistant panes — TODO list and aggregated
	// Markers. No keyboard shortcut by default; users open them via
	// the Edit menu like other dockable views.
	m_menuEdit->Append(wxID_DESIGNER_AI_TODO,
	                    _("Задачи AI-ассистента"));
	m_menuEdit->Append(wxID_DESIGNER_AI_MARKERS,
	                    _("Маркеры AI-ассистента"));

	m_frameMenuBar->Append(m_menuEdit, wxGetStockLabel(wxID_EDIT));

	m_menuDebug = new wxMenu;

	// "Start debugging" → GUI / Web
	wxMenu* subStart = new wxMenu;
	subStart->Append(wxID_DESIGNER_DEBUG_START, _("Thick client (GUI)"));
	subStart->Append(wxID_DESIGNER_DEBUG_START_WEB, _("Web client"));
	m_menuDebug->AppendSubMenu(subStart, _("Start debugging"));

	// "Start without debugging" → GUI / Web
	wxMenu* subStartNoDebug = new wxMenu;
	subStartNoDebug->Append(wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING, _("Thick client (GUI)"));
	subStartNoDebug->Append(wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING_WEB, _("Web client"));
	m_menuDebug->AppendSubMenu(subStartNoDebug, _("Start without debugging"));

	m_menuDebug->Append(wxID_DESIGNER_DEBUG_ATTACH_FOR_DEBUGGING, _("Attach for debugging..."));
	m_menuDebug->AppendSeparator();

	m_menuDebug->Append(wxID_DESIGNER_DEBUG_NEXT_POINT, _("Continue"))->Enable(false);
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_PAUSE, _("Pause"), _("Pause"))->Enable(false);
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_STEP_INTO, _("Step into"))->Enable(false);
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_STEP_OVER, _("Step over"))->Enable(false);
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_STOP_DEBUGGING, _("Stop debugging"), _("Stop debugging"))->Enable(false);
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_STOP_PROGRAM, _("Stop debugging program"), _("Stop program"))->Enable(false);

	m_menuDebug->AppendSeparator();
	m_menuDebug->Append(wxID_DESIGNER_DEBUG_REMOVE_ALL_DEBUGPOINTS, _("Remove all breakpoints"));

	m_menuConfiguration = new wxMenu;

	wxMenuItem* menuItem = nullptr;
	
	menuItem = m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_OPEN_DATABASE, _("Open database configuration"));
	menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DATABASE, wxART_FRONTEND, wxSize(16, 16)));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());	
	menuItem = m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_ROLLBACK_DATABASE, _("Rollback to database configuration"));
	menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DATABASE_ROOLBACK, wxART_FRONTEND, wxSize(16, 16)));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());

	menuItem = m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_UPDATE_DATABASE, _("Update database configuration"));
	menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DATABASE_APPLY, wxART_FRONTEND, wxSize(16, 16)));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());

	m_menuConfiguration->AppendSeparator();

	menuItem = m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_LOAD_FROM_FILE, _("Load configuration"));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());
	menuItem = m_menuConfiguration->Append(wxID_DESIGNER_CONFIGURATION_SAVE_TO_FILE, _("Save configuration"));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());

	m_frameMenuBar->Append(m_menuConfiguration, _("Configuration"));
	m_frameMenuBar->Append(m_menuDebug, _("Debug"));

	m_menuAdministration = new wxMenu;
	
	menuItem = m_menuAdministration->Append(wxID_APPLICATION_USERS, _("Users"));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());
	menuItem = m_menuAdministration->Append(wxID_APPLICATION_ACTIVE_USERS, _("Active users"));
	menuItem->Enable(activeMetaData->AccessRight_ActiveUsers());
	m_menuAdministration->AppendSeparator();
	menuItem = m_menuAdministration->Append(wxID_DESIGNER_DATABASE_LOAD_FROM_FILE, _("Restore database"));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());
	menuItem = m_menuAdministration->Append(wxID_DESIGNER_DATABASE_SAVE_TO_FILE, _("Dump database"));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());
	m_menuAdministration->AppendSeparator();
	menuItem = m_menuAdministration->Append(wxID_DESIGNER_DATABASE_CLEAR, _("Clear database"));
	menuItem->Enable(activeMetaData->AccessRight_DataAdministration());

	m_frameMenuBar->Append(m_menuAdministration, _("Administration"));

	m_menuSetting = new wxMenu;
	m_frameMenuBar->Append(m_menuSetting, _("Tools"));
	m_menuSetting->Append(wxID_APPLICATION_SETTING, _("Options..."));

	// Syntax helper items in Tools, NOT Help — macOS wxWidgets routes
	// the Help menu through the system-native Help search which eats
	// custom command bindings. RawCtrl forces the literal Control key
	// on every platform (wxWidgets maps "Ctrl" to Cmd on macOS).
	m_menuSetting->AppendSeparator();
	m_menuSetting->Append(wxID_FRONTEND_SYNTAX_HELPER,
	                       _("Syntax Helper\tRawCtrl+Alt+F1"));
	m_menuSetting->Append(wxID_FRONTEND_SYNTAX_HELPER_LOOKUP,
	                       _("Look up in Syntax Helper\tRawCtrl+F1"));
	m_menuSetting->Append(wxID_FRONTEND_PLUGIN_WEB_PANE,
	                       _("AI Assistant\tRawCtrl+Alt+I"));
	m_menuSetting->Append(wxID_FRONTEND_PLUGIN_MANAGER,
	                       _("Plugins…"));

	// Plugin-supplied menu items live under Tools → Plugins. Built from
	// the plugin manager's RegisteredMenuItem table. Each click routes
	// through CallMenuHandler so the plugin's callback runs inside a
	// host-managed ibPluginCallScope.
	if (auto* pm = appData->GetPluginManager()) {
		const auto& items = pm->MenuItems();
		if (!items.empty()) {
			auto* pluginsMenu = new wxMenu;
			constexpr int kPluginIdBase = wxID_HIGHEST + 6000;
			int slot = 0;
			for (const auto& item : items) {
				const int id = kPluginIdBase + slot++;
				pluginsMenu->Append(id,
				    wxString::FromUTF8(item.m_label.c_str()));
				Bind(wxEVT_MENU,
				     [pm, &item](wxCommandEvent&) {
				         pm->CallMenuHandler(item);
				     },
				     id);
			}
			m_menuSetting->AppendSeparator();
			m_menuSetting->AppendSubMenu(pluginsMenu, _("Plugins"));
		}
	}

	m_menuHelp = new wxMenu;
	m_menuHelp->Append(wxID_DESIGNER_ABOUT, _("About"));
	m_frameMenuBar->Append(m_menuHelp, wxGetStockLabel(wxID_HELP, wxSTOCK_NOFLAGS));

	// Ctrl+Z hook for agent mutations. Tried BEFORE wxDocument's default
	// undo routing so "Ctrl+Z reverts whole agent turn" (Phase 3.4 spec)
	// works at the configuration level. When the agent undo stack is
	// empty we Skip() so the focused editor's command processor handles
	// it the normal wxWidgets way (form/module editor undo).
	Bind(wxEVT_MENU,
	     [](wxCommandEvent& evt) {
	         if (metaBridge::UndoLastAgentMutation() != 0) {
	             evt.Skip();   // empty stack → fall through to editor undo
	         }
	     },
	     wxID_UNDO);

	// Tools → Plugins — Phase 4.3 settings dialog. Read/write of
	// plugins.json5 + BYOK env files lives entirely inside the dialog;
	// the menu handler is a thin show-modal shim.
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) {
	         ibPluginManagerDialog dlg(this);
	         dlg.ShowModal();
	     },
	     wxID_FRONTEND_PLUGIN_MANAGER);

	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { ToggleHelpPane(); },
	     wxID_FRONTEND_SYNTAX_HELPER);
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { OpenHelpForCursor(); },
	     wxID_FRONTEND_SYNTAX_HELPER_LOOKUP);

	// Workmate-parity Project Search by metadata. Ensures the pane,
	// raises it, focuses the query input — same UX shape as the
	// Syntax Helper "open and ready to type" pattern.
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { FocusProjectSearchPane(); },
	     wxID_DESIGNER_PROJECT_SEARCH);

	// Workmate-parity AI Assistant panes — TODO list and aggregated
	// Markers. Toggle on each invocation so a second click hides
	// the panel; the same pattern Syntax Helper uses.
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { ToggleAiTodoPane(); },
	     wxID_DESIGNER_AI_TODO);
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { ToggleAiMarkersPane(); },
	     wxID_DESIGNER_AI_MARKERS);
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) {
	         WirePluginWebPaneCallbacks();
	         auto* pm = appData->GetPluginManager();
	         if (pm == nullptr) return;

	         // Show the first registered plugin pane that is still live
	         // (the user may have closed an earlier one — skip stale ids).
	         for (const wxString& paneId : m_pluginWebPaneOrder) {
	             if (m_mgr.GetPane(paneId).IsOk()) {
	                 pm->CallWebPaneShow(paneId);
	                 return;
	             }
	         }
	         // No plugin registered a WebView pane (e.g. pre-v4 Pugi).
	         // Fall back to the built-in demo pane so the user can verify
	         // the WebView infrastructure end-to-end. The sample HTML
	         // ships next to the executable under assets/pluginWebPane/.
	         // Look for assets/pluginWebPane/sample.html. Production install
	         // places it under wxStandardPaths Resources; dev builds keep
	         // it in the repo root several dirs above the .app bundle
	         // (build/bin/Debug/designer.app/Contents/MacOS/). Walk UP the
	         // executable directory until we find a sibling `assets/` dir
	         // containing the bundle.
	         const wxString rel = wxT("assets") + wxString(wxFILE_SEP_PATH)
	             + wxT("pluginWebPane") + wxString(wxFILE_SEP_PATH)
	             + wxT("sample.html");
	         wxString resolvedPath = wxStandardPaths::Get().GetResourcesDir()
	             + wxFILE_SEP_PATH + rel;
	         if (!wxFileExists(resolvedPath)) {
	             wxString dir = wxFileName(
	                 wxStandardPaths::Get().GetExecutablePath()).GetPath();
	             for (int i = 0; i < 12; ++i) {
	                 const wxString candidate = dir + wxFILE_SEP_PATH + rel;
	                 if (wxFileExists(candidate)) {
	                     resolvedPath = candidate;
	                     break;
	                 }
	                 wxFileName up = wxFileName::DirName(dir);
	                 if (up.GetDirCount() == 0) break; // at filesystem root
	                 up.RemoveLastDir();
	                 const wxString parent = up.GetPath();
	                 if (parent.IsEmpty() || parent == dir) break;
	                 dir = parent;
	             }
	         }
	         wxLogDebug(wxT("AI Chat sample.html resolved -> %s (exists=%d)"),
	                    resolvedPath, (int)wxFileExists(resolvedPath));
	         if (!wxFileExists(resolvedPath)) {
	             wxMessageBox(_("AI Chat: sample HTML bundle not found.\n"
	                             "Install a v4 plugin via Tools → Plugins."),
	                          _("AI Assistant"), wxICON_INFORMATION, this);
	             return;
	         }
	         const int rc = pm->CallWebPaneRegister(
	             wxT("designer.demo.chat"),
	             _("AI Assistant"),
	             resolvedPath,
	             /*onMessage*/ nullptr,
	             /*userData*/  nullptr);
	         if (rc == 0) {
	             pm->CallWebPaneShow(wxT("designer.demo.chat"));
	         }
	     },
	     wxID_FRONTEND_PLUGIN_WEB_PANE);

	// Editor context-menu items use frontend-shared command ids
	// (frontend.dll cannot depend on the downstream designer header).
	// Forward the three frontend debug ids to the equivalent designer
	// ids that OnRunDebugCommand handles.
	auto forwardDebugId = [this](int designerId) {
		return [this, designerId](wxCommandEvent&) {
			wxCommandEvent ev(wxEVT_MENU, designerId);
			ev.SetEventObject(this);
			ProcessWindowEvent(ev);
		};
	};
	Bind(wxEVT_MENU, forwardDebugId(wxID_DESIGNER_DEBUG_STEP_INTO),
	     wxID_FRONTEND_DEBUG_STEP_INTO);
	Bind(wxEVT_MENU, forwardDebugId(wxID_DESIGNER_DEBUG_STEP_OVER),
	     wxID_FRONTEND_DEBUG_STEP_OVER);
	Bind(wxEVT_MENU, forwardDebugId(wxID_DESIGNER_DEBUG_REMOVE_ALL_DEBUGPOINTS),
	     wxID_FRONTEND_DEBUG_REMOVE_ALL_BREAKPOINTS);

	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnOpenConfiguration, this, wxID_DESIGNER_CONFIGURATION_OPEN_DATABASE);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnRollbackConfiguration, this, wxID_DESIGNER_CONFIGURATION_ROLLBACK_DATABASE);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnUpdateConfiguration, this, wxID_DESIGNER_CONFIGURATION_UPDATE_DATABASE);

	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnConfiguration, this, wxID_DESIGNER_CONFIGURATION_LOAD_FROM_FILE, wxID_DESIGNER_CONFIGURATION_SAVE_TO_FILE);

	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnStartDebug, this, wxID_DESIGNER_DEBUG_START);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnStartDebugWithoutDebug, this, wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnStartDebugWeb, this, wxID_DESIGNER_DEBUG_START_WEB);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnStartDebugWithoutDebugWeb, this, wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING_WEB);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnAttachForDebugging, this, wxID_DESIGNER_DEBUG_ATTACH_FOR_DEBUGGING);

	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnRunDebugCommand, this, wxID_DESIGNER_DEBUG_EDIT_POINT, wxID_DESIGNER_DEBUG_REMOVE_ALL_DEBUGPOINTS);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnToolsSettings, this, wxID_APPLICATION_SETTING);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnUsers, this, wxID_APPLICATION_USERS);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnActiveUsers, this, wxID_APPLICATION_ACTIVE_USERS);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnConnection, this, wxID_APPLICATION_CONNECTION);

	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnLoadDatabase, this, wxID_DESIGNER_DATABASE_LOAD_FROM_FILE);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnSaveDatabase, this, wxID_DESIGNER_DATABASE_SAVE_TO_FILE);
	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnClearDatabase, this, wxID_DESIGNER_DATABASE_CLEAR);

	Bind(wxEVT_MENU, &ibFrontendDocMDIFrameDesigner::OnAbout, this, wxID_DESIGNER_ABOUT);

	LoadOptions();

	// Wire plugin-WebView pane callbacks now that the plugin manager is
	// alive and the designer frame exists. Plugins loaded BEFORE the
	// frame existed (the normal startup order — appData->LoadAll fires
	// during ibApplicationData ctor) had their RegisterWebPane calls
	// buffered inside ibPluginManager; WirePluginWebPaneCallbacks installs
	// the lambdas and then calls ReplayPendingWebPaneRegistrations to
	// drain the buffer. The method early-returns on duplicate calls so
	// the on-menu invocation in the wxID_FRONTEND_PLUGIN_WEB_PANE handler
	// stays a no-op afterwards.
	WirePluginWebPaneCallbacks();
}