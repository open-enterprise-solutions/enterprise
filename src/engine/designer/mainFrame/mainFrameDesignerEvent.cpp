////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/debugger/debugClient.h"
#include "backend/session/sessionRegistry.h"

#include "docManager/templates/docViewConfigCompare.h"

#include <wx/filename.h>

namespace {
// Show a backend-level error chain in a single message box. Used by
// designer save / load / structure-update paths after a backend
// operation throws — DrainLastErrors() consumes the per-thread chain
// pushed by every ibBackendException ctor, so the user sees the root
// cause along with any wrapping context, not just the last one.
void ShowBackendErrorChain(wxWindow* parent,
                            const wxString& caption,
                            const wxString& heading,
                            const wxString& detail = wxEmptyString)
{
	wxString body = heading;
	if (!detail.IsEmpty()) {
		body += wxT("\n\n");
		body += detail;
	}
	const auto chain = ibBackendException::DrainLastErrors();
	if (!chain.empty()) {
		body += wxT("\n\n--- backend error chain ---");
		for (std::size_t i = 0; i < chain.size(); ++i) {
			body += wxT("\n");
			body += chain[i];
		}
	}
	wxMessageBox(body, caption, wxOK | wxCENTRE | wxICON_ERROR, parent);
}
} // namespace

#include "frontend/window_ptr.h"

#include "frontend/mainFrame/settings/settingsdialog.h"
#include "frontend/mainFrame/settings/keybinderdialog.h" 
#include "frontend/mainFrame/settings/fontcolorsettingspanel.h"
#include "frontend/mainFrame/settings/editorsettingspanel.h"

#include "frontend/win/dlgs/applyChange.h"

//********************************************************************************
//*                                 Debug commands                               *
//********************************************************************************

#include "backend/metadataConfiguration.h"

// Common shape for the three places that save the configuration before
// kicking off a child process (enterprise.exe / wenterprise-server.exe
// for debug). Returns true when the configuration is in a state safe to
// launch — either no save was needed, the user declined to save, or the
// save succeeded. Returns false only when a save was attempted and it
// failed (then the dialog already showed the chain — caller bails).
static bool SaveBeforeChildLaunch(wxWindow* parent)
{
	if (!activeMetaData->IsModified())
		return true;
	const int ans = wxMessageBox(
		wxString::Format(_("Configuration '%s' has been changed.\nDo you want to save?"),
			activeMetaData->GetConfigName()),
		wxTheApp->GetAppDisplayName(),
		wxYES_NO | wxCENTRE | wxICON_QUESTION, parent);
	if (ans != wxYES)
		return true;

	try {
		if (!activeMetaData->SaveDatabase(saveConfigFlag)) {
			ShowBackendErrorChain(parent, wxMessageBoxCaptionStr,
				_("Failed to save configuration before launching the runtime."));
			return false;
		}
	}
	catch (const ibBackendException& e) {
		ShowBackendErrorChain(parent, wxMessageBoxCaptionStr,
			_("Pre-launch save aborted by a backend error."),
			e.GetErrorDescription());
		return false;
	}
	catch (const std::exception& e) {
		ShowBackendErrorChain(parent, wxMessageBoxCaptionStr,
			_("Pre-launch save aborted by an internal error."),
			wxString::FromUTF8(e.what()));
		return false;
	}
	return true;
}

void ibFrontendDocMDIFrameDesigner::OnStartDebug(wxCommandEvent& WXUNUSED(event))
{
	if (debugClient->HasConnections()) {
		wxMessageBox(_("Debugger is already running!"));
		return;
	}

	if (!SaveBeforeChildLaunch(this))
		return;

	appData->RunApplication(wxT("enterprise"));
}

void ibFrontendDocMDIFrameDesigner::OnStartDebugWithoutDebug(wxCommandEvent& WXUNUSED(event))
{
	if (!SaveBeforeChildLaunch(this))
		return;

	appData->RunApplication(wxT("enterprise"), false);
}

static bool SaveIfModifiedBeforeWebDebug(wxWindow* parent)
{
	return SaveBeforeChildLaunch(parent);
}

static void LaunchWebDebug(wxWindow* parent, bool withDebug)
{
	// URL prefix and port are derived by wes itself (from --file/--db
	// basename and OS-picked ephemeral port). Manifest handshake reports
	// the real URL back and opens the browser — no guessing here.
	// withDebug=true → wes spawns with --debug so its debugServer comes
	// up; the designer's debugClient then attaches via the manifest's
	// pid/host (out-of-band of the manifest itself — debug-server still
	// listens on defaultDebuggerPort+offset).
	if (appData->RunApplication(wxT("wenterprise-server"),
			/*searchDebug=*/withDebug, /*useManifest=*/true) == 0) {
		wxMessageBox(_("Failed to start wenterprise-server"),
			wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, parent);
	}
}

void ibFrontendDocMDIFrameDesigner::OnStartDebugWeb(wxCommandEvent& WXUNUSED(event))
{
	if (debugClient->HasConnections()) {
		wxMessageBox(_("Debugger is already running!"));
		return;
	}
	if (!SaveIfModifiedBeforeWebDebug(this))
		return;
	LaunchWebDebug(this, /*withDebug=*/true);
}

void ibFrontendDocMDIFrameDesigner::OnStartDebugWithoutDebugWeb(wxCommandEvent& WXUNUSED(event))
{
	if (!SaveIfModifiedBeforeWebDebug(this))
		return;
	LaunchWebDebug(this, /*withDebug=*/false);
}

#include "win/dlg/debugItem/debugItem.h"

void ibFrontendDocMDIFrameDesigner::OnAttachForDebugging(wxCommandEvent& WXUNUSED)
{
	ibWindowPtr<ibDialogDebugItem> dlg(new ibDialogDebugItem(this, wxID_ANY));
	dlg->Show();
}

#include "backend/metaData.h"
#include "frontend/mainFrame/objinspect/objinspect.h"

#include "docManager/docManager.h"
#include "docManager/templates/docViewMetaFile.h"

void ibFrontendDocMDIFrameDesigner::OnOpenConfiguration(wxCommandEvent& event)
{
	ibMetaDataConfigurationBase* configDatabase = activeMetaData->GetConfiguration();
	wxASSERT(configDatabase);

	for (auto& doc : docManager->GetDocumentsVector()) {
		ibMetadataBrowserDocument* metaDoc = wxDynamicCast(doc, ibMetadataBrowserDocument);
		if (metaDoc != nullptr &&
			metaDoc->GetMetaObject() == configDatabase->GetCommonMetaObject()) {
			metaDoc->Activate();
			return;
		}
	}

	ibMetadataBrowserDocument* newDocument =
		new ibMetadataBrowserDocument(configDatabase);

	wxASSERT(newDocument);
	try {

		m_docManager->AddDocument(newDocument);

		ibValueMetaObject* metaObject = configDatabase->GetCommonMetaObject();

		newDocument->SetTitle(metaObject->GetModuleName());
		newDocument->SetFilename(metaObject->GetDocPath());
		newDocument->SetMetaObject(metaObject);

		if (newDocument->OnCreate(metaObject->GetModuleName(), ibDOC_NEW)) {
			newDocument->SetCommandProcessor(newDocument->OnCreateCommandProcessor());
			//newDocument->UpdateAllViews();
		}
		else {

			// The document may be already destroyed, this happens if its view
			// creation fails as then the view being created is destroyed
			// triggering the destruction of the document as this first view is
			// also the last one. However if OnCreate() fails for any reason other
			// than view creation failure, the document is still alive and we need
			// to clean it up ourselves to avoid having a zombie document.
			newDocument->DeleteAllViews();
		}
	}
	catch (...) {
		if (m_docManager->GetDocuments().Member(newDocument)) {
			newDocument->DeleteAllViews();
		}
	}
}

void ibFrontendDocMDIFrameDesigner::OnRollbackConfiguration(wxCommandEvent& event)
{
	objectInspector->SelectObject(nullptr);

	wxAuiMDIClientWindow* client_window = GetClientWindow();
	wxCHECK_RET(client_window, wxS("Missing MDI Client Window"));

	client_window->Freeze();

	bool success = true;
	wxAuiMDIChildFrame* pActiveChild = nullptr;
	while ((pActiveChild = GetActiveChild()) != nullptr) {
		if (!pActiveChild->Close()) {
			// it refused to close, don't close the remaining ones either
			success = false;
			break;
		}
	}

	success = success && activeMetaData->RollbackDatabase()
		&& m_metaWindow->Load();

	client_window->Thaw();

	if (success) {
		objectInspector->SelectObject(activeMetaData->GetCommonMetaObject());
		wxMessageBox(_("Successfully rolled back to database configuration!"), wxTheApp->GetAppDisplayName(), wxOK | wxCENTRE, this);
	}
}

void ibFrontendDocMDIFrameDesigner::OnUpdateConfiguration(wxCommandEvent& event)
{
	bool canSave = true;
	if (debugClient->HasConnections()) {
		if (wxMessageBox(
			_("To update the database configuration you need stop debugging.\nDo you want to continue?"), wxTheApp->GetAppDisplayName(),
			wxYES_NO | wxCENTRE | wxICON_INFORMATION, this) == wxNO) {
			return;
		}
		debugClient->Stop(true);
	}

	for (auto& doc : m_docManager->GetDocumentsVector()) {

		if (!canSave)
			break;

		canSave = doc->OnSaveModified();
	}

	// DB calls below can throw ibDatabaseLayerException (constraint
	// violation on schema change, deadlock, lost connection mid-DDL).
	// Pre-2026-05-26 the layer swallowed and only set m_strErrorMessage;
	// now an unhandled throw would unwind through the wx event handler
	// into wxApp::OnUnhandledException → debug-report dialog with no
	// user-readable text. Wrap both stages so save / update DDL errors
	// land in a proper message box with the backend error chain.
	try {
		// stage one - save database
		if (canSave && !activeMetaData->SaveDatabase()) {

			for (const auto& entry : s_restructureInfo) {
				if (entry.type == ibRestructure::error)
					outputWindow->OutputError(entry.descr);
			}

			ShowBackendErrorChain(this, wxMessageBoxCaptionStr,
				_("Failed to save database!"));

			return;
		}

		// stage two - update database
		if (canSave && activeMetaData->OnBeforeSaveDatabase(saveConfigFlag)) {

			bool roolback = false, success = true;

			if (activeMetaData->OnSaveDatabase(saveConfigFlag)) {
				roolback = !ibDialogApplyChange::ShowApplyChange(s_restructureInfo, this);
			}
			else {
				success = false;
			}

			success = activeMetaData->OnAfterSaveDatabase(roolback || !success, saveConfigFlag);

			if (!success) {
				ShowBackendErrorChain(this, wxMessageBoxCaptionStr,
					_("Failed to update database!"));
			}
		}
	}
	catch (const ibBackendException& e) {
		ShowBackendErrorChain(this, wxMessageBoxCaptionStr,
			_("Configuration update was aborted by a backend error."),
			e.GetErrorDescription());
	}
	catch (const std::exception& e) {
		ShowBackendErrorChain(this, wxMessageBoxCaptionStr,
			_("Configuration update was aborted by an internal error."),
			wxString::FromUTF8(e.what()));
	}
}

void ibFrontendDocMDIFrameDesigner::OnLoadDatabase(wxCommandEvent& event)
{
	wxFileDialog openFileDialog(this, _("Open database file"), "", "",
		"Database files (*.obk)|*.obk", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (openFileDialog.ShowModal() == wxID_CANCEL)
		return;     // the user changed idea...

	objectInspector->SelectObject(nullptr);

	wxAuiMDIClientWindow* client_window = GetClientWindow();
	wxCHECK_RET(client_window, wxS("Missing MDI Client Window"));

	client_window->Freeze();

	bool success = true;
	wxAuiMDIChildFrame* pActiveChild = nullptr;
	while ((pActiveChild = GetActiveChild()) != nullptr) {
		if (!pActiveChild->Close()) {
			// it refused to close, don't close the remaining ones either
			success = false;
			break;
		}
	}

	client_window->Thaw();

	if (!success)
		return;

	try {
		if (appData->LoadDatabase(openFileDialog.GetPath())) {
			wxMessageBox(_("Loading of tasks completed successful. Restart the program!"));
			if (auto* reg = ibApplicationData::GetSessionRegistry())
				reg->CloseAll(true);
		}
		else {
			ShowBackendErrorChain(this, wxMessageBoxCaptionStr,
				_("Error when trying to load database from a file!"));
		}
	}
	catch (const ibBackendException& e) {
		ShowBackendErrorChain(this, wxMessageBoxCaptionStr,
			_("Loading the database failed."),
			e.GetErrorDescription());
	}
	catch (const std::exception& e) {
		ShowBackendErrorChain(this, wxMessageBoxCaptionStr,
			_("Loading the database failed with an internal error."),
			wxString::FromUTF8(e.what()));
	}

	event.Skip();
}

void ibFrontendDocMDIFrameDesigner::OnSaveDatabase(wxCommandEvent& event)
{
	wxFileDialog saveFileDialog(this, _("Save database file"), "", "",
		"Database files (*.obk)|*.obk", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (saveFileDialog.ShowModal() == wxID_CANCEL)
		return;     // the user changed idea...

	if (appData->SaveDatabase(saveFileDialog.GetPath()))
		wxMessageBox(_("Data upload completed successfully!"));
	else 
		wxMessageBox(_("Error when trying to upload data to a file!"));

	event.Skip();
}

void ibFrontendDocMDIFrameDesigner::OnClearDatabase(wxCommandEvent& event)
{
	if (wxMessageBox(_("Are you sure you want to clear the database?"), wxTheApp->GetAppDisplayName(), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxNO)
		return;

	objectInspector->SelectObject(nullptr);

	wxAuiMDIClientWindow* client_window = GetClientWindow();
	wxCHECK_RET(client_window, wxS("Missing MDI Client Window"));

	client_window->Freeze();

	bool success = true;
	wxAuiMDIChildFrame* pActiveChild = nullptr;
	while ((pActiveChild = GetActiveChild()) != nullptr) {
		if (!pActiveChild->Close()) {
			// it refused to close, don't close the remaining ones either
			success = false;
			break;
		}
	}

	client_window->Thaw();

	if (!success)
		return;

	if (appData->ClearDatabase())
		wxMessageBox(_("Database cleared successfully!"));
	else
		wxMessageBox(_("Error when trying to clear database!"));
	
	event.Skip();
}

void ibFrontendDocMDIFrameDesigner::OnConfiguration(wxCommandEvent& event)
{
	if (wxID_DESIGNER_CONFIGURATION_LOAD_FROM_FILE == event.GetId())
	{
		wxFileDialog openFileDialog(this, _("Open configuration file"), "", "",
			wxT("Configuration files (*.mcf)|*.mcf"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		objectInspector->SelectObject(nullptr);
		if (!m_docManager->CloseDocuments())
			return;

		// proceed loading the file chosen by the user;
		if (activeMetaData->LoadConfigFromFile(openFileDialog.GetPath())) {
			if (m_metaWindow->Load()) {
				if (activeMetaData->IsModified()) {
					if (wxMessageBox(wxString::Format(_("Configuration '%s' has been changed.\nDo you want to save?"), 
						activeMetaData->GetConfigName()), wxTheApp->GetAppDisplayName(), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES) {
						OnUpdateConfiguration(event);
					}
				}
				objectInspector->SelectObject(activeMetaData->GetCommonMetaObject());
			}
		}
	}
	else if (wxID_DESIGNER_CONFIGURATION_SAVE_TO_FILE == event.GetId())
	{
		wxFileDialog saveFileDialog(this, _("Save configuration file"), "", "",
			wxT("Configuration files (*.mcf)|*.mcf"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (saveFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		if (activeMetaData->SaveConfigToFile(saveFileDialog.GetPath())) {
			wxMessageBox(_("Successfully unloaded to: ") + saveFileDialog.GetPath());
		}
	}
	else if (wxID_DESIGNER_CONFIGURATION_COMPARE_FILE == event.GetId())
	{
		// "Compare with file..." — load a second config side-by-side into
		// a transient ibMetaDataConfigurationFile (not appData-owned) and
		// hand both roots to the dialog. The transient config goes away
		// when this scope exits; we don't mutate activeMetaData here.
		wxFileDialog openFileDialog(this, _("Choose configuration file to compare with"),
			"", "",
			wxT("Configuration files (*.mcf)|*.mcf"),
			wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;

		// Heap-allocate the transient config so it lives for the doc's
		// lifetime (the historical wxDialog version used a stack object
		// because ShowModal blocked here; an MDI tab can't rely on that).
		auto otherHolder = std::make_shared<ibMetaDataConfigurationFile>();
		const wxString otherPath = openFileDialog.GetPath();
		if (!otherHolder->LoadConfigFromFile(otherPath)) {
			wxMessageBox(_("Failed to load configuration file."),
				wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
			return;
		}

		const wxString otherLabel = wxFileName(otherPath).GetFullName();

		if (docManager != nullptr) {
			auto* doc = docManager->CreateDocument<ibConfigCompareDocument>();
			if (doc != nullptr) {
				doc->Configure(
					activeMetaData->GetCommonMetaObject(),
					otherHolder->GetCommonMetaObject(),
					_("Current"),
					otherLabel,
					[otherHolder, otherPath]() {
						return otherHolder->SaveConfigToFile(otherPath);
					},
					[this]() {
						// Pull mutation lands on activeMetaData — rebuild
						// the designer's metadata tree so the user sees the
						// merged state.
						if (m_metaWindow != nullptr)
							m_metaWindow->Load();
					});
				docManager->AddDocument(doc);
				if (!doc->OnCreate(wxEmptyString, 0))
					doc->DeleteAllViews();
			}
		}
	}
	else if (wxID_DESIGNER_CONFIGURATION_COMPARE_DB == event.GetId())
	{
		// "Compare with database configuration" — activeMetaData in
		// designer mode is an ibMetaDataConfigurationStorage that wraps
		// a baseline (the DB-stored config) plus the user's edits. The
		// baseline lives at GetConfiguration() — no DB query needed,
		// we just compare two already-loaded metadata trees.
		ibMetaDataConfigurationStorage* storage =
			dynamic_cast<ibMetaDataConfigurationStorage*>(activeMetaData);
		if (storage == nullptr || storage->GetConfiguration() == nullptr) {
			wxMessageBox(
				_("Database configuration is not available."),
				wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
			return;
		}

		// Right side is the DB baseline — Push direction would mean
		// "write current's changes into the DB config in memory". That
		// mutation never gets persisted unless the user runs "Update
		// database configuration" afterwards, so we don't expose Push
		// here (no save callback → view hides the Push entry).
		if (docManager != nullptr) {
			auto* doc = docManager->CreateDocument<ibConfigCompareDocument>();
			if (doc != nullptr) {
				doc->Configure(
					storage->GetCommonMetaObject(),
					storage->GetConfiguration()->GetCommonMetaObject(),
					_("Current"),
					_("Database"),
					/*rightSaveCallback*/ {},
					[this]() {
						if (m_metaWindow != nullptr)
							m_metaWindow->Load();
					});
				docManager->AddDocument(doc);
				if (!doc->OnCreate(wxEmptyString, 0))
					doc->DeleteAllViews();
			}
		}
	}
	else if (wxID_DESIGNER_CONFIGURATION_COMPARE_TWO_FILES == event.GetId())
	{
		// "Compare two files..." — pick two .mcf files, load both into
		// transient ibMetaDataConfigurationFile instances and diff.
		// Neither side is activeMetaData; activeMetaData stays untouched.
		wxFileDialog dlgA(this, _("Choose left configuration file"),
			"", "", wxT("Configuration files (*.mcf)|*.mcf"),
			wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (dlgA.ShowModal() == wxID_CANCEL)
			return;

		wxFileDialog dlgB(this, _("Choose right configuration file"),
			"", "", wxT("Configuration files (*.mcf)|*.mcf"),
			wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (dlgB.ShowModal() == wxID_CANCEL)
			return;

		// Heap-allocate both transient configs so they survive the doc's
		// lifetime (MDI tab is non-modal — can't rely on stack scope).
		auto fileA = std::make_shared<ibMetaDataConfigurationFile>();
		auto fileB = std::make_shared<ibMetaDataConfigurationFile>();
		const wxString pathA = dlgA.GetPath();
		const wxString pathB = dlgB.GetPath();
		if (!fileA->LoadConfigFromFile(pathA) || !fileB->LoadConfigFromFile(pathB)) {
			wxMessageBox(_("Failed to load one of the configuration files."),
				wxTheApp->GetAppDisplayName(), wxOK | wxICON_ERROR, this);
			return;
		}

		// activeMetaData isn't involved — Pull mutations to fileA would
		// need a separate save step. V2 enhancement; current behaviour
		// matches the historical dialog (no left-save callback).
		if (docManager != nullptr) {
			auto* doc = docManager->CreateDocument<ibConfigCompareDocument>();
			if (doc != nullptr) {
				doc->Configure(
					fileA->GetCommonMetaObject(),
					fileB->GetCommonMetaObject(),
					wxFileName(pathA).GetFullName(),
					wxFileName(pathB).GetFullName(),
					[fileB, pathB]() {
						return fileB->SaveConfigToFile(pathB);
					},
					/*appliedCallback*/ {});
				docManager->AddDocument(doc);
				if (!doc->OnCreate(wxEmptyString, 0))
					doc->DeleteAllViews();
			}
		}
	}
}

void ibFrontendDocMDIFrameDesigner::OnRunDebugCommand(wxCommandEvent& event)
{
	switch (event.GetId())
	{
	case wxID_DESIGNER_DEBUG_STEP_OVER:
		debugClient->StepOver();
		break;
	case wxID_DESIGNER_DEBUG_STEP_INTO:
		debugClient->StepInto();
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

//********************************************************************************
//*                                    Tool                                      *
//********************************************************************************

void ibFrontendDocMDIFrameDesigner::OnToolsSettings(wxCommandEvent& event)
{
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

	if (dialog.ShowModal() == wxID_OK)
	{
		m_keyBinder.ClearCommands();

		for (unsigned int i = 0; i < keyBinder->GetNumCommands(); ++i) {
			m_keyBinder.AddCommand(keyBinder->GetCommand(i));
		}

		m_keyBinder.UpdateWindow(this);

#if wxUSE_MENUBAR
		if (m_pMyMenuBar != nullptr)
			m_keyBinder.UpdateMenuBar(m_pMyMenuBar);

		m_keyBinder.UpdateMenuBar(GetMenuBar());
#endif

		m_fontColorSettings = fontColorSettings->GetSettings();
		m_editorSettings = editorSettings->GetSettings();

		UpdateEditorOptions();
	}

	SaveOptions();
}

#include "frontend/win/dlgs/userList.h"

void ibFrontendDocMDIFrameDesigner::OnUsers(wxCommandEvent& event)
{
	ibWindowPtr<ibDialogUserList> dlg(new ibDialogUserList(this, wxID_ANY));
	dlg->Show();
}

#include "frontend/win/dlgs/activeUser.h"

void ibFrontendDocMDIFrameDesigner::OnActiveUsers(wxCommandEvent& event)
{
	ibWindowPtr<ibDialogActiveUser> dlg(new ibDialogActiveUser(this, wxID_ANY));
	dlg->Show();
}

#include "frontend/docView/docView.h"
#include "frontend/docView/templates/docViewAuditLog.h"
#include "backend/picturePredefined.h"

void ibFrontendDocMDIFrameDesigner::OnAuditLog(wxCommandEvent& event)
{
	// Open the journal as an MDI tab via the docview system (replaces
	// the historical modal ibDialogAuditLog). See the matching enterprise
	// handler — same template/CLSID, registered in ibDocManagerDesigner's ctor.
	if (docManager != nullptr) {
		ibAuditLogDocument* doc = docManager->CreateDocument<ibAuditLogDocument>();
		if (doc != nullptr) {
			doc->SetTitle(_("Registration journal"));
			doc->SetIcon(ibBackendPicture::GetPictureAsIcon(g_picUserActiveCLSID));
			docManager->AddDocument(doc);
			if (!doc->OnCreate(wxEmptyString, 0))
				doc->DeleteAllViews();
		}
	}
}

#include "frontend/win/dlgs/connectionDB.h"

void ibFrontendDocMDIFrameDesigner::OnConnection(wxCommandEvent& event)
{
	ibDialogConnection dlg(this, wxID_ANY);
	dlg.ShowModal();
}

#include "frontend/win/dlgs/about.h"

void ibFrontendDocMDIFrameDesigner::OnAbout(wxCommandEvent& event)
{
	ibDialogAbout dlg(this, wxID_ANY);
	dlg.ShowModal();
}