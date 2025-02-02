////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaform object
////////////////////////////////////////////////////////////////////////////

#include "metaFormObject.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/metaData.h"
#include "backend/metaCollection/partial/object.h"
#include "backend/appData.h"

//***********************************************************************
//*                             IMetaObjectForm metaData                *
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectForm, CMetaObjectModule);

//***********************************************************************
//*                          common value object                        *
//***********************************************************************

bool IMetaObjectForm::LoadFormData(IBackendValueForm* valueForm) {
	return valueForm->LoadForm(m_formData);
}

bool IMetaObjectForm::SaveFormData(IBackendValueForm* valueForm) {
	m_formData = valueForm->SaveForm();
	return !m_formData.IsEmpty();
}

//***********************************************************************

IBackendValueForm* IMetaObjectForm::GenerateForm(IBackendControlFrame* ownerControl,
	ISourceDataObject* ownerSrc, const CUniqueKey& guidForm)
{
	if (m_formData.IsEmpty()) {
		IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(
			ownerControl, this, ownerSrc, guidForm, m_propEnabled
		);
		//build form
		if (m_firstInitialized) {
			m_firstInitialized = false;
			valueForm->BuildForm(GetTypeForm());
			SaveFormData(valueForm);
		}
		return valueForm;
	}

	IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(
		ownerControl, this, ownerSrc, guidForm, m_propEnabled
	);

	if (!LoadFormData(valueForm)) {
		wxDELETE(valueForm);
	}

	return valueForm;
}

IBackendValueForm* IMetaObjectForm::GenerateFormAndRun(IBackendControlFrame* ownerControl,
	ISourceDataObject* ownerSrc, const CUniqueKey& guidForm)
{
	IBackendValueForm* valueForm = nullptr;
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);
	if (!moduleManager->FindCompileModule(this, valueForm)) {
		valueForm = GenerateForm(ownerControl, ownerSrc, guidForm);
		if (!valueForm->InitializeFormModule()) {
			wxDELETE(valueForm);
			return nullptr;
		}
	}

	return valueForm;
}

///////////////////////////////////////////////////////////////////////////

IMetaObjectForm::IMetaObjectForm(const wxString& name, const wxString& synonym, const wxString& comment) :
	CMetaObjectModule(name, synonym, comment), m_formData(), m_firstInitialized(false)
{
	//set default proc
	SetDefaultProcedure("beforeOpen", eContentHelper::eProcedureHelper, { "cancel" });
	SetDefaultProcedure("onOpen", eContentHelper::eProcedureHelper);
	SetDefaultProcedure("beforeClose", eContentHelper::eProcedureHelper, { "cancel" });
	SetDefaultProcedure("onClose", eContentHelper::eProcedureHelper);

	SetDefaultProcedure("onReOpen", eContentHelper::eProcedureHelper);
	SetDefaultProcedure("choiceProcessing", eContentHelper::eProcedureHelper, { { "selectedValue" }, { "choiceSource" } });
	SetDefaultProcedure("refreshDisplay", eContentHelper::eProcedureHelper);
}

#define chunkForm 0x023456543

bool IMetaObjectForm::LoadData(CMemoryReader& reader)
{
	reader.r_chunk(m_metaId + chunkForm, m_formData);
	reader.r_stringZ(m_moduleData);
	return true;
}

bool IMetaObjectForm::SaveData(CMemoryWriter& writer)
{
	writer.w_chunk(m_metaId + chunkForm, m_formData);
	writer.w_stringZ(m_moduleData);
	return true;
}

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectForm, IMetaObjectForm)

//***********************************************************************
//*                            Metaform                                 *
//***********************************************************************

CMetaObjectForm::CMetaObjectForm(const wxString& name, const wxString& synonym, const wxString& comment) : IMetaObjectForm(name, synonym, comment)
{
}

bool CMetaObjectForm::LoadData(CMemoryReader& reader)
{
	m_properyFormType->SetValue(reader.r_s32());
	return IMetaObjectForm::LoadData(reader);
}

bool CMetaObjectForm::SaveData(CMemoryWriter& writer)
{
	writer.w_s32(m_properyFormType->GetValueAsInteger());
	return IMetaObjectForm::SaveData(writer);
}

OptionList* CMetaObjectForm::GetFormType(PropertyOption*)
{
	IMetaObjectGenericData* metaObject = wxDynamicCast(
		GetParent(), IMetaObjectGenericData
	);

	wxASSERT(metaObject);

	OptionList* optionlist = metaObject->GetFormType();
	optionlist->AddOption(formDefaultName, defaultFormType);

	return optionlist;
}

//***********************************************************************
//*                             event object                            *
//***********************************************************************

bool CMetaObjectForm::OnCreateMetaObject(IMetaData* metaData)
{
	m_firstInitialized = true;

	if (!IMetaObjectForm::OnCreateMetaObject(metaData))
		return false;

	IMetaObjectGenericData* metaObject = wxDynamicCast(
		m_parent, IMetaObjectGenericData
	);
	
	wxASSERT(metaObject);
	
	form_identifier_t res = wxID_CANCEL;
	if (metaData != nullptr) {
		IBackendMetadataTree* metaTree = metaData->GetMetaTree();
		if (metaTree != nullptr) {
			res = metaTree->SelectFormType(this);
		}
	}
	if (res != wxID_CANCEL) {
		m_properyFormType->SetValue(res);
	}
	else {
		return false;
	}
	metaObject->OnCreateFormObject(this);
	if (metaData != nullptr) {
		IBackendMetadataTree* metaTree = metaData->GetMetaTree();
		if (metaTree != nullptr) {
			metaTree->UpdateChoiceSelection();
		}
	}

	return true;
}

bool CMetaObjectForm::OnLoadMetaObject(IMetaData* metaData)
{
	return IMetaObjectForm::OnLoadMetaObject(metaData);
}

bool CMetaObjectForm::OnSaveMetaObject()
{
	return IMetaObjectForm::OnSaveMetaObject();
}

bool CMetaObjectForm::OnDeleteMetaObject()
{
	IMetaObjectGenericData* metaObject = wxDynamicCast(m_parent, IMetaObjectGenericData);
	wxASSERT(metaObject);
	metaObject->OnRemoveMetaForm(this);

	return IMetaObjectForm::OnDeleteMetaObject();
}

bool CMetaObjectForm::OnBeforeRunMetaObject(int flags)
{
	return IMetaObjectForm::OnBeforeRunMetaObject(flags);
}

bool CMetaObjectForm::OnAfterRunMetaObject(int flags)
{
	if (appData->DesignerMode()) {

		IModuleManager* moduleManager = m_metaData->GetModuleManager();
		wxASSERT(moduleManager);
		IMetaObjectGenericData* metaObject = wxDynamicCast(m_parent, IMetaObjectGenericData);
		wxASSERT(metaObject);

		return moduleManager->AddCompileModule(this, formWrapper::inl::cast_value(metaObject->CreateObjectForm(this)));
	}

	return IMetaObjectForm::OnAfterRunMetaObject(flags);
}

bool CMetaObjectForm::OnBeforeCloseMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->RemoveCompileModule(this))
		return false;

	return IMetaObjectForm::OnBeforeCloseMetaObject();
}

bool CMetaObjectForm::OnAfterCloseMetaObject()
{
	return IMetaObjectForm::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                           CommonFormObject metaData                 *
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectCommonForm, IMetaObjectForm)

CMetaObjectCommonForm::CMetaObjectCommonForm(const wxString& name, const wxString& synonym, const wxString& comment) : IMetaObjectForm(name, synonym, comment) {}

//***********************************************************************
//*                             event object                            *
//***********************************************************************

bool CMetaObjectCommonForm::OnCreateMetaObject(IMetaData* metaData)
{
	m_firstInitialized = true;
	return CMetaObjectModule::OnCreateMetaObject(metaData);
}

bool CMetaObjectCommonForm::OnBeforeRunMetaObject(int flags)
{
	if (appData->DesignerMode()) {
		IModuleManager* moduleManager = m_metaData->GetModuleManager();
		wxASSERT(moduleManager);
		if (moduleManager->AddCompileModule(this, formWrapper::inl::cast_value(GenerateFormAndRun()))) {
			return CMetaObjectModule::OnBeforeRunMetaObject(flags);
		}
		return false;
	}
	return CMetaObjectModule::OnBeforeRunMetaObject(flags);
}

bool CMetaObjectCommonForm::OnAfterCloseMetaObject()
{
	if (appData->DesignerMode()) {
		IModuleManager* moduleManager = m_metaData->GetModuleManager();
		wxASSERT(moduleManager);
		if (moduleManager->RemoveCompileModule(this)) {
			return CMetaObjectModule::OnAfterCloseMetaObject();
		}
		return false;
	}
	return CMetaObjectModule::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(CMetaObjectForm, "baseForm", g_metaFormCLSID);
METADATA_TYPE_REGISTER(CMetaObjectCommonForm, "commonForm", g_metaCommonFormCLSID);