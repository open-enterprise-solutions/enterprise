////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "backend/metaData.h"

#define objectModule wxT("objectModule")

//***********************************************************************
//*                         metaData                                    * 
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectConstant, CMetaObjectAttribute)

//***********************************************************************
//*                         Attributes                                  * 
//***********************************************************************

CMetaObjectConstant::CMetaObjectConstant() : CMetaObjectAttribute()
{
	//create module
	m_moduleObject = new CMetaObjectModule(objectModule);

	//set child/parent
	m_moduleObject->SetParent(this);
	AddChild(m_moduleObject);

	//set default proc
	m_moduleObject->SetDefaultProcedure("beforeWrite", eContentHelper::eProcedureHelper, { "cancel" });
	m_moduleObject->SetDefaultProcedure("onWrite", eContentHelper::eProcedureHelper, { "cancel" });
}

CMetaObjectConstant::~CMetaObjectConstant()
{
	wxDELETE(m_moduleObject);
}

bool CMetaObjectConstant::LoadData(CMemoryReader& dataReader)
{
	//load object module
	m_moduleObject->LoadMeta(dataReader);

	return CMetaObjectAttribute::LoadData(dataReader);
}

bool CMetaObjectConstant::SaveData(CMemoryWriter& dataWritter)
{
	//save object module
	m_moduleObject->SaveMeta(dataWritter);

	return CMetaObjectAttribute::SaveData(dataWritter);
}

bool CMetaObjectConstant::DeleteData()
{
	return CMetaObjectAttribute::DeleteData();
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "backend/appData.h"

bool CMetaObjectConstant::OnCreateMetaObject(IMetaData* metaData)
{
	if (!CMetaObjectAttribute::OnCreateMetaObject(metaData))
		return false;

	return m_moduleObject->OnCreateMetaObject(metaData);
}

bool CMetaObjectConstant::OnLoadMetaObject(IMetaData* metaData)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!m_moduleObject->OnLoadMetaObject(metaData))
		return false;

	return CMetaObjectAttribute::OnLoadMetaObject(metaData);
}

bool CMetaObjectConstant::OnSaveMetaObject()
{
	if (!m_moduleObject->OnSaveMetaObject())
		return false;

	return CMetaObjectAttribute::OnSaveMetaObject();
}

bool CMetaObjectConstant::OnDeleteMetaObject()
{
	if (!m_moduleObject->OnDeleteMetaObject())
		return false;

	return CMetaObjectAttribute::OnDeleteMetaObject();
}

#include "backend/objCtor.h"

bool CMetaObjectConstant::OnBeforeRunMetaObject(int flags)
{
	if (!m_moduleObject->OnBeforeRunMetaObject(flags))
		return false;

	registerConstObject();
	registerManager();

	return CMetaObjectAttribute::OnBeforeRunMetaObject(flags);
}

bool CMetaObjectConstant::OnAfterRunMetaObject(int flags)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
	
		if (CMetaObjectAttribute::OnAfterRunMetaObject(flags))
			return moduleManager->AddCompileModule(m_moduleObject, CreateObjectValue());
		
		return false;
	}

	return CMetaObjectAttribute::OnAfterRunMetaObject(flags);
}

bool CMetaObjectConstant::OnBeforeCloseMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (appData->DesignerMode()) {
		
		if (moduleManager->RemoveCompileModule(m_moduleObject)) 
			return CMetaObjectAttribute::OnAfterCloseMetaObject();
		
		return false;
	}

	return CMetaObjectAttribute::OnBeforeCloseMetaObject();
}

bool CMetaObjectConstant::OnAfterCloseMetaObject()
{
	if (!m_moduleObject->OnAfterCloseMetaObject())
		return false;

	unregisterConstObject();
	unregisterManager();

	return CMetaObjectAttribute::OnAfterCloseMetaObject();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IBackendValueForm* CMetaObjectConstant::GetObjectForm()
{
	IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(nullptr, nullptr,
		CreateObjectValue(), m_metaGuid
	);
	valueForm->BuildForm(defaultFormType);
	valueForm->Modify(false);

	return valueForm;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(CMetaObjectConstant, "constant", g_metaConstantCLSID);
