#ifndef _MAINFRAME_DESIGNER_H__
#define _MAINFRAME_DESIGNER_H__

#include <functional>

#include "frmclient/mainFrame/mainFrame.h"

#if defined(mainFrame)
#undef mainFrame
#endif

class ibConfigurationTree;
class ibDebuggerClient;
class ibStackWindow;
class ibWatchWindow;
class ibLocalWindow;

// The syntax helper's pane — the desktop's (frontend/mainFrame/mainFrame.h), the designer's alone here; its commands —
// the base's (the code editor posts them).
#define wxAUI_PANE_HELP     wxT("syntaxHelperWindow")
// …and the configuration's tree's.
#define wxAUI_PANE_METADATA wxT("metadataWindow")

// The desktop's (designer/mainFrame/mainFrameDesigner.h), by the same numbers: a person's keys are kept by them.
enum {

	wxID_DESIGNER_DEBUG_START = 10000,
	wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING,
	wxID_DESIGNER_DEBUG_START_WEB,
	wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING_WEB,
	wxID_DESIGNER_DEBUG_ATTACH_FOR_DEBUGGING,
	wxID_DESIGNER_DEBUG_EDIT_POINT,
	wxID_DESIGNER_DEBUG_STEP_OVER,
	wxID_DESIGNER_DEBUG_STEP_INTO,
	wxID_DESIGNER_DEBUG_STEP_OUT,
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

	wxID_DESIGNER_DEBUG_START_THIN,
	wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING_THIN,
	wxID_DESIGNER_END
};

// ⚠ A PERSON'S KEYS ARE KEPT BY THESE NUMBERS (ibKeyBinder::Save, the profile's "shortcuts"), so a command
// inserted in the middle moves every changed key after it onto another command — Step Out did (2026-09-12),
// and that profile was dropped for it. A new command goes LAST, before the end marker; these checks break the
// build on an insertion or a removal instead of moving somebody's keys. The numbers are the day the
// "shortcuts" profile began.
static_assert(wxID_DESIGNER_ABOUT == 10026, "append new designer commands before wxID_DESIGNER_END - the key profile keeps keys by number");

// …and the desktop's application commands, by the same numbers. Here only Connection is bound, where the desktop binds
// it (no menu item sends it, there either); the rest are the server's menu — Users, Options and the assistant.
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
// …and the same for these (see above): a new one goes last.
static_assert(wxID_APPLICATION_MCP_ASSISTANT == wxID_HIGHEST + 8, "append new application commands last - the key profile keeps keys by number");

#define mainFrame	(ibFrontendMainFrameDesigner::GetFrame())

// THE DESIGNER'S MAIN WINDOW — the desktop's ibFrontendMainFrameDesigner (designer/mainFrame) on the thin client.
// The base draws what both modes share — the menu, the tabs, the messages; this one logs in to the designer, whose
// configuration the server's session holds (ibClientFrameDesigner there).
class ibFrontendMainFrameDesigner : public ibFrontendMainFrame {
public:

	static ibFrontendMainFrameDesigner* GetFrame();

	// The window is built around a communicator, and owns it from that moment on; it logs in to the designer as it is
	// shown.
	ibFrontendMainFrameDesigner(std::unique_ptr<ibCommunicator> communicator,
		const wxString& user, const wxString& password,
		const wxString& title = _("Designer"),
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize);
	virtual ~ibFrontendMainFrameDesigner();

	// Shown — logged in — its debugger made over the configuration logged in to, and the person's assistant access
	// started with it when they switched it on (the desktop's).
	virtual bool Show(bool show = true) override;

	// The configuration's tree and the messages — the desktop's, where the debugger shows a stop and what was said.
	ibConfigurationTree* GetMetaWindow() const { return m_metaWindow; }
	ibOutputWindow* GetOutputWindow() const { return m_outputWindow; }

	// The debugger's panes along the bottom — the desktop's, beside the messages.
	ibStackWindow* GetStackWindow() const { return m_stackWindow; }
	ibWatchWindow* GetWatchWindow() const { return m_watchWindow; }
	ibLocalWindow* GetLocalWindow() const { return m_localWindow; }

	// The debugger's state changed (attached, detached, stopped, running): the Debug menu asks again
	// (OnUpdateDebugCommand) - it does not carry a state of its own.
	void Debugger_OnStateChanged();

	// …and the debugger's own connection, the window's doors to it (ibCommunicator::ListenDebug, Debug): the server's
	// debugger heard — `told` on that connection's thread — and asked, answered at once beside the window's call.
	bool ListenDebug(std::function<void(const ibProtocolNode& news)> told, ibProtocol::ibRefusal& refusal, wxString& error);
	bool Debug(const ibProtocolNode& params, ibProtocolNode& result, ibProtocol::ibRefusal& refusal, wxString& error);

protected:

	// The base's parts, and the desktop designer's around them (its CreateWideGui): the Update database button on the
	// main toolbar, the debug toolbar beside it, the configuration's tree on the left, version control's pane hidden.
	virtual void CreateGUI() override;
	// …and the notebook along the bottom — the messages, then the debugger's three (the desktop's).
	virtual void CreateBottomPane() override;

	// THE CONFIGURATION'S TREE — the frame's MetaData, drawn when the patch changed it; its state on the pane's caption.
	virtual void DrawFrame(const ibProtocolNode& frame, const ibProtocolNode& patch) override;

	// The designer's tabs — an external data processor's and report's tree — over the base's.
	virtual ibTabDocument* CreateDocument(ibProtocol::ibView::ibKind kind, long long tabId) override;

	// The debugger's store, read in the login — not a call of its own at the start.
	virtual std::vector<ibProtocol::ibSchema::ibKind> GetStartSchemas() const override;
	// The designer's Users and Options — the desktop's dialogs (Administration → Users, Tools → Options) — and the
	// debugger made over its store as the login read it, over the base's schemas.
	virtual void ShowSchema(ibProtocol::ibSchema::ibKind schema, const ibProtocolNode& shown) override;
	// …and the questions only the designer is asked — the restructure's ledger (the desktop's ibDialogApplyChange), the
	// kind of a form just made (ibDialogSelectTypeForm); the rest — the base's.
	virtual void ShowRequest(const ibProtocolNode& request, ibProtocolNode& response) override;

	// The designer's Configuration, Debug and Help — the desktop's — over the base's File and Edit.
	virtual void InitializeDefaultMenu() override;
	// …and their keys over the base's.
	virtual void SetDefaultHotKeys() override;

private:

	// The configuration's tree's pane — the desktop's.
	void CreateMetadataPane();
	// …and version control's, the desktop's: hidden, floating on the right, over the working copy beside the file base.
	void CreateGitPane();
	// The debug commands as buttons, beside the main toolbar. They are the Debug menu's own commands
	// (same ids, same handlers, same OnUpdateDebugCommand for enabling) - a button is a second way to
	// press a menu item, not a second implementation of it.
	void CreateDebugToolbar();

	// THE SYNTAX HELPER — the desktop's pane on the right, made the first time it is asked for.
	void EnsureHelpPane();
	void ToggleHelpPane();

	//events
	void OnStartDebug(wxCommandEvent& event);
	void OnStartDebugWithoutDebug(wxCommandEvent& event);
	void OnStartDebugWeb(wxCommandEvent& event);
	void OnStartDebugWithoutDebugWeb(wxCommandEvent& event);
	void OnStartDebugThin(wxCommandEvent& event);
	void OnStartDebugWithoutDebugThin(wxCommandEvent& event);
	void OnAttachForDebugging(wxCommandEvent& event);

	// The database's configuration, browsed in a tab of its own — the desktop's; ⚠ not in the thin yet, said so.
	void OnOpenConfiguration(wxCommandEvent& event);
	void OnRollbackConfiguration(wxCommandEvent& event);
	void OnUpdateConfiguration(wxCommandEvent& event);
	void OnConfiguration(wxCommandEvent& event);
	// Rollback and Update — lit while the configuration differs from the base's (the desktop's ibDocManagerDesigner's).
	void OnUpdateSaveMetadata(wxUpdateUIEvent& event);

	void OnRunDebugCommand(wxCommandEvent& event);
	void OnUpdateDebugCommand(wxUpdateUIEvent& event);
	// The connection's dialog — the desktop's (ibDialogConnection).
	void OnConnection(wxCommandEvent& event);

	ibConfigurationTree* m_metaWindow = nullptr;
	class ibGitPanel* m_gitPanel = nullptr;
	class ibHelpPaneView* m_helpPane = nullptr;

	// THE DEBUGGER — the engine's client, the designer's own (designer-thin/backend/debugger): its sockets go straight to
	// the runtimes; its breakpoints are kept by the server. Made when the frame logged in, gone with the frame.
	std::unique_ptr<ibDebuggerClient> m_debugClient;

	wxAuiToolBar* m_debugToolbar = nullptr;

	ibStackWindow* m_stackWindow = nullptr;
	ibWatchWindow* m_watchWindow = nullptr;
	ibLocalWindow* m_localWindow = nullptr;
};

#endif
