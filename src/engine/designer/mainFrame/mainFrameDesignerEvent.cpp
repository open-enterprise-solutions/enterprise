////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameDesigner.h"
#include "backend/appData.h"
#include "backend/debugger/debugClient.h"

#include "frontend/mainFrame/settings/settingsdialog.h"
#include "frontend/mainFrame/settings/keybinderdialog.h" 
#include "frontend/mainFrame/settings/fontcolorsettingspanel.h"
#include "frontend/mainFrame/settings/editorsettingspanel.h"

//********************************************************************************
//*                                 Debug commands                               *
//********************************************************************************

#include "backend/metadataConfiguration.h"

void CDocDesignerMDIFrame::OnStartDebug(wxCommandEvent& WXUNUSED(event))
{
	if (debugClient->HasConnections()) {
		wxMessageBox(_("Debugger is already running!"));
		return;
	}

	if (commonMetaData->IsModified()) {
		if (wxMessageBox(_("Configuration '" + commonMetaData->GetConfigName() + "' has been changed.\nDo you want to save?"), _("Save project"), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES) {
			if (!commonMetaData->SaveConfiguration(saveConfigFlag)) {
				return;
			}
		}
	}

	debugClient->SearchDebugger();
	appData->RunApplication(wxT("enterprise"));
}

void CDocDesignerMDIFrame::OnStartDebugWithoutDebug(wxCommandEvent& WXUNUSED(event))
{
	if (commonMetaData->IsModified()) {
		if (wxMessageBox(_("Configuration '" + commonMetaData->GetConfigName() + "' has been changed.\nDo you want to save?"), _("Save project"), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES) {
			if (!commonMetaData->SaveConfiguration(saveConfigFlag)) {
				return;
			}
		}
	}

	appData->RunApplication(wxT("enterprise"), false);
}

#include "win/dlg/debugItem/debugItem.h"

void CDocDesignerMDIFrame::OnAttachForDebugging(wxCommandEvent& WXUNUSED)
{
	CDialogDebugItem* debugItemsWnd = new CDialogDebugItem(this, wxID_ANY);
	debugItemsWnd->Show();
}

#include "backend/metaData.h"
#include "frontend/mainFrame/objinspect/objinspect.h"

void CDocDesignerMDIFrame::OnRollbackConfiguration(wxCommandEvent& event)
{
	objectInspector->ClearProperty();

	if (!m_docManager->CloseDocuments())
		return;

	if (commonMetaData->RoolbackConfiguration()) {
		if (m_metadataTree->Load()) {
			objectInspector->SelectObject(commonMetaData->GetCommonMetaObject());
		}
		wxMessageBox(_("Successfully rolled back to database configuration!"), _("Designer"), wxOK | wxCENTRE, this);
	}
}

void CDocDesignerMDIFrame::OnConfiguration(wxCommandEvent& event)
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
		if (commonMetaData->LoadFromFile(openFileDialog.GetPath())) {
			if (m_metadataTree->Load()) {
				if (commonMetaData->IsModified()) {
					if (wxMessageBox("Configuration '" + commonMetaData->GetConfigName() + "' has been changed.\nDo you want to save?", _("Save project"), wxYES_NO | wxCENTRE | wxICON_QUESTION, this) == wxYES) {
						commonMetaData->SaveConfiguration(saveConfigFlag);
					}
				}
				objectInspector->SelectObject(commonMetaData->GetCommonMetaObject());
			}
		}
	}
	else if (wxID_DESIGNER_CONFIGURATION_SAVE == event.GetId())
	{
		wxFileDialog saveFileDialog(this, _("Save configuration file"), "", "",
			"Configuration files (*.conf)|*.conf", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (saveFileDialog.ShowModal() == wxID_CANCEL)
			return;     // the user changed idea...

		if (commonMetaData->SaveToFile(saveFileDialog.GetPath())) {
			wxMessageBox(_("Successfully unloaded to: ") + saveFileDialog.GetPath());
		}
	}
}

void CDocDesignerMDIFrame::OnRunDebugCommand(wxCommandEvent& event)
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

void CDocDesignerMDIFrame::OnToolsSettings(wxCommandEvent& event)
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

#include "frontend/win/dlgs/userList.h"

void CDocDesignerMDIFrame::OnUsers(wxCommandEvent& event)
{
	CDialogUserList *dlg = new CDialogUserList(this, wxID_ANY);
	dlg->Show();
}

#include "frontend/win/dlgs/activeUser.h"

void CDocDesignerMDIFrame::OnActiveUsers(wxCommandEvent& event)
{
	CDialogActiveUser* dlg = new CDialogActiveUser(this, wxID_ANY);
	dlg->Show();
}

#include "frontend/win/dlgs/connectionDB.h"

void CDocDesignerMDIFrame::OnConnection(wxCommandEvent& event)
{
	CDialogConnection* dlg = new CDialogConnection(this, wxID_ANY);
	dlg->LoadConnectionData();
	dlg->ShowModal();
	
	dlg->Destroy();
}

#include "frontend/win/dlgs/about.h"

void CDocDesignerMDIFrame::OnAbout(wxCommandEvent& event)
{
	CDialogAbout *dlg = new CDialogAbout(this, wxID_ANY);
	const int ret = dlg->ShowModal();
	dlg->Destroy();
}

//********************************************************************************
//*                                    Toolbar                                   *
//********************************************************************************

void CDocDesignerMDIFrame::OnToolbarClicked(wxEvent& event)
{
	if (event.GetId() == wxID_DESIGNER_UPDATE_METADATA) {
		bool canSave = true;
		if (debugClient->HasConnections()) {
			if (wxMessageBox(
				_("To update the database configuration you need stop debugging.\nDo you want to continue?"), wxMessageBoxCaptionStr, 
				wxYES_NO | wxCENTRE | wxICON_INFORMATION, this) == wxNO) {
				return;
			}
			debugClient->Stop(true);
		}
		for (auto &doc : m_docManager->GetDocumentsVector()) {
			if (!canSave)
				break;
			canSave = doc->OnSaveModified();
		}
		if (canSave && !commonMetaData->SaveConfiguration(saveConfigFlag)) {
			wxMessageBox("Failed to save metaData!", 
				wxMessageBoxCaptionStr, wxOK | wxCENTRE | wxICON_ERROR, this
			);
		}
	}

	event.Skip();
}