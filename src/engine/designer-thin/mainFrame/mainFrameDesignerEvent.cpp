////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the designer's events
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"

#include <vector>
#include <wx/filename.h>
#include <wx/filedlg.h>
#include <wx/dirdlg.h>
#include <wx/msgdlg.h>

//********************************************************************************
//*                                 Debug commands                               *
//********************************************************************************

#include "frmclient/backend/appData.h"
#include "designer-thin/backend/debugger/debugClient.h"
#include "designer-thin/mainFrame/metaTree/treeConfiguration.h"
#include "designer-thin/backend/metaCollection/metaDiff.h"
#include "designer-thin/docManager/templates/docViewConfigCompare.h"
#include "core/exception.h"   // ibCoreException — a directory of files refused on this computer, said here

// Common shape for the three places that save the configuration before
// kicking off a child process (enterprise-thin.exe / webserver.exe
// for debug). Returns true when the configuration is in a state safe to
// launch — either no save was needed, the user declined to save, or the
// save succeeded. Returns false only when a save was attempted and it
// failed (the server has said why — ibFrontendMainFrame::Call).
static bool SaveBeforeChildLaunch(wxWindow* parent, const ibConfigurationTree* metaWindow)
{
	if (metaWindow == nullptr || !metaWindow->IsEdited())
		return true;
	const int ans = wxMessageBox(
		ibString::Format(_("Configuration '%s' has been changed.\nDo you want to save?"),
			metaWindow->GetConfigName()),
		wxTheApp->GetAppDisplayName(),
		wxYES_NO | wxCENTRE | wxICON_QUESTION, parent);
	if (ans != wxYES)
		return true;

	return metaWindow->SaveDatabase();
}

// THE THIN CLIENT, with the designer's command line (a file base, or a server's connection). A file base's engine runs
// in the thin client's own process (fileserver), and `--debug` brings its debug server up there.
void ibFrontendMainFrameDesigner::OnStartDebug(wxCommandEvent& WXUNUSED(event))
{
	if (debugClient->HasConnections()) {
		wxMessageBox(_("Debugger is already running!"));
		return;
	}

	if (!SaveBeforeChildLaunch(this, m_metaWindow))
		return;

	// …and the child's debug server looked for once, and not waited for: the connections keep trying on their own
	// threads while the child boots (SearchServer(true)).
	if (appData->RunApplication(wxT("enterprise-thin")) != 0)
		debugClient->SearchServer(true);
}

void ibFrontendMainFrameDesigner::OnStartDebugWithoutDebug(wxCommandEvent& WXUNUSED(event))
{
	if (!SaveBeforeChildLaunch(this, m_metaWindow))
		return;

	appData->RunApplication(wxT("enterprise-thin"), false);
}

static void LaunchWebDebug(wxWindow* parent, bool withDebug)
{
	// The publication's name and the port are the web server's own (the base's folder or its name on the server, a
	// port the system picks); the manifest reports the page's address back and the browser opens it. Its debug server
	// is looked for as the thin client's is.
	if (appData->RunApplication(wxT("webserver"),
			/*searchDebug=*/withDebug, /*useManifest=*/true) == 0) {
		wxMessageBox(_("Failed to start webserver"),
			wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, parent);
	}
	else if (withDebug) {
		debugClient->SearchServer(true);
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

void ibFrontendMainFrameDesigner::OnOpenConfiguration(wxCommandEvent& WXUNUSED(event))
{
	// The desktop's raised the browser already open on the database's configuration, else opened one — its tree read-only
	// in a tab (ibMetadataBrowserDocument). The database's configuration is the server's: its command does both, and the
	// tab comes with the frame (ibView::ibKind::Configuration).
	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::MetaData));
	params.SetValue(ibProtocol::ibSchema::Command,
		static_cast<long long>(ibProtocol::ibSchema::ibMetaData::ibCommand::Browse));
	Call(ibProtocol::ibMethod::Schema, params, answer);
}

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


namespace {

// Records from schema ConfigCompare Build → ibMetaDiffRecord vector.
std::vector<ibMetaDiffRecord> ReadCompareRecords(const ibProtocolNode& answer)
{
	std::vector<ibMetaDiffRecord> records;
	for (const ibProtocolNode& row : answer.FindChild(ibProtocol::ibSchema::ibConfigCompare::Records).Children()) {
		ibMetaDiffRecord rec;
		rec.m_status = static_cast<ibMetaDiffStatus>(row.GetInt(ibProtocol::ibSchema::ibConfigCompare::Status));
		rec.m_parentIndex = static_cast<int>(row.GetInt(ibProtocol::ibSchema::ibConfigCompare::Parent, -1));
		rec.m_depth = static_cast<int>(row.GetInt(ibProtocol::ibSchema::ibConfigCompare::Depth));
		const wxString group = row.GetString(ibProtocol::ibSchema::ibConfigCompare::Group);
		if (!group.IsEmpty()) {
			unsigned long long clsid = 0;
			group.ToULongLong(&clsid);
			rec.m_groupClsid = static_cast<ibClassID>(clsid);
		}
		rec.m_propertyName = row.GetString(ibProtocol::ibSchema::ibConfigCompare::Property);
		rec.m_leftValue = row.GetString(ibProtocol::ibSchema::ibConfigCompare::Left);
		rec.m_rightValue = row.GetString(ibProtocol::ibSchema::ibConfigCompare::Right);
		rec.m_name = row.GetString(ibProtocol::ibSchema::ibConfigCompare::Name);
		const wxString picture = row.GetString(ibProtocol::ibSchema::ibConfigCompare::Picture);
		if (!picture.IsEmpty()) {
			unsigned long long clsid = 0;
			picture.ToULongLong(&clsid);
			rec.m_iconClsid = static_cast<ibClassID>(clsid);
		}
		rec.m_leftId = static_cast<ibMetaID>(row.GetInt(ibProtocol::ibSchema::ibConfigCompare::LeftId));
		rec.m_rightId = static_cast<ibMetaID>(row.GetInt(ibProtocol::ibSchema::ibConfigCompare::RightId));
		records.push_back(std::move(rec));
	}
	return records;
}

bool CallCompareBuild(ibProtocol::ibSchema::ibConfigCompare::ibMode mode,
	const wxString& fileId, const wxString& file2Id, ibProtocolNode& answer)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibConfigCompare::Mode, static_cast<int>(mode));
	if (!fileId.IsEmpty())
		args.SetValue(ibProtocol::ibSchema::ibConfigCompare::File, fileId);
	if (!file2Id.IsEmpty())
		args.SetValue(ibProtocol::ibSchema::ibConfigCompare::File2, file2Id);
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibSchema::Schema,
		static_cast<long long>(ibProtocol::ibSchema::ibKind::ConfigCompare));
	params.SetValue(ibProtocol::ibSchema::Args, args);
	return frame->Call(ibProtocol::ibMethod::Schema, params, answer);
}

void OpenCompareTab(ibFrontendDocManager* manager,
	std::vector<ibMetaDiffRecord> records,
	const wxString& leftLabel, const wxString& rightLabel,
	ibProtocol::ibSchema::ibConfigCompare::ibMode mode,
	const wxString& fileId, const wxString& file2Id,
	const wxString& localRightPath)
{
	if (manager == nullptr)
		return;
	auto* doc = new ibConfigCompareDocument();
	manager->AddDocument(doc);
	doc->Configure(std::move(records), leftLabel, rightLabel, mode, fileId, file2Id, localRightPath,
		{});  // Merge Call redraws MetaData in the frame; no separate tree reload.
	if (!doc->OnCreate(wxEmptyString, 0))
		doc->DeleteAllViews();
}

} // namespace

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
				if (wxMessageBox(ibString::Format(_("Configuration '%s' has been changed.\nDo you want to save?"),
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
	// …and as a directory of files: read and written on this computer, so what refuses here (a directory that is no
	// export, or holds other files) is said here.
	else if (wxID_DESIGNER_CONFIGURATION_LOAD_FROM_FILES == event.GetId())
	{
		wxDirDialog dirDialog(this, _("Choose the directory the configuration was saved to"), "",
			wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
		if (dirDialog.ShowModal() == wxID_CANCEL)
			return;

		try {
			if (m_metaWindow->LoadConfigFromFiles(dirDialog.GetPath()) && m_metaWindow->IsEdited()) {
				if (wxMessageBox(ibString::Format(_("Configuration '%s' has been changed.\nDo you want to save?"),
					m_metaWindow->GetConfigName()), wxTheApp->GetAppDisplayName(), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES) {
					OnUpdateConfiguration(event);
				}
			}
		}
		catch (const ibCoreException& err) {
			wxMessageBox(err.GetErrorDescription(), wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
		}
	}
	else if (wxID_DESIGNER_CONFIGURATION_SAVE_TO_FILES == event.GetId())
	{
		wxDirDialog dirDialog(this, _("Choose a directory of its own for the configuration's files"), "", wxDD_DEFAULT_STYLE);
		if (dirDialog.ShowModal() == wxID_CANCEL)
			return;

		try {
			if (m_metaWindow->SaveConfigToFiles(dirDialog.GetPath()))
				wxMessageBox(_("Successfully unloaded to: ") + dirDialog.GetPath());
		}
		catch (const ibCoreException& err) {
			wxMessageBox(err.GetErrorDescription(), wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
		}
	}
	else if (wxID_DESIGNER_CONFIGURATION_COMPARE_FILE == event.GetId())
	{
		wxFileDialog openFileDialog(this, _("Choose configuration file to compare with"),
			"", "",
			ibFileFilter(ibFileKind::Application),
			wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;

		ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
		wxString fileId;
		if (frame == nullptr || !frame->UploadFile(openFileDialog.GetPath(), fileId)) {
			wxMessageBox(_("Failed to upload configuration file."),
				wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
			return;
		}
		ibProtocolNode answer;
		if (!CallCompareBuild(ibProtocol::ibSchema::ibConfigCompare::ibMode::File, fileId, wxString(), answer)) {
			wxMessageBox(_("Failed to compare with the configuration file."),
				wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
			return;
		}
		OpenCompareTab(m_docManager, ReadCompareRecords(answer), _("Current"),
			wxFileName(openFileDialog.GetPath()).GetFullName(),
			ibProtocol::ibSchema::ibConfigCompare::ibMode::File,
			fileId, wxString(), openFileDialog.GetPath());
	}
	else if (wxID_DESIGNER_CONFIGURATION_COMPARE_DB == event.GetId())
	{
		ibProtocolNode answer;
		if (!CallCompareBuild(ibProtocol::ibSchema::ibConfigCompare::ibMode::Database,
				wxString(), wxString(), answer)) {
			wxMessageBox(_("Database configuration is not available."),
				wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
			return;
		}
		// No Push — same as the desktop (no right-save callback).
		OpenCompareTab(m_docManager, ReadCompareRecords(answer), _("Current"), _("Database"),
			ibProtocol::ibSchema::ibConfigCompare::ibMode::Database,
			wxString(), wxString(), wxString());
	}
	else if (wxID_DESIGNER_CONFIGURATION_COMPARE_TWO_FILES == event.GetId())
	{
		wxFileDialog dlgA(this, _("Choose left configuration file"),
			"", "", ibFileFilter(ibFileKind::Application),
			wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (dlgA.ShowModal() == wxID_CANCEL)
			return;
		wxFileDialog dlgB(this, _("Choose right configuration file"),
			"", "", ibFileFilter(ibFileKind::Application),
			wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (dlgB.ShowModal() == wxID_CANCEL)
			return;

		ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
		wxString fileId, file2Id;
		if (frame == nullptr
			|| !frame->UploadFile(dlgA.GetPath(), fileId)
			|| !frame->UploadFile(dlgB.GetPath(), file2Id)) {
			wxMessageBox(_("Failed to upload one of the configuration files."),
				wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
			return;
		}
		ibProtocolNode answer;
		if (!CallCompareBuild(ibProtocol::ibSchema::ibConfigCompare::ibMode::TwoFiles,
				fileId, file2Id, answer)) {
			wxMessageBox(_("Failed to load one of the configuration files."),
				wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
			return;
		}
		OpenCompareTab(m_docManager, ReadCompareRecords(answer),
			wxFileName(dlgA.GetPath()).GetFullName(),
			wxFileName(dlgB.GetPath()).GetFullName(),
			ibProtocol::ibSchema::ibConfigCompare::ibMode::TwoFiles,
			fileId, file2Id, dlgB.GetPath());
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
#include "frmclient/mainFrame/settings/keybinderpanel.h"
#include "frmclient/mainFrame/settings/fontcolorsettingspanel.h"
#include "frmclient/mainFrame/settings/editorsettingspanel.h"
#include "frmclient/mainFrame/settings/mcpsettingspanel.h"
#include "frmclient/mainFrame/output/outputWindow.h"

#include "frmclient/win/dlgs/userList.h"

void ibFrontendMainFrameDesigner::ShowSchema(ibProtocol::ibSchema::ibKind schema, const ibProtocolNode& shown)
{
	// The debugger's store, read in the login (GetStartSchemas) — the debugger made over it, nothing asked again.
	if (schema == ibProtocol::ibSchema::ibKind::Debugger) {
		if (m_debugClient == nullptr)
			m_debugClient.reset(new ibDebuggerClient(shown));
		return;
	}

	// Administration → Users — the server's menu item (its UserList schema); the desktop's OnUsers as it is, the list
	// asked of the schema again by the dialog.
	if (schema == ibProtocol::ibSchema::ibKind::UserList) {
		ibWindowPtr<ibDialogUserList> dlg(new ibDialogUserList(this, wxID_ANY));
		dlg->Show();
		return;
	}

	// Tools → Options — the server's menu item (its McpServer schema); the dialog the desktop's OnToolsSettings
	// (designer/mainFrame/mainFrameDesignerEvent.cpp) as it is, its assistant access asked of the schema again.
	if (schema != ibProtocol::ibSchema::ibKind::McpServer) {
		ibFrontendMainFrame::ShowSchema(schema, shown);
		return;
	}

	// The pages edit copies; the frame takes them back only on OK.
	ibDialogSettings dialog(this);
	dialog.GetKeyBinderPanel()->SetSettings(m_keyBinder);
	dialog.GetFontColorSettingsPanel()->SetSettings(m_fontColorSettings);
	dialog.GetEditorSettingsPanel()->SetSettings(m_editorSettings);

	// ASSISTANT ACCESS. Unlike the pages above, this one is not the frame's own
	// state kept in an options file — it belongs to the PERSON in this base, so
	// it is read from and written back to the server that owns it.
	// The menu item's answer is this schema's (McpServer): Allowed, the settings, Running, the Endpoint — the page reads
	// them from it, and asks the server nothing more to open.
	ibPanelMcpSettings* mcpSettings = dialog.GetMcpSettingsPanel();
	ibMcpServer* mcpServer = ibApplicationInstance::GetMcpServer(shown);
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

		m_keyBinder = dialog.GetKeyBinderPanel()->GetSettings();
		m_keyBinder.UpdateWindow(this);
#if wxUSE_MENUBAR
		m_keyBinder.UpdateMenuBar(GetMenuBar());
#endif

		m_fontColorSettings = dialog.GetFontColorSettingsPanel()->GetSettings();
		m_editorSettings = dialog.GetEditorSettingsPanel()->GetSettings();

		// …and in force at once, as the desktop's UpdateEditorOptions put them: every document's views told (a module's
		// sets its editor anew, a form's its Module page), and the output window.
		for (ibFrontendDocument* const doc : m_docManager->GetDocumentsVector())
			doc->UpdateAllViews();
		if (m_outputWindow != nullptr)
			m_outputWindow->SetFontColorSettings(m_fontColorSettings);
	}

	SaveOptions();
}

#include "frmclient/win/dlgs/connectionDB.h"

void ibFrontendMainFrameDesigner::OnConnection(wxCommandEvent& WXUNUSED(event))
{
	ibDialogConnection dlg(this, wxID_ANY);
	dlg.ShowModal();
}
