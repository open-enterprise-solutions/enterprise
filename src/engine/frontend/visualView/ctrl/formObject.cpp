////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame object
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "backend/appData.h"
#include "backend/metaData.h"
#include "frontend/docView/docManager.h"
#include "backend/srcExplorer.h"
#include "backend/moduleManager/moduleManager.h"
#include "frontend/visualView/visualHost.h"

//*************************************************************************************************
//*                                    System attribute                                           *
//*************************************************************************************************

#include "toolBar.h"
#include "tableBox.h"

void CValueForm::BuildForm(const form_identifier_t& formType)
{
	m_defaultFormType = formType;

	if (m_sourceObject != nullptr) {

		CValue* prevSrcData = nullptr;

		CValueToolbar* mainToolBar =
			wxDynamicCast(
				CValueForm::CreateControl(wxT("toolbar")), CValueToolbar
			);

		mainToolBar->SetControlName(wxT("mainToolbar"));
		mainToolBar->SetActionSrc(FORM_ACTION);

		IMetaObjectGenericData* metaObjectValue =
			m_sourceObject->GetSourceMetaObject();

		CValueTableBox* mainTableBox = nullptr;

		const actionData_t& actionData = CValueForm::GetActions(formType);
		for (unsigned int idx = 0; idx < actionData.GetCount(); idx++) {
			const action_identifier_t& id = actionData.GetID(idx);
			if (id != wxNOT_FOUND) {
				CValue* currSrcData = actionData.GetSourceDataByID(id);
				if (currSrcData != prevSrcData
					&& prevSrcData != nullptr) {
					CValueForm::CreateControl(wxT("toolSeparator"), mainToolBar);
				}
				CValueToolBarItem* toolBarItem =
					wxDynamicCast(
						CValueForm::CreateControl(wxT("tool"), mainToolBar), CValueToolBarItem
					);
				toolBarItem->SetControlName(mainToolBar->GetControlName() + wxT("_") + actionData.GetNameByID(id));
				toolBarItem->SetCaption(actionData.GetCaptionByID(id));
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
				tableBoxColumn->SetVisibleColumn(nextSourceExplorer.IsVisible()
					|| sourceExplorer.GetHelperCount() == 1);
				tableBoxColumn->SetSourceId(nextSourceExplorer.GetMetaIDSource());
			}
			else
			{
				prevSrcData = nullptr;

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

					actionData_t actionData = tableBox->GetActions(formType);
					for (unsigned int idx = 0; idx < actionData.GetCount(); idx++) {
						const action_identifier_t& id = actionData.GetID(idx);
						if (id != wxNOT_FOUND) {
							CValue* currSrcData = actionData.GetSourceDataByID(id);
							if (currSrcData != prevSrcData
								&& prevSrcData != nullptr) {
								CValueForm::CreateControl(wxT("toolSeparator"), toolBar);
							}
							CValueToolBarItem* toolBarItem =
								wxDynamicCast(
									CValueForm::CreateControl(wxT("tool"), toolBar), CValueToolBarItem
								);
							toolBarItem->SetControlName(toolBar->GetControlName() + wxT("_") + actionData.GetNameByID(id));
							toolBarItem->SetCaption(actionData.GetCaptionByID(id));
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
						textCtrl->SetOpenButton(false);
						textCtrl->SetClearButton(nextSourceExplorer.IsEnabled());
					}
				}
			}
		}

		if (metaObjectValue != nullptr)
			SetCaption(metaObjectValue->GetSynonym());
	}
	else {
		CValueToolbar* mainToolBar =
			wxDynamicCast(
				CValueForm::CreateControl(wxT("toolbar")), CValueToolbar
			);

		mainToolBar->SetControlName(wxT("mainToolbar"));
		mainToolBar->SetActionSrc(FORM_ACTION);

		CValueTableBox* mainTableBox = nullptr;

		actionData_t actionData = CValueForm::GetActions(formType);
		for (unsigned int idx = 0; idx < actionData.GetCount(); idx++) {
			CValueToolBarItem* toolBarItem =
				wxDynamicCast(
					CValueForm::CreateControl(wxT("tool"), mainToolBar), CValueToolBarItem
				);
			action_identifier_t id = actionData.GetID(idx);
			toolBarItem->SetControlName(wxT("tool_") + actionData.GetNameByID(id));
			toolBarItem->SetCaption(actionData.GetCaptionByID(id));
			toolBarItem->SetAction(wxString::Format("%i", id));
		}

		SetCaption(m_metaFormObject ?
			m_metaFormObject->GetSynonym() :
			wxEmptyString);
	}
}

void CValueForm::InitializeForm(IControlFrame* ownerControl,
	IMetaObjectForm* metaForm, ISourceDataObject* ownerSrc, const CUniqueKey& formGuid, bool readOnly)
{
	if (ownerSrc != nullptr) {
		ownerSrc->IncrRef();
	}

	if (m_sourceObject != nullptr) {
		m_sourceObject->DecrRef();
		m_sourceObject = nullptr;
	}

	if (m_valueFormDocument != nullptr) {
		m_valueFormDocument->Close();
		m_valueFormDocument = nullptr;
	}

	m_controlOwner = ownerControl;
	m_sourceObject = ownerSrc;
	m_metaFormObject = metaForm;

	if (formGuid.isValid()) { // 1. if set guid from user
		m_formKey = formGuid;
	}
	else if (ownerControl != nullptr) { // 2. if set guid in owner
		m_formKey = ownerControl->GetControlGuid();
	}
	else if (m_sourceObject != nullptr) { //3. if set guid in object 
		m_formKey = m_sourceObject->GetGuid();
	}
	else { //4. just generate 
		m_formKey = wxNewUniqueGuid;
	}

	if (metaForm != nullptr) {
		m_defaultFormType = metaForm->GetTypeForm();
	}

	SetReadOnly(readOnly);
}

bool CValueForm::InitializeFormModule()
{
	IMetaData* metaData = m_metaFormObject->GetMetaData();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	IModuleInfo* sourceObjectValue =
		dynamic_cast<IModuleInfo*>(
			m_sourceObject
			);

	if (m_compileModule == nullptr) {
		m_compileModule = new CCompileModule(m_metaFormObject);
		m_compileModule->SetParent(
			sourceObjectValue != nullptr ? sourceObjectValue->GetCompileModule() :
			moduleManager->GetCompileModule()
		);
		m_compileModule->AddContextVariable(thisForm, this);
	}

	if (!appData->DesignerMode()) {

		try {
			m_compileModule->Compile();
		}
		catch (const CBackendException*) {

			if (!appData->DesignerMode())
				throw(std::exception());

			return false;
		};

		if (m_procUnit == nullptr) {
			m_procUnit = new CProcUnit();
			m_procUnit->SetParent(
				sourceObjectValue != nullptr ? sourceObjectValue->GetProcUnit() :
				moduleManager->GetProcUnit()
			);
		}

		m_procUnit->Execute(m_compileModule->m_cByteCode, true);
	}

	return true;
}

#include "backend/compiler/value/valueType.h"

void CValueForm::NotifyChoice(CValue& vSelected)
{
	if (m_controlOwner != nullptr) {
		CValueForm* ownerForm = m_controlOwner->GetOwnerForm();
		if (ownerForm != nullptr)
			ownerForm->CallAsEvent("choiceProcessing", vSelected, GetValue());
		m_controlOwner->ChoiceProcessing(vSelected);
		if (ownerForm != nullptr)
			ownerForm->UpdateForm();
	}

	CValueForm::CloseForm();
}

#include "backend/systemManager/systemManager.h"

CValue CValueForm::CreateControl(const CValueType* clsControl, const CValue& vControl)
{
	if (appData->DesignerMode())
		return CValue();

	if (!CValue::IsRegisterCtor(clsControl->GetString(), eCtorObjectType::eCtorObjectType_object_control)) {
		CSystemFunction::Raise(_("Ñlass does not belong to control!"));
	}

	//get parent obj
	IValueFrame* parentControl = nullptr;

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
	if (foundedControl != nullptr)
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

void CValueForm::ShowForm(IBackendMetaDocument* doc, bool demoRun)
{
	CMetaDocument* docParent = wxDynamicCast(doc, CMetaDocument);

	if (CBackendException::IsEvalMode())
		return;

	if (m_valueFormDocument != nullptr) {
		ActivateForm();
		return;
	}
	else if (!demoRun) {
		CValue::IncrRef();
	}

	if (m_controlOwner != nullptr &&
		doc == nullptr) {
		docParent = m_controlOwner->GetVisualDocument();
	}

	if (demoRun || !appData->DesignerMode()) {
		CreateDocForm(docParent, demoRun);
	}
}

void CValueForm::ActivateForm()
{
	if (m_procUnit != nullptr) {
		m_procUnit->CallAsProc("onReOpen");
	}

	if (m_valueFormDocument != nullptr) {
		m_valueFormDocument->Activate();
	}
}

void CValueForm::UpdateForm()
{
	if (CBackendException::IsEvalMode())
		return;

	if (m_valueFormDocument != nullptr) {

		CVisualHost* visualView =
			m_valueFormDocument->GetVisualView();

		if (visualView != nullptr) {
			visualView->Freeze();
			visualView->UpdateFrame();
			visualView->Thaw();
		}

		if (!appData->DesignerMode()) {
			if (m_procUnit != nullptr) {
				m_procUnit->CallAsProc("refreshDisplay");
			}
		}
	}
}

bool CValueForm::CloseForm()
{
	if (CBackendException::IsEvalMode())
		return false;

	if (!appData->DesignerMode()) {
		if (!CloseDocForm()) {
			return false;
		}
	}

	if (m_valueFormDocument != nullptr) {
		if (m_valueFormDocument->OnSaveModified()) {
			m_valueFormDocument->DeleteAllViews();
			m_valueFormDocument = nullptr;
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

#include "frontend/win/dlgs/generation.h"

bool CValueForm::GenerateForm(IRecordDataObjectRef* obj) const
{
	IMetaObjectRecordDataMutableRef* metaObject = obj->GetMetaObject();
	wxASSERT(metaObject);
	IMetaData* metaData = metaObject->GetMetaData();
	wxASSERT(metaData);

	CDialogGeneration* selectDataType = new CDialogGeneration(metaData, metaObject->GetGenMetaId());

	meta_identifier_t sel_id = 0;
	if (selectDataType->ShowModal(sel_id)) {
		IMetaObjectRecordDataMutableRef* meta = nullptr;
		if (metaData->GetMetaObject(meta, sel_id)) {
			IRecordDataObjectRef* genObj = meta->CreateObjectValue(obj, true);
			if (genObj != nullptr) {
				genObj->ShowFormValue();
				selectDataType->Destroy();
				return true;
			}

		}
		selectDataType->Destroy();
		return false;
	}
	selectDataType->Destroy();
	return false;
}

//**********************************************************************************
//*                                   Other                                        *
//**********************************************************************************

IValueFrame* CValueForm::CreateControl(const wxString& clsControl, IValueFrame* control)
{
	//get parent obj
	IValueFrame* parentControl = nullptr;

	if (control != nullptr)
		parentControl = control;
	else
		parentControl = this;

	// ademas, el objeto se insertara a continuacion del objeto seleccionado
	IValueFrame* newControl =
		CValueForm::CreateObject(clsControl, parentControl);
	wxASSERT(newControl);
	if (!CBackendException::IsEvalMode()) {
		if (m_valueFormDocument != nullptr) {
			CVisualHost* visualView =
				m_valueFormDocument->GetVisualView();
			visualView->CreateControl(newControl);
			//fix size in parent window 
			wxWindow* wndParent = visualView->GetParent();
			if (wndParent != nullptr)
				wndParent->Layout();
		}
	}

	m_formControls->PrepareNames();
	m_formData->PrepareNames();

	//return value 
	if (newControl->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
		return newControl->GetChild(0);

	return newControl;
}

void CValueForm::RemoveControl(IValueFrame* control)
{
	//get parent obj
	IValueFrame* currentControl = control;
	wxASSERT(currentControl);
	if (!CBackendException::IsEvalMode()) {
		if (m_valueFormDocument != nullptr) {
			CVisualHost* visualView = m_valueFormDocument->GetVisualView();
			visualView->RemoveControl(currentControl);
			//fix size in parent window 
			wxWindow* wndParent = visualView->GetParent();
			if (wndParent != nullptr)
				wndParent->Layout();
		}
	}

	IValueFrame* parentControl =
		currentControl->GetParent();

	if (parentControl->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		IValueFrame* parentOwner = parentControl->GetParent();
		if (parentOwner != nullptr) {
			parentOwner->RemoveChild(parentControl);
		}
		parentControl->SetParent(nullptr);
		parentControl->RemoveChild(currentControl);
		parentControl->DecrRef();

		currentControl->SetParent(nullptr);
		currentControl->DecrRef();
	}
	else {
		IValueFrame* parentOwner = currentControl->GetParent();
		if (parentOwner != nullptr) {
			parentOwner->RemoveChild(currentControl);
		}
		currentControl->SetParent(nullptr);
		currentControl->DecrRef();
	}

	m_formControls->PrepareNames();
	m_formData->PrepareNames();
}

void CValueForm::OnIdleHandler(wxTimerEvent& event)
{
	if (m_procUnit != nullptr) {
		auto& it = std::find_if(m_aIdleHandlers.begin(), m_aIdleHandlers.end(), [event](std::pair<wxString, wxTimer*> pair) {
			return pair.second == event.GetEventObject();
			}
		);
		if (it != m_aIdleHandlers.end()) CallAsEvent(it->first);

	}

	event.Skip();
}


void CValueForm::AttachIdleHandler(const wxString& procedureName, int interval, bool single)
{
	if (appData->DesignerMode())
		return;

	for (unsigned int i = 0; i < procedureName.length(); i++) {
		if (!((procedureName[i] >= 'A' && procedureName[i] <= 'Z') || (procedureName[i] >= 'a' && procedureName[i] <= 'z') ||
			(procedureName[i] >= 'À' && procedureName[i] <= 'ß') || (procedureName[i] >= 'à' && procedureName[i] <= 'ÿ') ||
			(procedureName[i] >= '0' && procedureName[i] <= '9')))
		{
			CBackendException::Error("Procedure can enter only numbers, letters and the symbol \"_\"");
			return;
		}
	}

	if (m_procUnit != nullptr && m_procUnit->FindMethod(procedureName, true)) {
		auto& it = m_aIdleHandlers.find(procedureName);
		if (it == m_aIdleHandlers.end()) {
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
			(procedureName[i] >= 'À' && procedureName[i] <= 'ß') || (procedureName[i] >= 'à' && procedureName[i] <= 'ÿ') ||
			(procedureName[i] >= '0' && procedureName[i] <= '9')))
		{
			CBackendException::Error("Procedure can enter only numbers, letters and the symbol \"_\"");
			return;
		}
	}

	if (m_procUnit != nullptr && m_procUnit->FindMethod(procedureName, true)) {
		auto& it = m_aIdleHandlers.find(procedureName);
		if (it != m_aIdleHandlers.end()) {
			wxTimer* timer = it->second;
			m_aIdleHandlers.erase(it);
			if (timer != nullptr && timer->IsRunning()) {
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
		if (controlChild != nullptr) {
			controlChild->DecrRef();
		}
	}
}