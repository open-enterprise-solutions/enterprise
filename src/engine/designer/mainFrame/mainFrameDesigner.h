#ifndef _MAINFRAME_DESIGNER_H__
#define _MAINFRAME_DESIGNER_H__

#include "frontend/mainFrame/mainFrame.h"
#include "mainFrame/metaTree/treeConfiguration.h"

#include <unordered_map>
#include <vector>

#if defined(mainFrame)
#undef mainFrame
#endif

#include "mainFrame/output/outputWindow.h"
#include "mainFrame/local/localWindow.h"
#include "mainFrame/stack/stackWindow.h"
#include "mainFrame/watch/watchWindow.h"

enum {

	wxID_DESIGNER_DEBUG_START = 10000,
	wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING,
	wxID_DESIGNER_DEBUG_START_WEB,
	wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING_WEB,
	wxID_DESIGNER_DEBUG_ATTACH_FOR_DEBUGGING,
	wxID_DESIGNER_DEBUG_EDIT_POINT,
	wxID_DESIGNER_DEBUG_STEP_OVER,
	wxID_DESIGNER_DEBUG_STEP_INTO,
	wxID_DESIGNER_DEBUG_PAUSE,
	wxID_DESIGNER_DEBUG_STOP_DEBUGGING,
	wxID_DESIGNER_DEBUG_STOP_PROGRAM,
	wxID_DESIGNER_DEBUG_NEXT_POINT,
	wxID_DESIGNER_DEBUG_REMOVE_ALL_DEBUGPOINTS,

	wxID_DESIGNER_CONFIGURATION_OPEN_DATABASE,
	wxID_DESIGNER_CONFIGURATION_UPDATE_DATABASE,
	wxID_DESIGNER_CONFIGURATION_ROLLBACK_DATABASE,

	wxID_DESIGNER_CONFIGURATION_LOAD_FROM_FILE,
	wxID_DESIGNER_CONFIGURATION_SAVE_TO_FILE,

	wxID_DESIGNER_DATABASE_LOAD_FROM_FILE,
	wxID_DESIGNER_DATABASE_SAVE_TO_FILE,

	wxID_DESIGNER_DATABASE_CLEAR,

	wxID_DESIGNER_ABOUT,

	// Project Search panel (Workmate parity). Designer-private menu id,
	// bound to Ctrl+Shift+F. Designer-owned so the binding survives
	// frontend.dll / external rebuilds without touching the shared header.
	wxID_DESIGNER_PROJECT_SEARCH,

	// AI Assistant TODO list panel (Workmate parity). Designer-private
	// menu id — toggles the dockable list of accumulated agent tasks.
	wxID_DESIGNER_AI_TODO,

	// AI Assistant Markers panel (Workmate parity). Designer-private
	// menu id — toggles the dockable aggregated marker list.
	wxID_DESIGNER_AI_MARKERS,

	// Template Wizard ("Создать из шаблона..."). Designer-private menu
	// id — opens the gallery → preview → customize modal that builds a
	// new configuration from a Pugi-served template. File menu only;
	// no keyboard shortcut by default (rarely-invoked flagship action).
	wxID_DESIGNER_TEMPLATE_WIZARD,

	wxID_DESIGNER_AI_ONBOARDING,

	wxID_DESIGNER_END
};
// Note: Syntax-helper command ids live in frontend/mainFrame/mainFrame.h
// (wxID_FRONTEND_SYNTAX_HELPER / wxID_FRONTEND_SYNTAX_HELPER_LOOKUP) so
// frontend widgets (e.g. ibCodeEditor's context menu) can post them
// without taking a dependency on the downstream designer header.

//menu  
enum {
	wxID_APPLICATION_DEBUG = wxID_HIGHEST + 1,
	wxID_APPLICATION_SETTING,
	wxID_APPLICATION_USERS,
	wxID_APPLICATION_ACTIVE_USERS,
	wxID_APPLICATION_CONNECTION,
};

#define mainFrame	(ibFrontendDocMDIFrameDesigner::GetFrame())

class ibFrontendDocMDIFrameDesigner : public ibFrontendDocMDIFrame {
public:

	static ibFrontendDocMDIFrameDesigner* GetFrame();

	ibFrontendDocMDIFrameDesigner(const wxString& title = _("Designer"),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize);

	virtual ~ibFrontendDocMDIFrameDesigner();

	void Message(const wxString& strMessage, ibStatusMessage status) override { m_outputWindow->SharedOutput(strMessage, status); }
	void ClearMessage() override { m_outputWindow->ClearAll(); }

	void BackendError(const wxString& strFileName, const wxString& strDocPath, const long line, const wxString& strErrorMessage) const override {
		m_outputWindow->SharedOutput(strErrorMessage, ibStatusMessage::ibStatusMessage_Error, strFileName, strDocPath, line);
	}

	virtual void CreateGUI() override;
	virtual void Modify(bool modify) override;
	virtual bool IsModified() const override;

	ibOutputWindow* GetOutputWindow() const { return m_outputWindow; }
	ibStackWindow* GetStackWindow() const { return m_stackWindow; }
	ibWatchWindow* GetWatchWindow() const { return m_watchWindow; }
	ibLocalWindow* GetLocalWindow() const { return m_localWindow; }
	// Accessor for the Configuration tree pane — required by the
	// Project Search panel so it can route OpenFormMDI calls without
	// reaching into AUI internals.
	ibMetadataTree* GetMetadataTreeWindow() const { return m_metaWindow; }

	// Syntax-helper sidebar lifecycle. Pane is lazy-created on first
	// toggle / lookup so the corpus load is amortised away from
	// designer startup.
	void EnsureHelpPane();
	void ToggleHelpPane();
	void OpenHelpForCursor();

	// Project Search pane (Workmate parity: "Project Search — by
	// metadata"). The pane is created together with the Configuration
	// tree but starts hidden; Ctrl+Shift+F brings it to focus.
	void EnsureProjectSearchPane();
	void ToggleProjectSearchPane();
	void FocusProjectSearchPane();

	// AI Assistant TODO list (Workmate parity: "Управление: TODO лист,
	// Подзадача, Команда системы, Проекты"). Stores per-configuration
	// rows under ~/Library/Preferences/OES/ai-todo/<configHash>.json.
	// Pane is lazily created on first toggle so cold-start cost stays
	// flat for users who never open it.
	void EnsureAiTodoPane();
	void ToggleAiTodoPane();
	void FocusAiTodoPane();

	// AI Markers panel (Workmate parity: "Маркеры: Установка и удаление
	// маркеров в редакторе"). Aggregates editor markers across the
	// project. Lazy-created on first toggle.
	void EnsureAiMarkersPane();
	void ToggleAiMarkersPane();
	void FocusAiMarkersPane();

	void LoadOptions();
	void SaveOptions();

#pragma region debugger 
	void Debugger_OnSessionStart();
	void Debugger_OnSessionEnd();
	void Debugger_OnEnterLoop();
	void Debugger_OnLeaveLoop();
#pragma endregion 

	virtual bool Show(bool show = true) override;

protected:

	void InitializeDefaultMenu();

	virtual void CreateMetadataPane();
	virtual void CreateBottomPane();
	virtual void CreateWideGui();

	virtual bool AllowRun() const override;
	virtual bool AllowClose() const override;

	/**
	* Adds the default profile to the hot keys.
	*/
	void SetDefaultHotKeys();

	/**
	* Updates all of the open editors with the current font, color, etc.
	* options.
	*/
	void UpdateEditorOptions();

	//events 
	void OnStartDebug(wxCommandEvent& WXUNUSED(event));
	void OnStartDebugWithoutDebug(wxCommandEvent& WXUNUSED(event));
	void OnStartDebugWeb(wxCommandEvent& WXUNUSED(event));
	void OnStartDebugWithoutDebugWeb(wxCommandEvent& WXUNUSED(event));
	void OnAttachForDebugging(wxCommandEvent& WXUNUSED(event));

	void OnOpenConfiguration(wxCommandEvent& event);
	void OnRollbackConfiguration(wxCommandEvent& event);
	void OnUpdateConfiguration(wxCommandEvent& event);

	void OnLoadDatabase(wxCommandEvent& event);
	void OnSaveDatabase(wxCommandEvent& event);
	void OnClearDatabase(wxCommandEvent& event);

	void OnConfiguration(wxCommandEvent& event);
	void OnRunDebugCommand(wxCommandEvent& event);
	void OnToolsSettings(wxCommandEvent& event);
	void OnUsers(wxCommandEvent& event);
	void OnActiveUsers(wxCommandEvent& event);
	void OnConnection(wxCommandEvent& event);

	void OnAbout(wxCommandEvent& event);

private:

	wxMenu* m_menuFile;
	wxMenu* m_menuEdit;
	wxMenu* m_menuConfiguration;
	wxMenu* m_menuDebug;
	wxMenu* m_menuSetting;
	wxMenu* m_menuAdministration;
	wxMenu* m_menuHelp;

	// Syntax-helper sidebar. Created on first toggle / lookup; owned
	// by the AUI manager once added.
	class ibHelpPaneView* m_helpPane = nullptr;

	// Project Search panel (Ctrl+Shift+F). Created lazily on first
	// toggle/focus; owned by the AUI manager once added.
	class ibProjectSearchPanel* m_projectSearchPanel = nullptr;

	// AI Assistant TODO panel (Workmate parity). Lazily created.
	class ibAiTodoPanel* m_aiTodoPanel = nullptr;

	// AI Assistant Markers panel (Workmate parity). Lazily created.
	class ibAiMarkersPanel* m_aiMarkersPanel = nullptr;

	// MCP concurrency notifier — polls the configuration directory's
	// .oes-mcp-mutation marker every ~2.5s and surfaces a status-bar
	// toast when oes-mcp (or another cooperating tool) reports a write.
	// Lazily allocated on first config open; lives for the rest of the
	// Designer process. See externalMutationNotifier.{h,cpp}.
	class ibExternalMutationNotifier* m_externalMutationNotifier = nullptr;

public:
	// Activate the external-mutation notifier for the given configuration
	// directory. Called from CreateWideGui (initial open) and from any
	// path that swaps configurations. Pass an empty string to deactivate.
	void StartExternalMutationNotifier(const wxString& configDir);
	void StopExternalMutationNotifier();

private:

	// Plugin-driven WebView panes — generic infrastructure for any plugin
	// that wants to embed a wxWebView-based UI (AI assistants, custom
	// integration dashboards, etc.). Ordered registration log; the AUI
	// manager owns the actual ibPluginWebPane lifecycle. Lookups go via
	// m_mgr.GetPane(paneId) so a closed-and-destroyed pane never leaves
	// a dangling pointer behind. The vector preserves registration order
	// so the "primary pane" picker (RawCtrl+Alt+I default action) is
	// deterministic across restarts — first registered wins.
	std::vector<wxString> m_pluginWebPaneOrder;
	bool m_pluginWebPaneCallbacksRegistered = false;
	void WirePluginWebPaneCallbacks();

	// Pre-loaded visibility map from options.xml. Plugins register their
	// panes asynchronously after LoadOptions has already run, so the
	// visibility decision happens later inside the RegisterWebPane
	// callback — we look up paneId here and apply Show() at AUI-add time.
	// Entries persist after consumption so SaveOptions can write back
	// state for panes that were registered but later closed.
	std::unordered_map<wxString, bool> m_pendingPluginWebPaneVisible;

	// Help-pane state pulled out of options.xml at startup and
	// applied lazily once EnsureHelpPane creates the widget. The
	// pane is built on first user invocation, which can happen well
	// after LoadOptions runs.
	struct PendingHelpState {
		bool      has        = false;
		wxString  currentId;
		long      tab        = 0;
		long      fontBoost  = 0;
	};
	PendingHelpState m_pendingHelpState;

	ibMetadataTree* m_metaWindow;

	ibOutputWindow* m_outputWindow;
	ibStackWindow* m_stackWindow;
	ibWatchWindow* m_watchWindow;
	ibLocalWindow* m_localWindow;
};
#endif
