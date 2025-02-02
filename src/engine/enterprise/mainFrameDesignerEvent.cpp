////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"
#include "appData.h"
#include "core/compiler/debugger/debugClient.h"
#include "settings/settingsdialog.h"
#include "settings/keybinderdialog.h" 
#include "settings/fontcolorsettingspanel.h"
#include "settings/editorsettingspanel.h"

//********************************************************************************
//*                                 Debug commands                               *
//********************************************************************************

#include "core/metadata/metadataConfig.h"

void wxAuiDocDesignerMDIFrame::OnStartDebug(wxCommandEvent& WXUNUSED(event))
{
	if (debugClient->HasConnections()) {
		wxMessageBox(_("Debugger is already running!"));
		return;
	}

	if (metadata->IsModified()) {
		if (wxMessageBox(_("Configuration '" + metadata->GetMetadataName() + "' has been changed.\nDo you want to save?"), _("Save project"), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES) {
			if (!metadata->SaveMetadata(saveConfigFlag)) {
				return;
			}
		}
	}

	wxString executeCmd = "oes -m enterprise -d -ib " + appData->GetApplicationPath();

	if (!appData->GetUserName().IsEmpty()) {
		executeCmd += " -l " + appData->GetUserName();
	}
	if (!appData->GetUserPassword().IsEmpty()) {
		executeCmd += " -p " + appData->GetUserPassword();
	}

	wxExecute(executeCmd);
	debugClient->FindDebuggers();
}

void wxAuiDocDesignerMDIFrame::OnStartDebugWithoutDebug(wxCommandEvent& WXUNUSED(event))
{
	if (metadata->IsModified()) {
		if (wxMessageBox(_("Configuration '" + metadata->GetMetadataName() + "' has been changed.\nDo you want to save?"), _("Save project"), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES) {
			if (!metadata->SaveMetadata(saveConfigFlag)) {
				return;
			}
		}
	}

	wxString executeCmd = "oes -m enterprise -ib " + appData->GetApplicationPath();

	if (!appData->GetUserName().IsEmpty()) {
		executeCmd += " -l " + appData->GetUserName();
	}
	if (!appData->GetUserPassword().IsEmpty()) {
		executeCmd += " -p " + appData->GetUserPassword();
	}

	wxExecute(executeCmd);
}

#include "windows/debugItemsWnd.h"

void wxAuiDocDesignerMDIFrame::OnAttachForDebugging(wxCommandEvent& WXUNUSED)
{
	CDebugItemsWnd* debugItemsWnd = new CDebugItemsWnd(this, wxID_ANY);
	debugItemsWnd->Show();
}

#include "frontend/metatree/metaTreeWnd.h"
#include "frontend/objinspect/objinspect.h"

void wxAuiDocDesignerMDIFrame::OnRollbackConfiguration(wxCommandEvent& event)
{
	objectInspector->ClearProperty();

	if (!m_docManager->CloseDocuments())
		return;

	if (metadata->RoolbackToConfigDatabase()) {
		if (m_metadataTree->Load()) {
			objectInspector->SelectObject(metadata->GetCommonMetaObject());
		}
		wxMessageBox(_("Successfully rolled back to database configuration!"), _("Designer"), wxOK | wxCENTRE, this);
	}
}

void wxAuiDocDesignerMDIFrame::OnConfiguration(wxCommandEvent& event)
{
	if (wxID_DESIGNER_CONFIGURATION_LOAD == event.GetId())
	{
		wxFileDialog openFileDialog(this, _("Open configuration file"), "", "",
			"Configuration files (*.conf)|*.conf", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		objectInspector->ClearProperty();
		if (!m_docManager->CloseDocuments())
			return;

		// proceed loading the file chosen by the user;
		if (metadata->LoadFromFile(openFileDialog.GetPath())) {
			if (m_metadataTree->Load()) {
				objectInspector->SelectObject(metadata->GetCommonMetaObject());
				if (metadata->IsModified()) {
					if (wxMessageBox("Configuration '" + metadata->GetMetadataName() + "' has been changed.\nDo you want to save?", _("Save project"), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES) {
						metadata->SaveMetadata(saveConfigFlag);
					}
				}
			}
		}
	}
	else if (wxID_DESIGNER_CONFIGURATION_SAVE == event.GetId())
	{
		wxFileDialog saveFileDialog(this, _("Save configuration file"), "", "",
			"Configuration files (*.conf)|*.conf", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (saveFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		if (metadata->SaveToFile(saveFileDialog.GetPath())) {
			wxMessageBox(_("Successfully unloaded to: ") + saveFileDialog.GetPath());
		}
	}
}

void wxAuiDocDesignerMDIFrame::OnRunDebugCommand(wxCommandEvent& event)
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
		debugClient->RemoveAllBreakPoints(); 
		break;
	}
}

//********************************************************************************
//*                                    Tool                                      *
//********************************************************************************

void wxAuiDocDesignerMDIFrame::OnToolsSettings(wxCommandEvent& event)
{
	SettingsDialog dialog(this);

	KeyBinderDialog* keyBinder = dialog.GetKeyBinderDialog();

	for (unsigned int i = 0; i < m_keyBinder.GetNumCommands(); ++i)
	{
		keyBinder->AddCommand(m_keyBinder.GetCommand(i));
	}

	FontColorSettingsPanel* fontColorSettings = dialog.GetFontColorSettingsPanel();
	fontColorSettings->SetSettings(m_fontColorSettings);

	EditorSettingsPanel* editorSettings = dialog.GetEditorSettingsPanel();
	editorSettings->SetSettings(m_editorSettings);

	if (dialog.ShowModal() == wxID_OK)
	{
		m_keyBinder.ClearCommands();

		for (unsigned int i = 0; i < keyBinder->GetNumCommands(); ++i)
		{
			m_keyBinder.AddCommand(keyBinder->GetCommand(i));
		}

		m_keyBinder.UpdateWindow(this);
		m_keyBinder.UpdateMenuBar(GetMenuBar());

		m_fontColorSettings = fontColorSettings->GetSettings();
		m_editorSettings = editorSettings->GetSettings();

		UpdateEditorOptions();
	}

	SaveOptions();
}

#include "frontend/windows/userListWnd.h"

void wxAuiDocDesignerMDIFrame::OnUsers(wxCommandEvent& event)
{
	CUsersListWnd* usersWnd = new CUsersListWnd(this, wxID_ANY);
	usersWnd->Show();
}

#include "frontend/windows/activeUsersWnd.h"

void wxAuiDocDesignerMDIFrame::OnActiveUsers(wxCommandEvent& event)
{
	CActiveUsersWnd* activeUsers = new CActiveUsersWnd(this, wxID_ANY);
	activeUsers->Show();
}

#include "frontend/windows/aboutWnd.h"

void wxAuiDocDesignerMDIFrame::OnAbout(wxCommandEvent& event)
{
	CAboutDialogWnd* aboutDlg = new CAboutDialogWnd(this, wxID_ANY);
	aboutDlg->ShowModal();
}

//********************************************************************************
//*                                    Toolbar                                   *
//********************************************************************************

#include "core/compiler/debugger/debugClient.h"

void wxAuiDocDesignerMDIFrame::OnDebugEvent(wxDebugEvent& event)
{
	if (event.GetEventId() == EventId::EventId_SessionStart) {
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STEP_INTO, true);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STEP_OVER, true);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_PAUSE, true);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STOP_DEBUGGING, true);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STOP_PROGRAM, true);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_NEXT_POINT, false);
	}
	else if (event.GetEventId() == EventId::EventId_EnterLoop) {
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_PAUSE, false);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_NEXT_POINT, true);
	}
	else if (event.GetEventId() == EventId::EventId_LeaveLoop) {
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_PAUSE, true);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_NEXT_POINT, false);
	}
	else if (event.GetEventId() == EventId::EventId_SessionEnd) {
		if (debugClient->HasConnections())
			return;
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STEP_INTO, false);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STEP_OVER, false);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_PAUSE, false);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STOP_DEBUGGING, false);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_STOP_PROGRAM, false);
		m_menuDebug->Enable(wxID_DESIGNER_DEBUG_NEXT_POINT, false);
	}
}

#include "core/metadata/metadata.h"

void wxAuiDocDesignerMDIFrame::OnToolbarClicked(wxEvent& event)
{
	if (event.GetId() == wxID_DESIGNER_UPDATE_METADATA)
	{
		bool canSave = true;

		if (debugClient->HasConnections()) {
			if (wxMessageBox(_("To update the database configuration you need stop debugging.\nDo you want to continue?"), wxMessageBoxCaptionStr, wxYES_NO | wxCENTRE | wxICON_INFORMATION, this) == wxNO) {
				return;
			}
			debugClient->Stop(true);
		}

		for (auto m_document : m_docManager->GetDocumentsVector()) {
			if (!canSave)
				break;
			canSave = m_document->OnSaveModified();
		}

		if (canSave && !appData->SaveConfiguration()) {
			wxMessageBox("Failed to save metadata!", wxMessageBoxCaptionStr, wxOK | wxCENTRE | wxICON_ERROR, this);
		}
	}

	event.Skip();
}