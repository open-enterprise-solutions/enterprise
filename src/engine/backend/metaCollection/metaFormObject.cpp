////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaform object
////////////////////////////////////////////////////////////////////////////

#include "metaFormObject.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/metaData.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/appData.h"

// -----------------------------------------------------------------------
// ibBackendCommandItem
// -----------------------------------------------------------------------

#include "backend/system/systemManager.h"

bool ibBackendCommandItem::ShowFormByCommandType(ibInterfaceCommandType cmdType)
{
	ibBackendValueForm* valueForm = nullptr;

	try {

		valueForm = GetFormByCommandType(cmdType);

		if (valueForm == nullptr)
			return false;

		valueForm->ShowForm();
	}
	catch (const ibBackendAccessException* err) {
		wxDELETE(valueForm);
		ibValueSystemFunction::Alert(err->GetErrorDescription());
		return false;
	}
	catch (const ibBackendException*) {
		wxDELETE(valueForm);
		return false;
	}

	return true;
}

// -----------------------------------------------------------------------
// ibValueMetaObjectFormBase
// -----------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibValueMetaObjectFormBase, ibValueMetaObjectModule);

//***********************************************************************
//*                          common value object                        *
//***********************************************************************

bool ibValueMetaObjectFormBase::LoadFormData(ibBackendValueForm* valueForm) const {
	return valueForm->LoadForm(GetFormData());
}

bool ibValueMetaObjectFormBase::SaveFormData(ibBackendValueForm* valueForm) {
	const wxMemoryBuffer& memoryBuffer = valueForm->SaveForm();
	SetFormData(memoryBuffer);
	return !memoryBuffer.IsEmpty();
}

//***********************************************************************

#pragma region _form_creator_h_

ibBackendValueForm* ibValueMetaObjectFormBase::CreateAndBuildForm(const ibValueMetaObjectFormBase* creator,
	ibBackendControlFrame* ownerControl, ibSourceDataObject* srcObject, const ibUniqueKey& formGuid)
{
	return CreateAndBuildForm(creator,
		creator != nullptr ? creator->GetTypeForm() : defaultFormType, ownerControl, srcObject, formGuid);
}

ibBackendValueForm* ibValueMetaObjectFormBase::CreateAndBuildForm(const ibValueMetaObjectFormBase* creator, const ibFormID& form_id,
	ibBackendControlFrame* ownerControl, ibSourceDataObject* srcObject, const ibUniqueKey& formGuid)
{
	ibBackendValueForm* result = nullptr;

	if (creator != nullptr) {
		const ibMetaData* metaData = creator->GetMetaData();
		wxASSERT(metaData);
		const ibValueModuleManager* moduleManager = metaData->GetModuleManager();
		wxASSERT(moduleManager);
		if (!moduleManager->FindCompileModule(creator, result)) {
			result = ibBackendValueForm::CreateNewForm(creator, ownerControl, srcObject, formGuid);
			if (!creator->GetFormData().IsEmpty() && !creator->LoadFormData(result)) {
				wxDELETE(result);
				return nullptr;
			}
			else if (creator->GetFormData().IsEmpty()) {
				result->BuildForm(form_id);
			}
		}
	}
	else {
		result = ibBackendValueForm::CreateNewForm(nullptr, ownerControl, srcObject, formGuid);
		result->BuildForm(form_id);
	}

	if (result != nullptr) {

		bool success = true;

		try {
			success = result->InitializeFormModule();
		}
		catch (...) {
			success = false;
		}

		if (!success) {
			wxDELETE(result);
			return nullptr;
		}

		if (srcObject != nullptr) result->Modify(srcObject->IsModified());
	}

	return result;
}

#pragma endregion

///////////////////////////////////////////////////////////////////////////

wxMemoryBuffer ibValueMetaObjectFormBase::CopyFormData() const
{
	ibBackendValueForm* valueForm = nullptr;
	ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);
	if (moduleManager->FindCompileModule(this, valueForm))
		return valueForm->SaveForm();
	return wxMemoryBuffer();
}

bool ibValueMetaObjectFormBase::PasteFormData()
{
	return true;
}

///////////////////////////////////////////////////////////////////////////

ibValueMetaObjectFormBase::ibValueMetaObjectFormBase(const wxString& name, const wxString& synonym, const wxString& comment) :
	ibValueMetaObjectModuleBase(name, synonym, comment)
{
	//set default proc
	SetDefaultProcedure(wxT("BeforeOpen"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	SetDefaultProcedure(wxT("OnOpen"), ibContentHelper::eProcedureHelper);
	SetDefaultProcedure(wxT("BeforeClose"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	SetDefaultProcedure(wxT("OnClose"), ibContentHelper::eProcedureHelper);

	SetDefaultProcedure(wxT("OnReOpen"), ibContentHelper::eProcedureHelper);
	SetDefaultProcedure(wxT("ChoiceProcessing"), ibContentHelper::eProcedureHelper, { { wxT("SelectedValue") }, { wxT("ChoiceSource") } });
	SetDefaultProcedure(wxT("RefreshDisplay"), ibContentHelper::eProcedureHelper);
}

wxIMPLEMENT_DYNAMIC_CLASS(ibValueMetaObjectForm, ibValueMetaObjectFormBase)

//***********************************************************************
//*                            Metaform                                 *
//***********************************************************************

ibValueMetaObjectForm::ibValueMetaObjectForm(const wxString& name, const wxString& synonym, const wxString& comment) : ibValueMetaObjectFormBase(name, synonym, comment)
{
}

bool ibValueMetaObjectForm::LoadData(ibReaderMemory& reader)
{
	m_properyFormType->SetValue(reader.r_s32());
	return m_propertyForm->LoadData(reader);
}

bool ibValueMetaObjectForm::SaveData(ibWriterMemory& writer)
{
	writer.w_s32(m_properyFormType->GetValueAsInteger());
	return m_propertyForm->SaveData(writer);
}

//***********************************************************************

bool ibValueMetaObjectForm::FillGenericFormType(ibPropertyList* prop)
{
	const ibValueMetaObjectGenericData* geneticObject = m_parent != nullptr ?
		m_parent->ConvertToType<ibValueMetaObjectGenericData>() : nullptr;

	if (geneticObject != nullptr) {

		ibFormTypeList formList = geneticObject->GetFormType();
		for (unsigned int idx = 0; idx < formList.GetItemCount(); idx++) {
			prop->AppendItem(
				formList.GetItemName(idx),
				formList.GetItemLabel(idx),
				formList.GetItemHelp(idx),
				formList.GetItemId(idx),
				GetIcon(),
				formList.GetItemName(idx)
			);
		}

		return true;
	}

	return false;
}

//***********************************************************************
//*                             event object                            *
//***********************************************************************

bool ibValueMetaObjectForm::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectFormBase::OnCreateMetaObject(metaData, flags))
		return false;

	if ((flags & newObjectFlag) != 0) {

		ibValueMetaObjectGenericData* metaObject = wxDynamicCast(
			m_parent, ibValueMetaObjectGenericData
		);

		wxASSERT(metaObject);

		ibFormID res = wxNOT_FOUND;
		if (metaData != nullptr) {
			ibBackendMetadataTree* metaTree = metaData->GetMetaTree();
			if (metaTree != nullptr) {
				res = metaTree->SelectFormType(this);
			}
		}
		if (res != wxNOT_FOUND) {
			m_properyFormType->SetValue(res);
		}
		else {
			return false;
		}
		metaObject->OnCreateFormObject(this);
		if (metaData != nullptr) {
			ibBackendMetadataTree* metaTree = metaData->GetMetaTree();
			if (metaTree != nullptr) {
				metaTree->UpdateChoiceSelection();
			}
		}
	}

	return true;
}

bool ibValueMetaObjectForm::OnLoadMetaObject(ibMetaData* metaData)
{
	return ibValueMetaObjectFormBase::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectForm::OnSaveMetaObject(int flags)
{
	return ibValueMetaObjectFormBase::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectForm::OnDeleteMetaObject()
{
	ibValueMetaObjectGenericData* metaObject = wxDynamicCast(m_parent, ibValueMetaObjectGenericData);
	wxASSERT(metaObject);
	metaObject->OnRemoveMetaForm(this);

	return ibValueMetaObjectFormBase::OnDeleteMetaObject();
}

bool ibValueMetaObjectForm::OnBeforeRunMetaObject(int flags)
{
	return ibValueMetaObjectFormBase::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectForm::OnAfterRunMetaObject(int flags)
{
	if (appData->DesignerMode()) {

		ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
		wxASSERT(moduleManager);
		ibValueMetaObjectGenericData* metaObject = wxDynamicCast(m_parent, ibValueMetaObjectGenericData);
		wxASSERT(metaObject);

		return moduleManager->AddCompileModule(this, formWrapper::inl::cast_value(metaObject->CreateObjectForm(this)));
	}

	return ibValueMetaObjectFormBase::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectForm::OnBeforeCloseMetaObject()
{
	ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->RemoveCompileModule(this))
		return false;

	return ibValueMetaObjectFormBase::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectForm::OnAfterCloseMetaObject()
{
	return ibValueMetaObjectFormBase::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                           CommonFormObject metaData                 *
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(ibValueMetaObjectCommonForm, ibValueMetaObjectFormBase)

ibValueMetaObjectCommonForm::ibValueMetaObjectCommonForm(const wxString& name, const wxString& synonym, const wxString& comment) : ibValueMetaObjectFormBase(name, synonym, comment) {}

bool ibValueMetaObjectCommonForm::LoadData(ibReaderMemory& reader)
{
	return m_propertyForm->LoadData(reader);
}

bool ibValueMetaObjectCommonForm::SaveData(ibWriterMemory& writer)
{
	return m_propertyForm->SaveData(writer);
}

#include "backend/system/systemManager.h"

ibBackendValueForm* ibValueMetaObjectCommonForm::GetObjectForm(ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	if (!AccessRight_Use()) {
		ibBackendAccessException::Error();
		return nullptr;
	}

	return ibValueMetaObjectFormBase::CreateAndBuildForm(
		this,
		ownerControl,
		nullptr,
		formGuid
	);
}

//***********************************************************************
//*                             event object                            *
//***********************************************************************

bool ibValueMetaObjectCommonForm::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	return ibValueMetaObjectModuleBase::OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectCommonForm::OnBeforeRunMetaObject(int flags)
{
	return ibValueMetaObjectModuleBase::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectCommonForm::OnAfterRunMetaObject(int flags)
{
	if (appData->DesignerMode()) {
		ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
		wxASSERT(moduleManager);
		if (moduleManager->AddCompileModule(this, formWrapper::inl::cast_value(ibValueMetaObjectFormBase::CreateAndBuildForm(this, defaultFormType)))) {
			return ibValueMetaObjectModuleBase::OnAfterRunMetaObject(flags);
		}
		return false;
	}
	return ibValueMetaObjectModuleBase::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectCommonForm::OnBeforeCloseMetaObject()
{
	if (appData->DesignerMode()) {
		ibValueModuleManager* moduleManager = m_metaData->GetModuleManager();
		wxASSERT(moduleManager);
		if (moduleManager->RemoveCompileModule(this)) {
			return ibValueMetaObjectModuleBase::OnBeforeCloseMetaObject();
		}
		return false;
	}

	return ibValueMetaObjectModuleBase::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectCommonForm::OnAfterCloseMetaObject()
{
	return ibValueMetaObjectModuleBase::OnAfterCloseMetaObject();
}


//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectForm, "Form", g_metaFormCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectCommonForm, "CommonForm", g_metaCommonFormCLSID);