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
#include "frontend/visualView/visualHostClient.h"

//*************************************************************************************************
//*                                    System attribute                                           *
//*************************************************************************************************

#include "toolBar.h"
#include "tableBox.h"

void ibValueForm::BuildForm(const ibFormID& formType)
{
	m_formType = formType;

	if (m_sourceObject != nullptr) {

		ibValue* prevSrcData = nullptr;

		ibValueToolbar* mainToolBar =
			wxDynamicCast(
				ibValueForm::CreateControl(wxT("Toolbar")), ibValueToolbar
			);

		mainToolBar->SetControlName(wxT("MainToolbar"));
		mainToolBar->SetActionSrc(FORM_ACTION);

		const ibValueMetaObjectGenericData* metaObjectValue = m_sourceObject->GetSourceMetaObject();

		ibValueModelTableBox* mainTableBox = nullptr;

		const ibActionCollection& actionData = ibValueForm::GetActionCollection(formType);
		for (unsigned int idx = 0; idx < actionData.GetCount(); idx++) {
			const ibActionID& action_id = actionData.GetID(idx);
			if (action_id != wxNOT_FOUND) {
				ibValue* currSrcData = actionData.GetSourceDataByID(action_id);
				if (currSrcData != prevSrcData
					&& prevSrcData != nullptr) {
					ibValueForm::CreateControl(wxT("ToolSeparator"), mainToolBar);
				}
				ibValueToolBarItem* toolBarItem =
					wxDynamicCast(
						ibValueForm::CreateControl(wxT("Tool"), mainToolBar), ibValueToolBarItem
					);
				toolBarItem->SetControlName(mainToolBar->GetControlName() + actionData.GetNameByID(action_id));
				//toolBarItem->SetCaption(actionData.GetCaptionByID(action_id));
				//toolBarItem->SetToolTip(actionData.GetCaptionByID(action_id));
				toolBarItem->SetAction(action_id);
				prevSrcData = currSrcData;
			}
			else {
				ibValueForm::CreateControl(wxT("ToolSeparator"), mainToolBar);
			}
		}

		const ibSourceExplorer& sourceExplorer = m_sourceObject->GetSourceExplorer();
		if (sourceExplorer.IsTableSection()) {

			mainTableBox =
				wxDynamicCast(
					ibValueForm::CreateControl(wxT("Tablebox")), ibValueModelTableBox
				);

			mainTableBox->SetControlName(sourceExplorer.GetSourceName());
			mainTableBox->SetSource(sourceExplorer.GetSourceId());
		}

		for (unsigned int idx = 0; idx < sourceExplorer.GetHelperCount(); idx++) {

			const ibSourceExplorer& nextSourceExplorer = sourceExplorer.GetHelper(idx);

			if (sourceExplorer.IsTableSection()) {
				ibValueModelTableBoxColumn* tableBoxColumn =
					wxDynamicCast(
						ibValueForm::CreateControl(wxT("TableboxColumn"), mainTableBox), ibValueModelTableBoxColumn
					);
				tableBoxColumn->SetControlName(mainTableBox->GetControlName() + nextSourceExplorer.GetSourceName());
				tableBoxColumn->SetVisibleColumn(nextSourceExplorer.IsVisible() || sourceExplorer.GetHelperCount() == 1);
				tableBoxColumn->SetSource(nextSourceExplorer.GetSourceId());
			}
			else
			{
				prevSrcData = nullptr;

				if (nextSourceExplorer.IsTableSection()) {

					ibValueToolbar* toolBar =
						wxDynamicCast(
							ibValueForm::CreateControl(wxT("Toolbar")), ibValueToolbar
						);

					toolBar->SetControlName(wxT("Toolbar") + nextSourceExplorer.GetSourceName());

					ibValueModelTableBox* tableBox =
						wxDynamicCast(
							ibValueForm::CreateControl(wxT("Tablebox")), ibValueModelTableBox
						);

					tableBox->SetControlName(nextSourceExplorer.GetSourceName());
					tableBox->SetSource(nextSourceExplorer.GetSourceId());

					toolBar->SetActionSrc(tableBox->GetControlID());

					ibActionCollection actionData = tableBox->GetActionCollection(formType);
					for (unsigned int idx = 0; idx < actionData.GetCount(); idx++) {
						const ibActionID& action_id = actionData.GetID(idx);
						if (action_id != wxNOT_FOUND) {
							ibValue* currSrcData = actionData.GetSourceDataByID(action_id);
							if (currSrcData != prevSrcData
								&& prevSrcData != nullptr) {
								ibValueForm::CreateControl(wxT("ToolSeparator"), toolBar);
							}
							ibValueToolBarItem* toolBarItem =
								wxDynamicCast(
									ibValueForm::CreateControl(wxT("Tool"), toolBar), ibValueToolBarItem
								);
							toolBarItem->SetControlName(toolBar->GetControlName() + actionData.GetNameByID(action_id));
							//toolBarItem->SetCaption(actionData.GetCaptionByID(action_id));
							//toolBarItem->SetToolTip(actionData.GetCaptionByID(action_id));
							toolBarItem->SetAction(action_id);
							prevSrcData = currSrcData;
						}
						else {
							ibValueForm::CreateControl(wxT("ToolSeparator"), toolBar);
						}
					}

					for (unsigned int col = 0; col < nextSourceExplorer.GetHelperCount(); col++) {
						const ibSourceExplorer& colSourceExplorer = nextSourceExplorer.GetHelper(col);

						ibValueModelTableBoxColumn* tableBoxColumn =
							wxDynamicCast(
								ibValueForm::CreateControl(wxT("TableboxColumn"), tableBox), ibValueModelTableBoxColumn
							);
						tableBoxColumn->SetControlName(tableBox->GetControlName() + colSourceExplorer.GetSourceName());
						//tableBoxColumn->SetCaption(colSourceExplorer.GetSourceSynonym());
						tableBoxColumn->SetVisibleColumn(colSourceExplorer.IsVisible()
							|| nextSourceExplorer.GetHelperCount() == 1);
						tableBoxColumn->SetSource(colSourceExplorer.GetSourceId());
					}
				}
				else {
					if (nextSourceExplorer.ContainType(ibValueTypes::TYPE_BOOLEAN)
						&& nextSourceExplorer.GetClsidList().size() == 1) {
						ibValueCheckbox* checkbox =
							wxDynamicCast(
								ibValueForm::CreateControl(wxT("Checkbox")), ibValueCheckbox
							);
						checkbox->SetControlName(nextSourceExplorer.GetSourceName());
						//checkbox->SetCaption(nextSourceExplorer.GetSourceSynonym());
						checkbox->EnableWindow(nextSourceExplorer.IsEnabled());
						checkbox->VisibleWindow(nextSourceExplorer.IsVisible());
						checkbox->SetSource(nextSourceExplorer.GetSourceId());
					}
					else {

						bool selButton = !nextSourceExplorer.ContainType(ibValueTypes::TYPE_BOOLEAN) &&
							!nextSourceExplorer.ContainType(ibValueTypes::TYPE_NUMBER) &&
							//!nextSourceExplorer.ContainType(ibValueTypes::TYPE_DATE) &&
							!nextSourceExplorer.ContainType(ibValueTypes::TYPE_STRING);

						if (nextSourceExplorer.GetClsidList().size() != 1)
							selButton = true;

						ibValueTextCtrl* textCtrl =
							wxDynamicCast(
								ibValueForm::CreateControl(wxT("Textctrl")), ibValueTextCtrl
							);
						textCtrl->SetControlName(nextSourceExplorer.GetSourceName());
						//textCtrl->SetCaption(nextSourceExplorer.GetSourceSynonym());
						textCtrl->EnableWindow(nextSourceExplorer.IsEnabled());
						textCtrl->VisibleWindow(nextSourceExplorer.IsVisible());
						textCtrl->SetSource(nextSourceExplorer.GetSourceId());

						textCtrl->SetSelectButton(selButton);
						textCtrl->SetOpenButton(false);
						textCtrl->SetClearButton(nextSourceExplorer.IsEnabled());
					}
				}
			}
		}
	}
	else {

		ibValueToolbar* mainToolBar =
			wxDynamicCast(
				ibValueForm::CreateControl(wxT("Toolbar")), ibValueToolbar
			);

		mainToolBar->SetControlName(wxT("MainToolbar"));
		mainToolBar->SetActionSrc(FORM_ACTION);

		ibValueModelTableBox* mainTableBox = nullptr;

		const ibActionCollection& actionData = ibValueForm::GetActionCollection(formType);
		for (unsigned int idx = 0; idx < actionData.GetCount(); idx++) {

			const ibActionID& id = actionData.GetID(idx);

			if (id != wxNOT_FOUND) {
				ibValueToolBarItem* toolBarItem =
					wxDynamicCast(
						ibValueForm::CreateControl(wxT("Tool"), mainToolBar), ibValueToolBarItem
					);
				toolBarItem->SetControlName(mainToolBar->GetControlName() + actionData.GetNameByID(id));
				//toolBarItem->SetCaption(actionData.GetCaptionByID(id));
				//toolBarItem->SetToolTip(actionData.GetCaptionByID(id));
				toolBarItem->SetAction(id);
			}
			else {
				ibValueForm::CreateControl(wxT("ToolSeparator"), mainToolBar);
			}
		}
	}
}

void ibValueForm::InitializeForm(const ibValueMetaObjectFormBase* creator,
	ibControlFrame* ownerControl, ibSourceDataObject* srcObject, const ibUniqueKey& formGuid)
{
	if (ownerControl != nullptr) ownerControl->ControlIncrRef();
	if (m_controlOwner != nullptr) m_controlOwner->ControlDecrRef();

	if (srcObject != nullptr) srcObject->SourceIncrRef();
	if (m_sourceObject != nullptr) m_sourceObject->SourceDecrRef();

	m_controlOwner = ownerControl;
	m_sourceObject = srcObject;
	m_metaFormObject = creator;

	m_formKey = CreateFormUniqueKey(ownerControl, srcObject, formGuid);

	if (creator != nullptr)
		m_formType = creator->GetTypeForm();

	//SetReadOnly(readOnly);
}

#include "backend/system/systemManager.h"

bool ibValueForm::InitializeFormModule()
{
	if (m_metaFormObject != nullptr) {

		if (!m_metaFormObject->AccessRight_Show()) {
			ibBackendAccessException::Error();
			return false;
		}

		const ibMetaData* metaData = m_metaFormObject->GetMetaData();
		wxASSERT(metaData);
		const ibValueModuleManager* moduleManager = metaData->GetModuleManager();
		wxASSERT(moduleManager);

		ibModuleDataObject* sourceObjectValue =
			dynamic_cast<ibModuleDataObject*>(m_sourceObject);

		if (m_compileModule == nullptr) {
			m_compileModule = new ibCompileModule(m_metaFormObject);
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
			catch (const ibBackendException* err) {
				if (!appData->DesignerMode())
					throw(err);
				return false;
			};

			if (m_procUnit == nullptr) {
				m_procUnit = new ibProcUnit();
				m_procUnit->SetParent(
					sourceObjectValue != nullptr ? sourceObjectValue->GetProcUnit() :
					moduleManager->GetProcUnit()
				);
			}

			m_procUnit->Execute(m_compileModule->m_cByteCode, true);
		}

		PrepareNames();
	}

#pragma region _control_guard_

	struct ibControlGuard {

		static bool Initialize(ibValueFrame* controlParent) {
			for (unsigned int idx = controlParent->GetChildCount(); idx > 0; idx--) {
				if (!Initialize(controlParent->GetChild(idx - 1)))
					return false;
			}
			return controlParent->InitializeControl();
		}
	};

	return ibControlGuard::Initialize(this);

#pragma endregion 
}

#include "backend/system/value/valueType.h"

void ibValueForm::NotifyCreate(const ibValue& vCreated)
{
	ibValueForm* ownerForm = m_controlOwner != nullptr ?
		m_controlOwner->GetOwnerForm() : nullptr;

	if (ownerForm != nullptr) {

		ownerForm->m_createdValue = vCreated;
		ownerForm->m_changedValue = wxEmptyValue;

		ownerForm->UpdateForm();
	}

	ibValueForm::UpdateForm();
	ibValueForm::Modify(false);
}

void ibValueForm::NotifyChange(const ibValue& vChanged)
{
	ibValueForm* ownerForm = m_controlOwner != nullptr ?
		m_controlOwner->GetOwnerForm() : nullptr;

	if (ownerForm != nullptr) {

		ownerForm->m_createdValue = wxEmptyValue;
		ownerForm->m_changedValue = vChanged;

		ownerForm->UpdateForm();
	}

	ibValueForm::UpdateForm();
	ibValueForm::Modify(false);
}

void ibValueForm::NotifyDelete(const ibValue& vChanged)
{
	ibValueForm* ownerForm = m_controlOwner != nullptr ?
		m_controlOwner->GetOwnerForm() : nullptr;

	if (ownerForm != nullptr) {

		ownerForm->m_createdValue = wxEmptyValue;
		ownerForm->m_changedValue = wxEmptyValue;

		ownerForm->UpdateForm();
	}

	ibValueForm::CloseForm(true);
}

void ibValueForm::NotifyChoice(ibValue& vSelected)
{
	ChoiceDocForm(vSelected);

	if (m_closeOnChoice)
		ibValueForm::CloseForm();
}

#include "backend/system/systemManager.h"

ibValue ibValueForm::CreateControl(const ibValueType* clsControl, const ibValue& vControl)
{
	if (appData->DesignerMode())
		return ibValue();

	if (!ibValue::IsRegisterCtor(clsControl->GetString(), ibCtorObjectType::ibCtorObjectType_object_control)) {
		ibBackendCoreException::Error(_("Error occurred while trying to create a form element!"));
	}

	//get parent obj
	ibValueFrame* parentControl = nullptr;

	if (!vControl.IsEmpty())
		parentControl = CastValue<ibValueFrame>(vControl);
	else
		parentControl = this;

	return ibValueForm::CreateControl(
		clsControl->GetString(),
		parentControl
	);
}

ibValue ibValueForm::FindControl(const ibValue& vControl)
{
	ibValueFrame* foundedControl = FindControlByName(vControl.GetString());
	if (foundedControl != nullptr)
		return foundedControl;
	return ibValue();
}

void ibValueForm::RemoveControl(const ibValue& vControl)
{
	if (appData->DesignerMode())
		return;

	//get parent obj
	ibValueControl* currentControl =
		CastValue<ibValueControl>(vControl);

	wxASSERT(currentControl);
	RemoveControl(currentControl);
}

//*************************************************************************************************
//*                                              Events                                           *
//*************************************************************************************************

void ibValueForm::ShowForm(ibBackendMetaDocument* doc, bool createContext)
{
	ibMetaDocument* docParent = static_cast<ibMetaDocument *>(doc);

	if (ibBackendException::IsEvalMode())
		return;

	ibFormVisualDocument* const ownerDocForm = GetVisualDocument();

	if (ownerDocForm != nullptr) {
		ActivateForm();
		return;
	}

	if (m_controlOwner != nullptr &&
		doc == nullptr) {
		docParent = m_controlOwner->GetVisualDocument();
	}

	if (!createContext || !appData->DesignerMode()) {
		CreateDocForm(docParent, createContext);
	}
}

void ibValueForm::UpdateForm()
{
	if (ibBackendException::IsEvalMode())
		return;

	ibFormVisualDocument* const ownerDocForm = GetVisualDocument();

	if (ownerDocForm != nullptr) {

		ibVisualHostClient* visualView = ownerDocForm->GetFirstView() ?
			ownerDocForm->GetFirstView()->GetVisualHost() : nullptr;

		if (visualView != nullptr) {
			visualView->Freeze();
			visualView->UpdateVisualHost();
			visualView->Thaw();
		}
	}
}

bool ibValueForm::CloseForm(bool force)
{
	if (ibBackendException::IsEvalMode())
		return false;

	if (!appData->DesignerMode()) {
		if (!force && !CloseDocForm()) {
			return false;
		}
	}

	ibFormVisualDocument* const ownerDocForm = GetVisualDocument();

	if (ownerDocForm != nullptr) {
		return ownerDocForm->DeleteAllViews();
	}

	return true;
}

void ibValueForm::HelpForm()
{
	wxMessageBox(
		_("Help will appear here sometime, but not today.")
	);
}

#include "frontend/win/dlgs/formEditor.h"

void ibValueForm::ChangeForm()
{
	ibDialogFormEditor dlg(this);
	dlg.ShowModal();
}

#include "frontend/win/dlgs/generation.h"

bool ibValueForm::GenerateForm(ibValueRecordDataObjectRef* obj) const
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = obj->GetMetaObject();
	wxASSERT(metaObject);
	ibMetaData* metaData = metaObject->GetMetaData();
	wxASSERT(metaData);

	ibDialogGeneration* selectDataType = new ibDialogGeneration(metaData, metaObject->GetGenerationDescription());

	ibMetaID sel_id = 0;
	if (selectDataType->ShowModal(sel_id)) {
		ibValueMetaObjectRecordDataMutableRef* meta = metaData->FindAnyObjectByFilter<ibValueMetaObjectRecordDataMutableRef>(sel_id);
		if (meta != nullptr) {
			ibValueRecordDataObjectRef* genObj = meta->CreateObjectValue(obj, true);
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

ibValueFrame* ibValueForm::CreateControl(const wxString& clsControl, ibValueFrame* control)
{
	//get parent obj
	ibValueFrame* parentControl = nullptr;

	if (control != nullptr)
		parentControl = control;
	else
		parentControl = this;

	// ademas, el objeto se insertara a continuacion del objeto seleccionado
	ibValueFrame* newControl = ibValueForm::CreateObject(clsControl, parentControl);
	wxASSERT(newControl);
	if (!ibBackendException::IsEvalMode()) {
		ibFormVisualDocument* const ownerDocForm = GetVisualDocument();
		if (ownerDocForm != nullptr) {
			ibVisualHostClient* visualView = ownerDocForm->GetFirstView() ?
				ownerDocForm->GetFirstView()->GetVisualHost() : nullptr;
			visualView->CreateControl(newControl);
		}
	}

	m_formCollectionControl->PrepareNames();

	//return value 
	if (newControl->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
		return newControl->GetChild(0);

	return newControl;
}

void ibValueForm::RemoveControl(ibValueFrame* control)
{
	//get parent obj
	ibValueFrame* currentControl = control;
	wxASSERT(currentControl);
	if (!ibBackendException::IsEvalMode()) {
		ibFormVisualDocument* const ownerDocForm = GetVisualDocument();
		if (ownerDocForm != nullptr) {
			ibVisualHostClient* visualView = ownerDocForm->GetFirstView() ?
				ownerDocForm->GetFirstView()->GetVisualHost() : nullptr;
			visualView->RemoveControl(currentControl);
		}
	}

	ibValueFrame* parentControl = currentControl->GetParent();

	if (parentControl->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		ibValueFrame* parentOwner = parentControl->GetParent();
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
		ibValueFrame* parentOwner = currentControl->GetParent();
		if (parentOwner != nullptr) {
			parentOwner->RemoveChild(currentControl);
		}
		currentControl->SetParent(nullptr);
		currentControl->DecrRef();
	}

	m_formCollectionControl->PrepareNames();
}

void ibValueForm::AttachIdleHandler(const wxString& procedureName, int interval, bool single)
{
	if (appData->DesignerMode())
		return;

	for (unsigned int i = 0; i < procedureName.length(); i++) {
		if (!((procedureName[i] >= 'A' && procedureName[i] <= 'Z') || (procedureName[i] >= 'a' && procedureName[i] <= 'z') ||
			(procedureName[i] >= L'\u0410' && procedureName[i] <= L'\u042F') || (procedureName[i] >= L'\u0430' && procedureName[i] <= L'\u044F') ||
			(procedureName[i] >= '0' && procedureName[i] <= '9')))
		{
			ibBackendCoreException::Error(_("Procedure can enter only numbers, letters and the symbol \"_\""));
			return;
		}
	}

	if (m_procUnit != nullptr && m_procUnit->FindMethod(procedureName, true)) {
		auto it = m_idleHandlerArray.find(procedureName);
		if (it == m_idleHandlerArray.end()) {
			wxTimer* timer = new wxTimer();
			timer->Bind(wxEVT_TIMER, &ibValueForm::OnIdleHandler, this);
			if (timer->Start(interval * 1000, single)) {
				m_idleHandlerArray.insert_or_assign(procedureName, timer);
			}
		}
	}
}

void ibValueForm::DetachIdleHandler(const wxString& procedureName)
{
	if (appData->DesignerMode())
		return;

	for (unsigned int i = 0; i < procedureName.length(); i++) {
		if (!((procedureName[i] >= 'A' && procedureName[i] <= 'Z') || (procedureName[i] >= 'a' && procedureName[i] <= 'z') ||
			(procedureName[i] >= L'\u0410' && procedureName[i] <= L'\u042F') || (procedureName[i] >= L'\u0430' && procedureName[i] <= L'\u044F') ||
			(procedureName[i] >= '0' && procedureName[i] <= '9')))
		{
			ibBackendCoreException::Error(_("Procedure can enter only numbers, letters and the symbol \"_\""));
			return;
		}
	}

	if (m_procUnit != nullptr && m_procUnit->FindMethod(procedureName, true)) {
		auto it = m_idleHandlerArray.find(procedureName);
		if (it != m_idleHandlerArray.end()) {
			wxTimer* timer = it->second;
			m_idleHandlerArray.erase(it);
			if (timer != nullptr && timer->IsRunning()) {
				timer->Stop();
			}
			timer->Unbind(wxEVT_TIMER, &ibValueForm::OnIdleHandler, this);
			delete timer;
		}
	}
}

void ibValueForm::ClearRecursive(ibValueFrame* control)
{
	for (unsigned int idx = control->GetChildCount(); idx > 0; idx--) {
		ibValueFrame* controlChild = control->GetChild(idx - 1);
		ClearRecursive(controlChild);
		if (controlChild != nullptr) {
			controlChild->DecrRef();
		}
	}
}