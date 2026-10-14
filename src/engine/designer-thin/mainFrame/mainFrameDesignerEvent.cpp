////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the designer's events
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"

//********************************************************************************
//*                                 Debug commands                               *
//********************************************************************************

#include "frmclient/backend/appData.h"
#include "designer-thin/backend/debugger/debugClient.h"
#include "designer-thin/mainFrame/metaTree/treeConfiguration.h"

// Common shape for the three places that save the configuration before
// kicking off a child process (enterprise.exe / wenterprise-server.exe
// for debug). Returns true when the configuration is in a state safe to
// launch — either no save was needed, the user declined to save, or the
// save succeeded. Returns false only when a save was attempted and it
// failed (the server has said why — ibFrontendMainFrame::Call).
static bool SaveBeforeChildLaunch(wxWindow* parent, const ibConfigurationTree* metaWindow)
{
	if (metaWindow == nullptr || !metaWindow->IsEdited())
		return true;
	const int ans = wxMessageBox(
		wxString::Format(_("Configuration '%s' has been changed.\nDo you want to save?"),
			metaWindow->GetConfigName()),
		wxTheApp->GetAppDisplayName(),
		wxYES_NO | wxCENTRE | wxICON_QUESTION, parent);
	if (ans != wxYES)
		return true;

	return metaWindow->SaveDatabase();
}

void ibFrontendMainFrameDesigner::OnStartDebug(wxCommandEvent& WXUNUSED(event))
{
	if (debugClient->HasConnections()) {
		wxMessageBox(_("Debugger is already running!"));
		return;
	}

	if (!SaveBeforeChildLaunch(this, m_metaWindow))
		return;

	// …and the child's debug server looked for, as the desktop's RunApplication did with the debugger it held — once,
	// and not waited for: the connections keep trying on their own threads while the child boots (SearchServer(true)).
	if (appData->RunApplication(wxT("enterprise")) != 0)
		debugClient->SearchServer(true);
}

void ibFrontendMainFrameDesigner::OnStartDebugWithoutDebug(wxCommandEvent& WXUNUSED(event))
{
	if (!SaveBeforeChildLaunch(this, m_metaWindow))
		return;

	appData->RunApplication(wxT("enterprise"), false);
}

// THE THIN CLIENT, the same way: the same command line (a file base, or a server's connection), the same search for its
// debug server — a file base's engine runs in the thin client's own process (fileserver), and `--debug` brings it up there.
void ibFrontendMainFrameDesigner::OnStartDebugThin(wxCommandEvent& WXUNUSED(event))
{
	if (debugClient->HasConnections()) {
		wxMessageBox(_("Debugger is already running!"));
		return;
	}

	if (!SaveBeforeChildLaunch(this, m_metaWindow))
		return;

	if (appData->RunApplication(wxT("enterprise-thin")) != 0)
		debugClient->SearchServer(true);
}

void ibFrontendMainFrameDesigner::OnStartDebugWithoutDebugThin(wxCommandEvent& WXUNUSED(event))
{
	if (!SaveBeforeChildLaunch(this, m_metaWindow))
		return;

	appData->RunApplication(wxT("enterprise-thin"), false);
}

static void LaunchWebDebug(wxWindow* parent, bool withDebug)
{
	// URL prefix and port are derived by wes itself (from --file/--db
	// basename and OS-picked ephemeral port). Manifest handshake reports
	// the real URL back and opens the browser — no guessing here.
	if (appData->RunApplication(wxT("wenterprise-server"),
			/*searchDebug=*/withDebug, /*useManifest=*/true) == 0) {
		wxMessageBox(_("Failed to start wenterprise-server"),
			wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, parent);
	}
}

void ibFrontendMainFrameDesigner::OnStartDebugWeb(wxCommandEvent& WXUNUSED(event))
{
	if (debugClient->HasConnections()) {
		wxMessageBox(_("Debugger is already running!"));
		return;
	}
	if (!SaveBeforeChildLaunch(this, m_metaWindow))
		return;
	LaunchWebDebug(this, /*withDebug=*/true);
}

void ibFrontendMainFrameDesigner::OnStartDebugWithoutDebugWeb(wxCommandEvent& WXUNUSED(event))
{
	if (!SaveBeforeChildLaunch(this, m_metaWindow))
		return;
	LaunchWebDebug(this, /*withDebug=*/false);
}

#include "designer-thin/win/dlg/debugItem/debugItem.h"
#include "frmclient/window_ptr.h"

void ibFrontendMainFrameDesigner::OnAttachForDebugging(wxCommandEvent& WXUNUSED(event))
{
	ibWindowPtr<ibDialogDebugItem> dlg(new ibDialogDebugItem(this, wxID_ANY));
	dlg->Show();
}

//********************************************************************************
//*                                Configuration                                 *
//********************************************************************************

#include "core/fileKind.h"   // extensions live in one table, not at each call site

// ⭐ THE MENU REDIRECTS; THE WORK IS THE SERVER'S — the engine's verbs, which the assistant calls too (the doors of
// schema MetaData, through the configuration's tree). The open editors are the server's: what replaces the
// configuration takes them down there (its tree's Closed), as the desktop closed its MDI children first. And the
// message boxes the desktop showed when it was done the server asks — it alone knows how it went.

void ibFrontendMainFrameDesigner::OnRollbackConfiguration(wxCommandEvent& WXUNUSED(event))
{
	m_metaWindow->RollbackConfiguration();
}

void ibFrontendMainFrameDesigner::OnUpdateConfiguration(wxCommandEvent& WXUNUSED(event))
{
	// ⭐ WHAT STAYS HERE IS THE ASKING. The work is ApplyConfiguration, which the assistant calls
	// too — one road for the doing. What differs is only who answers the questions, and a person
	// answers them in dialogs.
	if (debugClient->HasConnections()) {
		if (wxMessageBox(
			_("To update the database configuration you need stop debugging.\nDo you want to continue?"), wxTheApp->GetAppDisplayName(),
			wxYES_NO | wxCENTRE | wxICON_INFORMATION, this) == wxNO) {
			return;
		}
		debugClient->Stop(true);
	}

	// ⚠ NOT ASKED HERE YET, as the desktop asked before the apply: who else is connected (the engine's own refusal says
	// it when the exclusive cannot be taken — the desktop's Try again / Update dynamically choice is not ported).

	// …and the last question is the ledger itself, which the server puts to the person (ShowRequest, ApplyChange).
	// Applied — the configuration no longer differs from the base's — the breakpoints are written at the lines they
	// moved to, as the desktop's modules wrote theirs on the apply (ibValueMetaObjectModuleBase::OnSaveMetaObject).
	if (m_metaWindow->ApplyConfiguration() && !m_metaWindow->IsModified() && debugClient != nullptr)
		debugClient->SaveAllBreakpoints();
}

void ibFrontendMainFrameDesigner::OnConfiguration(wxCommandEvent& event)
{
	if (wxID_DESIGNER_CONFIGURATION_LOAD_FROM_FILE == event.GetId())
	{
		wxFileDialog openFileDialog(this, _("Open configuration file"), "", "",
			ibFileFilter(ibFileKind::Application), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		// proceed loading the file chosen by the user; the tree reads what it became with the answer's frame
		if (m_metaWindow->LoadConfigFromFile(openFileDialog.GetPath())) {
			// A configuration just read from a FILE lives nowhere but in the server's session, which is what makes the
			// question worth asking here (ibMetaData::IsEdited).
			if (m_metaWindow->IsEdited()) {
				if (wxMessageBox(wxString::Format(_("Configuration '%s' has been changed.\nDo you want to save?"),
					m_metaWindow->GetConfigName()), wxTheApp->GetAppDisplayName(), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES) {
					OnUpdateConfiguration(event);
				}
			}
		}
	}
	else if (wxID_DESIGNER_CONFIGURATION_SAVE_TO_FILE == event.GetId())
	{
		wxFileDialog saveFileDialog(this, _("Save configuration file"), "", "",
			ibFileFilter(ibFileKind::Application), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (saveFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		if (m_metaWindow->SaveConfigToFile(saveFileDialog.GetPath())) {
			wxMessageBox(_("Successfully unloaded to: ") + saveFileDialog.GetPath());
		}
	}
}

void ibFrontendMainFrameDesigner::OnUpdateSaveMetadata(wxUpdateUIEvent& event)
{
	event.Enable(m_metaWindow != nullptr && m_metaWindow->IsModified());
}

void ibFrontendMainFrameDesigner::OnRunDebugCommand(wxCommandEvent& event)
{
	switch (event.GetId())
	{
	case wxID_DESIGNER_DEBUG_STEP_OVER:
		debugClient->StepOver();
		break;
	case wxID_DESIGNER_DEBUG_STEP_INTO:
		debugClient->StepInto();
		break;
	case wxID_DESIGNER_DEBUG_STEP_OUT:
		debugClient->StepOut();
		break;
	case wxID_DESIGNER_DEBUG_PAUSE:
		debugClient->Pause();
		break;
	case wxID_DESIGNER_DEBUG_STOP_DEBUGGING:
		debugClient->Stop(false);
		break;
	case wxID_DESIGNER_DEBUG_STOP_PROGRAM:
		debugClient->Stop(true);
		break;
	case wxID_DESIGNER_DEBUG_NEXT_POINT:
		debugClient->Continue();
		break;
	case wxID_DESIGNER_DEBUG_REMOVE_ALL_DEBUGPOINTS:
		debugClient->RemoveAllBreakpoint();
		break;
	}
}

// ⭐ THE DEBUG COMMANDS ASK THE CLIENT, AT THE MOMENT THEY ARE SHOWN. They used to be switched on and off by
// the debugger's events, and "the session ended" arrived while the connection it was about still counted as
// connected - so Pause, the steps and both stops stayed lit with nothing attached (2026-09-11). A state read
// when it is needed cannot run ahead of the thing it describes.
void ibFrontendMainFrameDesigner::OnUpdateDebugCommand(wxUpdateUIEvent& event)
{
	const bool attached = debugClient != nullptr && debugClient->HasConnections();
	const bool stopped  = attached && debugClient->IsEnterLoop();

	switch (event.GetId())
	{
	case wxID_DESIGNER_DEBUG_NEXT_POINT:           // continue - from a stop
		event.Enable(stopped);
		break;
	case wxID_DESIGNER_DEBUG_PAUSE:                // pause - a runtime that is going
		event.Enable(attached && !stopped);
		break;
	default:                                       // the steps and both stops - whenever something is attached
		event.Enable(attached);
		break;
	}
}

//********************************************************************************
//*                                    Tool                                      *
//********************************************************************************

#include "frmclient/backend/mcp/mcpServer.h"
#include "frmclient/mainFrame/settings/settingsdialog.h"
#include "frmclient/mainFrame/settings/keybinderdialog.h"
#include "frmclient/mainFrame/settings/fontcolorsettingspanel.h"
#include "frmclient/mainFrame/settings/editorsettingspanel.h"
#include "frmclient/mainFrame/settings/mcpsettingspanel.h"

#include "frmclient/win/dlgs/userList.h"

void ibFrontendMainFrameDesigner::ShowSchema(ibProtocolSchema schema, const ibProtocolNode& shown)
{
	// Administration → Users — the server's menu item (its UserList schema); the desktop's OnUsers as it is, the list
	// asked of the schema again by the dialog.
	if (schema == ibProtocolSchema::UserList) {
		ibWindowPtr<ibDialogUserList> dlg(new ibDialogUserList(this, wxID_ANY));
		dlg->Show();
		return;
	}

	// Tools → Options — the server's menu item (its McpServer schema); the dialog the desktop's OnToolsSettings
	// (designer/mainFrame/mainFrameDesignerEvent.cpp) as it is, its assistant access asked of the schema again.
	if (schema != ibProtocolSchema::McpServer) {
		ibFrontendMainFrame::ShowSchema(schema, shown);
		return;
	}

	ibDialogSettings dialog(this);

	ibDialogKeyBinder* keyBinder = dialog.GetKeyBinderDialog();

	for (unsigned int i = 0; i < m_keyBinder.GetNumCommands(); ++i)
	{
		keyBinder->AddCommand(m_keyBinder.GetCommand(i));
	}

	ibPanelFontColorSettings* fontColorSettings = dialog.GetFontColorSettingsPanel();
	fontColorSettings->SetSettings(m_fontColorSettings);

	ibPanelEditorSettings* editorSettings = dialog.GetEditorSettingsPanel();
	editorSettings->SetSettings(m_editorSettings);

	// ASSISTANT ACCESS. Unlike the pages above, this one is not the frame's own
	// state kept in an options file — it belongs to the PERSON in this base, so
	// it is read from and written back to the server that owns it.
	ibPanelMcpSettings* mcpSettings = dialog.GetMcpSettingsPanel();
	ibMcpServer* mcpServer = ibApplicationInstance::GetMcpServer();
	if (mcpServer != nullptr) {
		mcpSettings->SetSettings(mcpServer->GetSettings());
		mcpSettings->SetEndpoint(mcpServer->GetEndpoint());
	}

	if (dialog.ShowModal() == wxID_OK)
	{
		if (mcpServer != nullptr) {
			// Configure first, save second, both from the one value the page
			// holds — so what is written down and what is in force cannot differ.
			// A running server is NOT moved by this: it keeps the address it took
			// until somebody stops and starts it, which is the only way a page can
			// say one thing while a client talks to another and be caught at it.
			mcpServer->Configure(mcpSettings->GetSettings());
			mcpServer->SaveSettings(ibSession::Current());
		}

		m_keyBinder.ClearCommands();

		for (unsigned int i = 0; i < keyBinder->GetNumCommands(); ++i) {
			m_keyBinder.AddCommand(keyBinder->GetCommand(i));
		}

		m_keyBinder.UpdateWindow(this);

#if wxUSE_MENUBAR
		m_keyBinder.UpdateMenuBar(GetMenuBar());
#endif

		m_fontColorSettings = fontColorSettings->GetSettings();
		m_editorSettings = editorSettings->GetSettings();
	}

	SaveOptions();
}
