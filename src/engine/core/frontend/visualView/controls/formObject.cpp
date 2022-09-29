////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame object
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "appData.h"
#include "metadata/metadata.h"
#include "common/docManager.h"
#include "common/srcExplorer.h"
#include "metadata/moduleManager/moduleManager.h"
#include "compiler/systemObjects.h"
#include "frontend/visualView/visualHost.h"

//*************************************************************************************************
//*                                    System attribute                                           *
//*************************************************************************************************

#include "toolBar.h"
#include "tableBox.h"

void CValueForm::BuildForm(const form_identifier_t& formType)
{
	m_defaultFormType = formType;

	if (m_sourceObject != NULL) {

		CValue* prevSrcData = NULL;

		CValueToolbar* mainToolBar =
			wxDynamicCast(
				CValueForm::CreateControl(wxT("toolbar")), CValueToolbar
			);

		mainToolBar->SetControlName(wxT("mainToolbar"));
		mainToolBar->SetActionSrc(FORM_ACTION);

		IMetaObjectWrapperData* metaObjectValue =
			m_sourceObject->GetMetaObject();

		CValueTableBox* mainTableBox = NULL;

		actionData_t actions = CValueForm::GetActions(formType);
		for (unsigned int idx = 0; idx < actions.GetCount(); idx++) {
			const action_identifier_t &id = actions.GetID(idx);
			if (id != wxNOT_FOUND) {
				CValue* currSrcData = actions.GetSourceDataByID(id);
				if (currSrcData != prevSrcData
					&& prevSrcData != NULL) {
					CValueForm::CreateControl(wxT("toolSeparator"), mainToolBar);
				}
				CValueToolBarItem* toolBarItem =
					wxDynamicCast(
						CValueForm::CreateControl(wxT("tool"), mainToolBar), CValueToolBarItem
				);
				toolBarItem->SetControlName(mainToolBar->GetControlName() + wxT("_") + actions.GetNameByID(id));
				toolBarItem->SetCaption(actions.GetCaptionByID(id));
				toolBarItem->SetAction(wxString::Format("%i", id));
				prevSrcData = currSrcData;
			}
			else {
				CValueForm::CreateControl(wxT("toolSeparator"), mainToolBar);
			}
		}

		CSourceExplorer sourceExplorer = m_sourceObject->GetSourceExplorer();
		if (sourceExplorer.IsTableSection()) {

			mainTableBox =
				wxDynamicCast(
					CValueForm::CreateControl(wxT("tablebox")), CValueTableBox
				);

			mainTableBox->SetControlName(sourceExplorer.GetSourceName());
			mainTableBox->SetSourceId(sourceExplorer.GetMetaIDSource());
		}

		for (unsigned int idx = 0; idx < sourceExplorer.GetHelperCount(); idx++) {

			CSourceExplorer nextSourceExplorer = sourceExplorer.GetHelper(idx);

			if (sourceExplorer.IsTableSection()) {
				CValueTableBoxColumn* tableBoxColumn =
					wxDynamicCast(
						CValueForm::CreateControl(wxT("tableboxColumn"), mainTableBox), CValueTableBoxColumn
					);
				tableBoxColumn->SetControlName(mainTableBox->GetControlName() + wxT("_") + nextSourceExplorer.GetSourceName());
				//tableBoxColumn->SetCaption(nextSourceExplorer.GetSourceSynonym());
				tableBoxColumn->SetVisibleColumn(nextSourceExplorer.IsVisible()
					|| sourceExplorer.GetHelperCount() == 1);
				tableBoxColumn->SetSourceId(nextSourceExplorer.GetMetaIDSource());
				tableBoxColumn->SetSortableColumn();
			}
			else
			{
				prevSrcData = NULL;

				if (nextSourceExplorer.IsTableSection()) {

					CValueToolbar* toolBar =
						wxDynamicCast(
							CValueForm::CreateControl(wxT("toolbar")), CValueToolbar
						);

					toolBar->SetControlName(wxT("toolbar_") + nextSourceExplorer.GetSourceName());

					CValueTableBox* tableBox =
						wxDynamicCast(
							CValueForm::CreateControl(wxT("tablebox")), CValueTableBox
						);

					tableBox->SetControlName(nextSourceExplorer.GetSourceName());
					tableBox->SetSourceId(nextSourceExplorer.GetMetaIDSource());

					toolBar->SetActionSrc(tableBox->GetControlID());

					actionData_t actions = tableBox->GetActions(formType);
					for (unsigned int idx = 0; idx < actions.GetCount(); idx++) {
						const action_identifier_t &id = actions.GetID(idx);
						if (id != wxNOT_FOUND) {
							CValue* currSrcData = actions.GetSourceDataByID(id);
							if (currSrcData != prevSrcData
								&& prevSrcData != NULL) {
								CValueForm::CreateControl(wxT("toolSeparator"), toolBar);
							}
							CValueToolBarItem* toolBarItem =
								wxDynamicCast(
									CValueForm::CreateControl(wxT("tool"), toolBar), CValueToolBarItem
							);
							toolBarItem->SetControlName(toolBar->GetControlName() + wxT("_") + actions.GetNameByID(id));
							toolBarItem->SetCaption(actions.GetCaptionByID(id));
							toolBarItem->SetAction(wxString::Format("%i", id));
							prevSrcData = currSrcData;
						}
						else {
							CValueForm::CreateControl(wxT("toolSeparator"), toolBar);
						}
					}

					for (unsigned int col = 0; col < nextSourceExplorer.GetHelperCount(); col++) {
						CSourceExplorer colSourceExplorer = nextSourceExplorer.GetHelper(col);

						CValueTableBoxColumn* tableBoxColumn =
							wxDynamicCast(
								CValueForm::CreateControl(wxT("tableboxColumn"), tableBox), CValueTableBoxColumn
							);
						tableBoxColumn->SetControlName(tableBox->GetControlName() + wxT("_") + colSourceExplorer.GetSourceName());
						//tableBoxColumn->SetCaption(colSourceExplorer.GetSourceSynonym());
						tableBoxColumn->SetVisibleColumn(colSourceExplorer.IsVisible()
							|| nextSourceExplorer.GetHelperCount() == 1);
						tableBoxColumn->SetSourceId(colSourceExplorer.GetMetaIDSource());
					}
				}
				else {
					if (nextSourceExplorer.ContainType(eValueTypes::TYPE_BOOLEAN)
						&& nextSourceExplorer.GetTypeIDSource().size() == 1) {
						CValueCheckbox* checkbox =
							wxDynamicCast(
								CValueForm::CreateControl(wxT("checkbox")), CValueCheckbox
							);
						checkbox->SetControlName(nextSourceExplorer.GetSourceName());
						//checkbox->SetCaption(nextSourceExplorer.GetSourceSynonym());
						checkbox->EnableWindow(nextSourceExplorer.IsEnabled());
						checkbox->VisibleWindow(nextSourceExplorer.IsVisible());
						checkbox->SetSourceId(nextSourceExplorer.GetMetaIDSource());
					}
					else {

						bool selButton = !nextSourceExplorer.ContainType(eValueTypes::TYPE_BOOLEAN) &&
							!nextSourceExplorer.ContainType(eValueTypes::TYPE_NUMBER) &&
							//!nextSourceExplorer.ContainType(eValueTypes::TYPE_DATE) &&
							!nextSourceExplorer.ContainType(eValueTypes::TYPE_STRING);

						if (nextSourceExplorer.GetTypeIDSource().size() != 1)
							selButton = true;

						CValueTextCtrl* textCtrl =
							wxDynamicCast(
								CValueForm::CreateControl(wxT("textctrl")), CValueTextCtrl
							);
						textCtrl->SetControlName(nextSourceExplorer.GetSourceName());
						//textCtrl->SetCaption(nextSourceExplorer.GetSourceSynonym());
						textCtrl->EnableWindow(nextSourceExplorer.IsEnabled());
						textCtrl->VisibleWindow(nextSourceExplorer.IsVisible());
						textCtrl->SetSourceId(nextSourceExplorer.GetMetaIDSource());

						textCtrl->SetSelectButton(selButton);
						textCtrl->SetListButton(false);
						textCtrl->SetClearButton(nextSourceExplorer.IsEnabled());
					}
				}
			}
		}

		if (metaObjectValue != NULL)
			SetCaption(metaObjectValue->GetSynonym());
	}
	else {
		CValueToolbar* mainToolBar =
			wxDynamicCast(
				CValueForm::CreateControl(wxT("toolbar")), CValueToolbar
			);

		mainToolBar->SetControlName(wxT("mainToolbar"));
		mainToolBar->SetActionSrc(FORM_ACTION);

		CValueTableBox* mainTableBox = NULL;

		actionData_t actions = CValueForm::GetActions(formType);
		for (unsigned int idx = 0; idx < actions.GetCount(); idx++) {
			CValueToolBarItem* toolBarItem =
				wxDynamicCast(
					CValueForm::CreateControl(wxT("tool"), mainToolBar), CValueToolBarItem
				);
			action_identifier_t id = actions.GetID(idx);
			toolBarItem->SetControlName(wxT("tool_") + actions.GetNameByID(id));
			toolBarItem->SetCaption(actions.GetCaptionByID(id));
			toolBarItem->SetAction(wxString::Format("%i", id));
		}

		SetCaption(m_metaFormObject ?
			m_metaFormObject->GetSynonym() :
			wxEmptyString);
	}
}

void CValueForm::InitializeForm(IValueFrame* ownerControl,
	IMetaFormObject* metaForm, ISourceDataObject* ownerSrc, const CUniqueKey& formGuid, bool readOnly)
{
	if (ownerSrc != NULL) {
		ownerSrc->IncrRef();
	}

	if (m_sourceObject != NULL) {
		m_sourceObject->DecrRef();
		m_sourceObject = NULL;
	}

	if (m_valueFormDocument != NULL) {
		m_valueFormDocument->Close();
		m_valueFormDocument = NULL;
	}

	m_formOwner = ownerControl;
	m_sourceObject = ownerSrc;
	m_metaFormObject = metaForm;

	if (formGuid.isValid()) { // 1. if set guid from user
		m_formKey = formGuid;
	}
	else if (ownerControl != NULL) { // 2. if set guid in owner
		m_formKey = ownerControl->GetControlGuid();
	}
	else if (m_sourceObject != NULL) { //3. if set guid in object 
		m_formKey = m_sourceObject->GetGuid();
	}
	else { //4. just generate 
		m_formKey = wxNewGuid;
	}

	if (m_metaFormObject != NULL) {
		m_defaultFormType = metaForm->GetTypeForm();
	}

	SetReadOnly(readOnly);
}

bool CValueForm::InitializeFormModule()
{
	IMetadata* metaData = m_metaFormObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	IModuleInfo* sourceObjectValue =
		dynamic_cast<IModuleInfo*>(
			m_sourceObject
			);

	if (m_compileModule == NULL) {
		m_compileModule = new CCompileModule(m_metaFormObject);
		m_compileModule->SetParent(
			sourceObjectValue ?
			sourceObjectValue->GetCompileModule() :
			moduleManager->GetCompileModule()
		);
		m_compileModule->AddContextVariable(thisForm, this);
	}

	if (appData->EnterpriseMode()) {

		try {
			m_compileModule->Compile();
		}
		catch (const CTranslateError* err) {
			if (appData->EnterpriseMode()) {
				CSystemObjects::Raise(err->what());
			}

			return false;
		};

		if (m_procUnit == NULL) {
			m_procUnit = new CProcUnit();
			m_procUnit->SetParent(
				sourceObjectValue ?
				sourceObjectValue->GetProcUnit() :
				moduleManager->GetProcUnit()
			);
		}

		m_procUnit->Execute(m_compileModule->m_cByteCode, true);
	}

	return true;
}

#include "compiler/valueType.h"

void CValueForm::NotifyChoice(CValue& vSelected)
{
	CValue vSelfForm = this; /*!!!*/
#pragma message("nouverbe to nouverbe: счётчие ссылок здесь равен 1, а должен быть больше одного!")

	if (m_formOwner != NULL) {
		m_formOwner->CallFunction("choiceProcessing", vSelected, vSelfForm);
	}

	if (m_formOwner != NULL) {
		m_formOwner->ChoiceProcessing(vSelected);
	}

	CValueForm* ownerForm =
		m_formOwner->GetOwnerForm();

	if (ownerForm != NULL) {
		ownerForm->UpdateForm();
	}

	CValueForm::CloseForm();

	IncrRef(); /*!!!*/
}

CValue CValueForm::CreateControl(const CValueType* clsControl, const CValue& vControl)
{
	if (appData->DesignerMode()) {
		return CValue();
	}

	if (!CValue::IsRegisterObject(clsControl->GetString(), eObjectType::eObjectType_object_control)) {
		CSystemObjects::Raise(_("Сlass does not belong to control!"));
	}

	//get parent obj
	IValueFrame* parentControl = NULL;

	if (!vControl.IsEmpty())
		parentControl = value_cast<IValueFrame>(vControl);
	else
		parentControl = this;

	return CValueForm::CreateControl(
		clsControl->GetString(),
		parentControl
	);
}

CValue CValueForm::FindControl(const CValue& vControl)
{
	IValueFrame* foundedControl = FindControlByName(vControl.GetString());

	if (foundedControl != NULL)
		return foundedControl;

	return CValue();
}

void CValueForm::RemoveControl(const CValue& vControl)
{
	if (appData->DesignerMode())
		return;

	//get parent obj
	IValueControl* currentControl =
		value_cast<IValueControl>(vControl);
	wxASSERT(currentControl);
	RemoveControl(currentControl);
}

//*************************************************************************************************
//*                                              Events                                           *
//*************************************************************************************************

void CValueForm::ShowForm(CDocument* doc, bool demoRun)
{
	CDocument* docParent = doc;

	if (CTranslateError::IsSimpleMode())
		return;

	if (m_valueFormDocument != NULL) {
		ActivateForm();
		return;
	}
	else if (!demoRun) {
		CValue::IncrRef();
	}

	if (m_formOwner != NULL &&
		doc == NULL) {
		CValueForm* ownerForm = m_formOwner->GetOwnerForm();
		wxASSERT(ownerForm);
		docParent = ownerForm->GetVisualDocument();
	}

	if (demoRun || appData->EnterpriseMode()) {
		CreateDocumentForm(docParent, demoRun);
	}
}

void CValueForm::ActivateForm()
{
	if (m_procUnit != NULL) {
		m_procUnit->CallFunction("onReOpen");
	}

	if (m_valueFormDocument != NULL) {
		m_valueFormDocument->Activate();
	}
}

void CValueForm::UpdateForm()
{
	if (CTranslateError::IsSimpleMode())
		return;

	if (m_valueFormDocument != NULL) {

		CVisualHost* visualView =
			m_valueFormDocument->GetVisualView();

		if (visualView != NULL) {
			visualView->UpdateFrame();
		}

		if (appData->EnterpriseMode()) {
			if (m_procUnit != NULL) {
				m_procUnit->CallFunction("refreshDisplay");
			}
		}
	}
}

bool CValueForm::CloseForm()
{
	if (CTranslateError::IsSimpleMode())
		return false;

	if (appData->EnterpriseMode()) {
		if (!CloseDocumentForm()) {
			return false;
		}
	}

	if (m_valueFormDocument != NULL) {
		if (m_valueFormDocument->OnSaveModified()) {
			m_valueFormDocument->DeleteAllViews();
			m_valueFormDocument = NULL;
			return true;
		}
		return false;
	}

	return true;
}

void CValueForm::HelpForm()
{
	wxMessageBox(
		_("Help will appear here sometime, but not today.")
	);
}

//**********************************************************************************
//*                                   Other                                        *
//**********************************************************************************

IValueFrame* CValueForm::CreateControl(const wxString& clsControl, IValueFrame* control)
{
	//get parent obj
	IValueFrame* parentControl = NULL;

	if (control != NULL)
		parentControl = control;
	else
		parentControl = this;

	// ademas, el objeto se insertara a continuacion del objeto seleccionado
	IValueFrame* newControl =
		CValueForm::CreateObject(clsControl, parentControl);

	if (!CTranslateError::IsSimpleMode()) {
		if (m_valueFormDocument != NULL) {
			CVisualHost* visualView =
				m_valueFormDocument->GetVisualView();
			visualView->CreateControl(newControl);
			//fix size in parent window 
			wxWindow* wndParent = visualView->GetParent();
			if (wndParent) {
				wndParent->Layout();
			}
		}
	}

	//return value 
	if (newControl->GetClassName() == wxT("sizerItem"))
		return newControl->GetChild(0);

	return newControl;
}

void CValueForm::RemoveControl(IValueFrame* control)
{
	//get parent obj
	IValueFrame* currentControl = control;
	wxASSERT(currentControl);
	if (!CTranslateError::IsSimpleMode()) {
		if (m_valueFormDocument != NULL) {
			CVisualHost* visualView = m_valueFormDocument->GetVisualView();
			visualView->RemoveControl(currentControl);

			//fix size in parent window 
			wxWindow* wndParent = visualView->GetParent();
			if (wndParent) wndParent->Layout();
		}
	}

	IValueFrame* parentControl =
		currentControl->GetParent();

	if (parentControl->GetClassName() == wxT("sizerItem")) {
		IValueFrame* parentOwner = parentControl->GetParent();
		if (parentOwner) {
			parentOwner->RemoveChild(parentControl);
		}
		parentControl->SetParent(NULL);
		parentControl->RemoveChild(currentControl);
		parentControl->DecrRef();

		currentControl->SetParent(NULL);
		currentControl->DecrRef();
	}
	else {
		IValueFrame* parentOwner = currentControl->GetParent();
		if (parentOwner) {
			parentOwner->RemoveChild(currentControl);
		}
		currentControl->SetParent(NULL);
		currentControl->DecrRef();
	}
}

void CValueForm::OnIdleHandler(wxTimerEvent& event)
{
	if (m_procUnit != NULL) {
		auto foundedIt = std::find_if(m_aIdleHandlers.begin(), m_aIdleHandlers.end(), [event](std::pair<wxString, wxTimer*> pair) {
			return pair.second == event.GetEventObject();
		});

		if (foundedIt != m_aIdleHandlers.end()) {
			CallFunction(foundedIt->first);
		}
	}

	event.Skip();
}

#include "utils/stringUtils.h"

void CValueForm::AttachIdleHandler(const wxString& procedureName, int interval, bool single)
{
	if (appData->DesignerMode())
		return;

	for (unsigned int i = 0; i < procedureName.length(); i++) {
		if (!((procedureName[i] >= 'A' && procedureName[i] <= 'Z') || (procedureName[i] >= 'a' && procedureName[i] <= 'z') ||
			(procedureName[i] >= 'А' && procedureName[i] <= 'Я') || (procedureName[i] >= 'а' && procedureName[i] <= 'я') ||
			(procedureName[i] >= '0' && procedureName[i] <= '9')))
		{
			return;
		}
	}

	if (m_procUnit && m_procUnit->FindFunction(procedureName, true)) {
		auto foundedIt = m_aIdleHandlers.find(procedureName);
		if (foundedIt == m_aIdleHandlers.end()) {
			wxTimer* timer = new wxTimer();
			timer->Bind(wxEVT_TIMER, &CValueForm::OnIdleHandler, this);
			if (timer->Start(interval * 1000, single)) {
				m_aIdleHandlers.insert_or_assign(procedureName, timer);
			}
		}
	}
}

void CValueForm::DetachIdleHandler(const wxString& procedureName)
{
	if (appData->DesignerMode())
		return;

	for (unsigned int i = 0; i < procedureName.length(); i++) {
		if (!((procedureName[i] >= 'A' && procedureName[i] <= 'Z') || (procedureName[i] >= 'a' && procedureName[i] <= 'z') ||
			(procedureName[i] >= 'А' && procedureName[i] <= 'Я') || (procedureName[i] >= 'а' && procedureName[i] <= 'я') ||
			(procedureName[i] >= '0' && procedureName[i] <= '9')))
		{
			return;
		}
	}

	if (m_procUnit && m_procUnit->FindFunction(procedureName, true)) {
		auto foundedIt = m_aIdleHandlers.find(procedureName);
		if (foundedIt != m_aIdleHandlers.end()) {
			wxTimer* timer = foundedIt->second;
			m_aIdleHandlers.erase(foundedIt);
			if (timer->IsRunning()) {
				timer->Stop();
			}
			timer->Unbind(wxEVT_TIMER, &CValueForm::OnIdleHandler, this);
			delete timer;
		}
	}
}

void CValueForm::ClearRecursive(IValueFrame* control)
{
	for (unsigned int idx = control->GetChildCount(); idx > 0; idx--) {
		IValueFrame* controlChild =
			dynamic_cast<IValueFrame*>(control->GetChild(idx - 1));
		ClearRecursive(controlChild);
		if (controlChild) {
			controlChild->DecrRef();
		}
	}
}