////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the designer's events
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"

#include <vector>
#include <wx/filename.h>
#include <wx/filedlg.h>
#include <wx/dirdlg.h>
#include "deskgui/univ/messageDialog.h"

//********************************************************************************
//*                                 Debug commands                               *
//********************************************************************************

#include "frmclient/backend/appData.h"
#include "appstudio/backend/debugger/debugClient.h"
#include "appstudio/mainFrame/metaTree/treeApplication.h"
#include "appstudio/backend/metaCollection/metaDiff.h"
#include "appstudio/docManager/templates/docViewApplicationCompare.h"
#include "core/exception.h"   // ibCoreException — a directory of files refused on this computer, said here
#include "protocol/protocolDesigner.h"   // ibProtocol::ibSchema::ibMetaData, ibProtocol::ibSchema::ibApplicationCompare

// Common shape for the three places that save the application before
// kicking off a child process (appworkspace.exe / webserver.exe
// for debug). Returns true when the application is in a state safe to
// launch — either no save was needed, the user declined to save, or the
// save succeeded. Returns false only when a save was attempted and it
// failed (the server has said why — ibFrontendMainFrame::Call).
static bool SaveBeforeChildLaunch(wxWindow* parent, const ibApplicationTree* metaWindow)
{
	if (metaWindow == nullptr || !metaWindow->IsEdited())
		return true;
	const int ans = ibMessageBox(
		ibString::Format(_("Application '%s' has been changed.\nDo you want to save?"),
			metaWindow->GetApplicationName()),
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
		ibMessageBox(_("Debugger is already running!"));
		return;
	}

	if (!SaveBeforeChildLaunch(this, m_metaWindow))
		return;

	// …and the child's debug server looked for once, and not waited for: the connections keep trying on their own
	// threads while the child boots (SearchServer(true)).
	if (appData->RunApplication(wxT("appworkspace")) != 0)
		debugClient->SearchServer(true);
}

void ibFrontendMainFrameDesigner::OnStartDebugWithoutDebug(wxCommandEvent& WXUNUSED(event))
{
	if (!SaveBeforeChildLaunch(this, m_metaWindow))
		return;

	appData->RunApplication(wxT("appworkspace"), false);
}

static void LaunchWebDebug(wxWindow* parent, bool withDebug)
{
	// The publication's name and the port are the web server's own (the base's folder or its name on the server, a
	// port the system picks); the manifest reports the page's address back and the browser opens it. Its debug server
	// is looked for as the thin client's is.
	if (appData->RunApplication(wxT("webserver"),
			/*searchDebug=*/withDebug, /*useManifest=*/true) == 0) {
		ibMessageBox(_("Failed to start webserver"),
			wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, parent);
	}
	else if (withDebug) {
		debugClient->SearchServer(true);
	}
}

void ibFrontendMainFrameDesigner::OnStartDebugWeb(wxCommandEvent& WXUNUSED(event))
{
	if (debugClient->HasConnections()) {
		ibMessageBox(_("Debugger is already running!"));
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

#include "appstudio/win/dlg/debugItem/debugItem.h"
#include "frmclient/window_ptr.h"

void ibFrontendMainFrameDesigner::OnAttachForDebugging(wxCommandEvent& WXUNUSED(event))
{
	ibWindowPtr<ibDialogDebugItem> dlg(new ibDialogDebugItem(this, wxID_ANY));
	dlg->Show();
}

//********************************************************************************
//*                                Application                                 *
//********************************************************************************

#include "core/fileKind.h"   // extensions live in one table, not at each call site

// ⭐ THE MENU REDIRECTS; THE WORK IS THE SERVER'S — the engine's verbs, which the assistant calls too (the doors of
// schema MetaData, through the application's tree). The open editors are the server's: what replaces the
// application takes them down there (its tree's Closed), as the desktop closed its MDI children first. And the
// message boxes the desktop showed when it was done the server asks — it alone knows how it went.

void ibFrontendMainFrameDesigner::OnOpenApplication(wxCommandEvent& WXUNUSED(event))
{
	// The desktop's raised the browser already open on the database's application, else opened one — its tree read-only
	// in a tab (ibMetadataBrowserDocument). The database's application is the server's: its command does both, and the
	// tab comes with the frame (ibView::ibKind::Application).
	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::MetaData));
	params.SetValue(ibProtocol::ibSchema::Command,
		static_cast<long long>(ibProtocol::ibSchema::ibMetaData::ibCommand::Browse));
	Call(ibProtocol::ibMethod::Schema, params, answer);
}

void ibFrontendMainFrameDesigner::OnRollbackApplication(wxCommandEvent& WXUNUSED(event))
{
	m_metaWindow->RollbackApplication();
}

void ibFrontendMainFrameDesigner::OnUpdateApplication(wxCommandEvent& WXUNUSED(event))
{
	// ⭐ WHAT STAYS HERE IS THE ASKING. The work is ApplyApplication, which the assistant calls
	// too — one road for the doing. What differs is only who answers the questions, and a person
	// answers them in dialogs.
	if (debugClient->HasConnections()) {
		if (ibMessageBox(
			_("To update the database you need to stop debugging.\nDo you want to continue?"), wxTheApp->GetAppDisplayName(),
			wxYES_NO | wxCENTRE | wxICON_INFORMATION, this) == wxNO) {
			return;
		}
		debugClient->Stop(true);
	}

	// ⚠ NOT ASKED HERE YET, as the desktop asked before the apply: who else is connected (the engine's own refusal says
	// it when the exclusive cannot be taken — the desktop's Try again / Update dynamically choice is not ported).

	// …and the last question is the ledger itself, which the server puts to the person (ShowRequest, ApplyChange).
	// The breakpoints go where the edits moved them with each module the apply saves — in the server's debugger, its one
	// map (ibValueMetaObjectModuleBase::OnSaveMetaObject), as at an external file's save: nothing here.
	m_metaWindow->ApplyApplication();
}


namespace {

// Records from schema ApplicationCompare Build → ibMetaDiffRecord vector.
std::vector<ibMetaDiffRecord> ReadCompareRecords(const ibProtocolNode& answer)
{
	std::vector<ibMetaDiffRecord> records;
	for (const ibProtocolNode& row : answer.FindChild(ibProtocol::ibSchema::ibApplicationCompare::Records).Children()) {
		ibMetaDiffRecord rec;
		rec.m_status = static_cast<ibMetaDiffStatus>(row.GetInt(ibProtocol::ibSchema::ibApplicationCompare::Status));
		rec.m_parentIndex = static_cast<int>(row.GetInt(ibProtocol::ibSchema::ibApplicationCompare::Parent, -1));
		rec.m_depth = static_cast<int>(row.GetInt(ibProtocol::ibSchema::ibApplicationCompare::Depth));
		const wxString group = row.GetString(ibProtocol::ibSchema::ibApplicationCompare::Group);
		if (!group.IsEmpty()) {
			unsigned long long clsid = 0;
			group.ToULongLong(&clsid);
			rec.m_groupClsid = static_cast<ibClassID>(clsid);
		}
		rec.m_propertyName = row.GetString(ibProtocol::ibSchema::ibApplicationCompare::Property);
		rec.m_leftValue = row.GetString(ibProtocol::ibSchema::ibApplicationCompare::Left);
		rec.m_rightValue = row.GetString(ibProtocol::ibSchema::ibApplicationCompare::Right);
		rec.m_name = row.GetString(ibProtocol::ibSchema::ibApplicationCompare::Name);
		const wxString picture = row.GetString(ibProtocol::ibSchema::ibApplicationCompare::Picture);
		if (!picture.IsEmpty()) {
			unsigned long long clsid = 0;
			picture.ToULongLong(&clsid);
			rec.m_iconClsid = static_cast<ibClassID>(clsid);
		}
		rec.m_leftId = static_cast<ibMetaID>(row.GetInt(ibProtocol::ibSchema::ibApplicationCompare::LeftId));
		rec.m_rightId = static_cast<ibMetaID>(row.GetInt(ibProtocol::ibSchema::ibApplicationCompare::RightId));
		records.push_back(std::move(rec));
	}
	return records;
}

bool CallCompareBuild(ibProtocol::ibSchema::ibApplicationCompare::ibMode mode,
	const wxString& fileId, const wxString& file2Id, ibProtocolNode& answer)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibApplicationCompare::Mode, static_cast<int>(mode));
	if (!fileId.IsEmpty())
		args.SetValue(ibProtocol::ibSchema::ibApplicationCompare::File, fileId);
	if (!file2Id.IsEmpty())
		args.SetValue(ibProtocol::ibSchema::ibApplicationCompare::File2, file2Id);
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibSchema::Schema,
		static_cast<long long>(ibProtocol::ibSchema::ibKind::ApplicationCompare));
	params.SetValue(ibProtocol::ibSchema::Args, args);
	return frame->Call(ibProtocol::ibMethod::Schema, params, answer);
}

void OpenCompareTab(ibFrontendDocManager* manager,
	std::vector<ibMetaDiffRecord> records,
	const wxString& leftLabel, const wxString& rightLabel,
	ibProtocol::ibSchema::ibApplicationCompare::ibMode mode,
	const wxString& fileId, const wxString& file2Id,
	const wxString& localRightPath)
{
	if (manager == nullptr)
		return;
	auto* doc = new ibApplicationCompareDocument();
	manager->AddDocument(doc);
	doc->Configure(std::move(records), leftLabel, rightLabel, mode, fileId, file2Id, localRightPath,
		{});  // Merge Call redraws MetaData in the frame; no separate tree reload.
	if (!doc->OnCreate(wxEmptyString, 0))
		doc->DeleteAllViews();
}

} // namespace

void ibFrontendMainFrameDesigner::OnApplication(wxCommandEvent& event)
{
	if (wxID_DESIGNER_APPLICATION_LOAD_FROM_FILE == event.GetId())
	{
		wxFileDialog openFileDialog(this, _("Open application file"), "", "",
			ibFileFilter(ibFileKind::Application), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		// proceed loading the file chosen by the user; the tree reads what it became with the answer's frame
		if (m_metaWindow->LoadApplicationFromFile(openFileDialog.GetPath())) {
			// An application just read from a FILE lives nowhere but in the server's session, which is what makes the
			// question worth asking here (ibMetaData::IsEdited).
			if (m_metaWindow->IsEdited()) {
				if (ibMessageBox(ibString::Format(_("Application '%s' has been changed.\nDo you want to save?"),
					m_metaWindow->GetApplicationName()), wxTheApp->GetAppDisplayName(), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES) {
					OnUpdateApplication(event);
				}
			}
		}
	}
	else if (wxID_DESIGNER_APPLICATION_SAVE_TO_FILE == event.GetId())
	{
		wxFileDialog saveFileDialog(this, _("Save application file"), "", "",
			ibFileFilter(ibFileKind::Application), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (saveFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		if (m_metaWindow->SaveApplicationToFile(saveFileDialog.GetPath())) {
			ibMessageBox(_("Successfully unloaded to: ") + saveFileDialog.GetPath());
		}
	}
	// …and as a directory of files: read and written on this computer, so what refuses here (a directory that is no
	// export, or holds other files) is said here.
	else if (wxID_DESIGNER_APPLICATION_LOAD_FROM_FILES == event.GetId())
	{
		wxDirDialog dirDialog(this, _("Choose the directory the application was saved to"), "",
			wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
		if (dirDialog.ShowModal() == wxID_CANCEL)
			return;

		try {
			if (m_metaWindow->LoadApplicationFromFiles(dirDialog.GetPath()) && m_metaWindow->IsEdited()) {
				if (ibMessageBox(ibString::Format(_("Application '%s' has been changed.\nDo you want to save?"),
					m_metaWindow->GetApplicationName()), wxTheApp->GetAppDisplayName(), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES) {
					OnUpdateApplication(event);
				}
			}
		}
		catch (const ibCoreException& err) {
			ibMessageBox(err.GetErrorDescription(), wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
		}
	}
	else if (wxID_DESIGNER_APPLICATION_SAVE_TO_FILES == event.GetId())
	{
		wxDirDialog dirDialog(this, _("Choose a directory of its own for the application's files"), "", wxDD_DEFAULT_STYLE);
		if (dirDialog.ShowModal() == wxID_CANCEL)
			return;

		try {
			if (m_metaWindow->SaveApplicationToFiles(dirDialog.GetPath()))
				ibMessageBox(_("Successfully unloaded to: ") + dirDialog.GetPath());
		}
		catch (const ibCoreException& err) {
			ibMessageBox(err.GetErrorDescription(), wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
		}
	}
	else if (wxID_DESIGNER_APPLICATION_COMPARE_FILE == event.GetId())
	{
		wxFileDialog openFileDialog(this, _("Choose application file to compare with"),
			"", "",
			ibFileFilter(ibFileKind::Application),
			wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;

		ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
		wxString fileId;
		if (frame == nullptr || !frame->UploadFile(openFileDialog.GetPath(), fileId)) {
			ibMessageBox(_("Failed to upload application file."),
				wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
			return;
		}
		ibProtocolNode answer;
		if (!CallCompareBuild(ibProtocol::ibSchema::ibApplicationCompare::ibMode::File, fileId, wxString(), answer)) {
			ibMessageBox(_("Failed to compare with the application file."),
				wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
			return;
		}
		OpenCompareTab(m_docManager, ReadCompareRecords(answer), _("Current"),
			wxFileName(openFileDialog.GetPath()).GetFullName(),
			ibProtocol::ibSchema::ibApplicationCompare::ibMode::File,
			fileId, wxString(), openFileDialog.GetPath());
	}
	else if (wxID_DESIGNER_APPLICATION_COMPARE_DB == event.GetId())
	{
		ibProtocolNode answer;
		if (!CallCompareBuild(ibProtocol::ibSchema::ibApplicationCompare::ibMode::Database,
				wxString(), wxString(), answer)) {
			ibMessageBox(_("The database's application is not available."),
				wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
			return;
		}
		// No Push — same as the desktop (no right-save callback).
		OpenCompareTab(m_docManager, ReadCompareRecords(answer), _("Current"), _("Database"),
			ibProtocol::ibSchema::ibApplicationCompare::ibMode::Database,
			wxString(), wxString(), wxString());
	}
	else if (wxID_DESIGNER_APPLICATION_COMPARE_TWO_FILES == event.GetId())
	{
		wxFileDialog dlgA(this, _("Choose left application file"),
			"", "", ibFileFilter(ibFileKind::Application),
			wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (dlgA.ShowModal() == wxID_CANCEL)
			return;
		wxFileDialog dlgB(this, _("Choose right application file"),
			"", "", ibFileFilter(ibFileKind::Application),
			wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (dlgB.ShowModal() == wxID_CANCEL)
			return;

		ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
		wxString fileId, file2Id;
		if (frame == nullptr
			|| !frame->UploadFile(dlgA.GetPath(), fileId)
			|| !frame->UploadFile(dlgB.GetPath(), file2Id)) {
			ibMessageBox(_("Failed to upload one of the application files."),
				wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
			return;
		}
		ibProtocolNode answer;
		if (!CallCompareBuild(ibProtocol::ibSchema::ibApplicationCompare::ibMode::TwoFiles,
				fileId, file2Id, answer)) {
			ibMessageBox(_("Failed to load one of the application files."),
				wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
			return;
		}
		OpenCompareTab(m_docManager, ReadCompareRecords(answer),
			wxFileName(dlgA.GetPath()).GetFullName(),
			wxFileName(dlgB.GetPath()).GetFullName(),
			ibProtocol::ibSchema::ibApplicationCompare::ibMode::TwoFiles,
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
	case wxID_DESIGNER_DEBUG_PROFILE:              // its end opens what it measured, a tab the server raises
		if (debugClient->IsProfiling())
			debugClient->StopProfile();
		else
			debugClient->StartProfile();
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
	case wxID_DESIGNER_DEBUG_PROFILE:              // the measurement - of a runtime attached, checked while it runs
		event.Enable(attached);
		event.Check(attached && debugClient->IsProfiling());
		break;
	default:                                       // the steps and both stops - whenever something is attached
		event.Enable(attached);
		break;
	}
}

//********************************************************************************
//*                                    Tool                                      *
//********************************************************************************

#include "frmclient/win/dlgs/userList.h"
#include "appstudio/win/dlg/webAddress.h"

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

	// …and Web addresses — what the base answers on a web server: the designer's alone, as its schema is.
	if (schema == ibProtocol::ibSchema::ibKind::WebAddress) {
		ibDialogWebAddress dialog(this, shown);
		dialog.ShowModal();
		return;
	}

	ibFrontendMainFrame::ShowSchema(schema, shown);
}

#include "frmclient/win/dlgs/connectionDB.h"

void ibFrontendMainFrameDesigner::OnConnection(wxCommandEvent& WXUNUSED(event))
{
	ibDialogConnection dlg(this, wxID_ANY);
	dlg.ShowModal();
}
