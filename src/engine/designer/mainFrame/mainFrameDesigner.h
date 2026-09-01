#ifndef _MAINFRAME_DESIGNER_H__
#define _MAINFRAME_DESIGNER_H__

#include "frontend/mainFrame/mainFrame.h"
#include "mainFrame/metaTree/treeConfiguration.h"

#if defined(mainFrame)
#undef mainFrame
#endif

#include "mcp/mcpDesignerMessages.h"   // where a message goes besides the pane
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
	wxID_DESIGNER_CONFIGURATION_EXPORT_JSON,   // EXPERIMENTAL — builder -> JSON dump
	wxID_DESIGNER_CONFIGURATION_COMPARE_FILE,
	wxID_DESIGNER_CONFIGURATION_COMPARE_DB,
	wxID_DESIGNER_CONFIGURATION_COMPARE_TWO_FILES,

	wxID_DESIGNER_DATABASE_LOAD_FROM_FILE,
	wxID_DESIGNER_DATABASE_SAVE_TO_FILE,

	wxID_DESIGNER_DATABASE_CLEAR,

	wxID_DESIGNER_ABOUT,
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
	wxID_APPLICATION_AUDIT_LOG,
	wxID_APPLICATION_CONNECTION,
	// Assistant access — one command, because starting and stopping are the same
	// switch seen from its two sides; the label follows the state through
	// EVT_UPDATE_UI rather than through two entries a person has to choose between.
	wxID_APPLICATION_MCP_SERVER,
	wxID_APPLICATION_MCP_ASSISTANT,
};

#define mainFrame	(ibFrontendMainFrameDesigner::GetFrame())

// ⭐⭐ THE DESIGNER IS THE NOTIFIER for the configuration it shows (Max, 2026-09-01: *"the designer
// inherits from it; my notifier onto the metadata tree as owner; the tree itself is just a panel"*).
//
// Everything a notifier is asked to DO is a window's work — open a document, put up a modal dialog,
// save or apply a configuration, mark the line the debugger stopped on. Until now a wxPanel did all
// of it by reaching for `docManager`, `objectInspector` and `mainFrame`, which is a panel carrying
// the window's job and the reason those globals were reachable from a navigator at all.
//
// The inherited bodies forward to the OWNER — this window's `m_metaWindow` — so the row work
// (re-read, relabel, erase) still lands on the panel that has the rows. Nothing had to be
// duplicated to move the subscription up here.
class ibFrontendMainFrameDesigner : public ibFrontendMainFrame {
public:

	static ibFrontendMainFrameDesigner* GetFrame();

	// Built around an authenticated session; owns it from here.
	explicit ibFrontendMainFrameDesigner(ibSessionHolder&& holder,
		const wxString& title = _("Designer"),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize);

	virtual ~ibFrontendMainFrameDesigner();

	// Each of these is ONE line, because showing a message and passing it on is one act and the
	// output window does both (see ibOutputWindow::SharedOutput). They used to spell the pair here,
	// four times over — and a pair spelled at every caller is a pair somebody eventually writes
	// half of.
	void Message(const wxString& strMessage, ibStatusMessage status) {
		m_outputWindow->SharedOutput(strMessage, status);
	}

	void ClearMessage() {
		m_outputWindow->ClearOutput();
	}

	void BackendError(const wxString& strFileName, const wxString& strDocPath, const long line, const wxString& strErrorMessage) const {
		m_outputWindow->SharedOutput(strErrorMessage, ibStatusMessage::ibStatusMessage_Error, strFileName, strDocPath, line);
	}

	// A MODAL BOX IS THE LOUDEST OF THEM, and the only one that never reaches the
	// pane at all — it is shown to the person in front of it and to nobody else.
	int ShowModalMessage(const wxString& message, const wxString& caption, int style) override {
		ibDesignerMessages::Report({ caption.IsEmpty() ? message : caption + wxT(": ") + message,
			ibStatusMessage::ibStatusMessage_Warning, wxEmptyString, wxNOT_FOUND, true });
		return ibFrontendMainFrame::ShowModalMessage(message, caption, style);
	}

	// What the running application says, arriving over the debugger — it used to
	// be written into the pane directly, past everything else.
	void RuntimeMessage(const wxString& strMessage, const wxString& strFileName,
		const wxString& strDocPath, const long line) {
		ibDesignerMessages::Report({ strMessage, ibStatusMessage::ibStatusMessage_Error, strDocPath, line, false });
		m_outputWindow->OutputError(strMessage, strFileName, strDocPath, line);
	}

	virtual void CreateGUI() override;
	virtual void Modify(bool modify) override;
	virtual bool IsModified() const override;

	ibOutputWindow* GetOutputWindow() const { return m_outputWindow; }
	ibStackWindow* GetStackWindow() const { return m_stackWindow; }
	ibWatchWindow* GetWatchWindow() const { return m_watchWindow; }
	ibLocalWindow* GetLocalWindow() const { return m_localWindow; }

	// ⭐ THE NAVIGATOR ITSELF, beside the other panes, for whoever needs to ASK it something — the
	// debugger's bridge and the output pane ask it to show a module (Max, 2026-09-01: *"you just
	// take the tree from the main form and send it a signal"*). Which tree shows a FILE'S metadata
	// is that document's answer instead; see ibMetaDataDocument::GetMetaTree.
	ibConfigurationTree* GetMetaWindow() const { return m_metaWindow; }

	// Syntax-helper sidebar lifecycle. Pane is lazy-created on first
	// toggle / lookup so the corpus load is amortised away from
	// designer startup. OpenHelpForCursor: resolve the identifier
	// at the focused editor's caret and either drive the pane to the
	// single match or open the chooser on multiple matches.
	void EnsureHelpPane();
	void ToggleHelpPane();
	void OpenHelpForCursor();

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
	virtual void CreateGitPane();
	virtual void CreateBottomPane();
	virtual void CreateWideGui();

	// Opening loads the metadata tree (no session scripts here — the
	// Designer has no runtime); closing asks about an unsaved
	// configuration.
	bool AllowRun() override;
	bool AllowClose() override;

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

	// ⚠ THE THREE CONFIGURATION VERBS ARE NOT HERE. Save, apply and rollback live on the metadata
	// TREE (ibMetaDataNotifier, implemented by ibConfigurationTree), because that interface is
	// what both sides can reach: the menu items below redirect into it, and so does the
	// assistant's tool from the backend. A copy on this window would have been the second road.


	void OnLoadDatabase(wxCommandEvent& event);
	void OnSaveDatabase(wxCommandEvent& event);
	void OnClearDatabase(wxCommandEvent& event);

	void OnConfiguration(wxCommandEvent& event);
	void OnRunDebugCommand(wxCommandEvent& event);
	void OnToolsSettings(wxCommandEvent& event);
	void OnUsers(wxCommandEvent& event);
	void OnActiveUsers(wxCommandEvent& event);
	void OnAuditLog(wxCommandEvent& event);
	void OnConnection(wxCommandEvent& event);

	// Assistant access: start it in the name of THIS session, or stop it.
	// Starting and stopping moved to the settings page — see the note in the menu. What is left
	// here is the window, and whether it can be opened at all.
	void OnUpdateMcpAssistant(wxUpdateUIEvent& event);

	// The window that shows the exchange — a tab, like the journal.
	void OnMcpAssistant(wxCommandEvent& event);

	void OnAbout(wxCommandEvent& event);

private:

	wxMenu* m_menuFile;
	wxMenu* m_menuEdit;
	wxMenu* m_menuConfiguration;
	wxMenu* m_menuDebug;
	wxMenu* m_menuSetting;
	wxMenu* m_menuAdministration;
	wxMenu* m_menuHelp;

	// Syntax-helper sidebar. Created on first toggle; owned by the
	// AUI manager once added. XML state persistence (last entry id /
	// active tab / detail font boost) lands as a separate cosmetic
	// step — pane works fully without it, just doesn't remember
	// position across sessions.
	class ibHelpPaneView* m_helpPane = nullptr;

	ibConfigurationTree* m_metaWindow;
	class ibGitPanel* m_gitPanel = nullptr;

	ibOutputWindow* m_outputWindow;
	ibStackWindow* m_stackWindow;
	ibWatchWindow* m_watchWindow;
	ibLocalWindow* m_localWindow;
};
#endif 