////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"

#include "backend/appData.h"
#include "backend/debugger/debugClient.h"

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

void ibFrontendDocMDIFrameDesigner::OnStartDebug(wxCommandEvent& WXUNUSED(event))
{
	if (debugClient->HasConnections()) {
		wxMessageBox(_("Debugger is already running!"));
		return;
	}

	if (activeMetaData->IsModified()) {
		if (wxMessageBox(wxString::Format(_("Configuration '%s' has been changed.\nDo you want to save?"), activeMetaData->GetConfigName()), wxTheApp->GetAppDisplayName(), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES) {
			if (!activeMetaData->SaveDatabase(saveConfigFlag)) {
				return;
			}
		}
	}

	appData->RunApplication(wxT("enterprise"));
}

void ibFrontendDocMDIFrameDesigner::OnStartDebugWithoutDebug(wxCommandEvent& WXUNUSED(event))
{
	if (activeMetaData->IsModified()) {
		if (wxMessageBox(wxString::Format(_("Configuration '%s' has been changed.\nDo you want to save?"), activeMetaData->GetConfigName()), wxTheApp->GetAppDisplayName(), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES) {
			if (!activeMetaData->SaveDatabase(saveConfigFlag)) {
				return;
			}
		}
	}

	appData->RunApplication(wxT("enterprise"), false);
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

		if (newDocument->OnCreate(metaObject->GetModuleName(), wxDOC_NEW)) {
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

	// stage one - save database  
	if (canSave && !activeMetaData->SaveDatabase()) {

		for (unsigned int idx = 0; idx < s_restructureInfo.GetCount(); idx++) {
			if (s_restructureInfo.GetType(idx) == ibRestructure::restructure_error)
				outputWindow->OutputError(s_restructureInfo.GetDescription(idx));
		}

		wxMessageBox(_("Failed to save database!"),
			wxMessageBoxCaptionStr, wxOK | wxCENTRE | wxICON_ERROR, this
		);

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
			wxMessageBox(_("Failed to update database!"),
				wxMessageBoxCaptionStr, wxOK | wxCENTRE | wxICON_ERROR, this
			);
		}
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

	if (appData->LoadDatabase(openFileDialog.GetPath())) {
		wxMessageBox(_("Loading of tasks completed successful. Restart the program!"));
		appData->ForceExit();
	}
	else {
		wxMessageBox(_("Error when trying to load database from a file!!"));
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