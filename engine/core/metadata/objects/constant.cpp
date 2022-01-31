////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "metadata/metadata.h"

#define objectModule wxT("objectModule")

//***********************************************************************
//*                         metadata                                    * 
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaConstantObject, CMetaAttributeObject)

//***********************************************************************
//*                         Attributes                                  * 
//***********************************************************************

CMetaConstantObject::CMetaConstantObject() : CMetaAttributeObject()
{
	//create module
	m_moduleObject = new CMetaModuleObject(objectModule);
	m_moduleObject->SetClsid(g_metaModuleCLSID);

	//set child/parent
	m_moduleObject->SetParent(this);
	AddChild(m_moduleObject);

	//set default proc
	m_moduleObject->SetDefaultProcedure("beforeWrite", eContentHelper::eProcedureHelper, { "cancel" });
	m_moduleObject->SetDefaultProcedure("onWrite", eContentHelper::eProcedureHelper, { "cancel" });
}

CMetaConstantObject::~CMetaConstantObject()
{
	wxDELETE(m_moduleObject);
}

bool CMetaConstantObject::LoadData(CMemoryReader &dataReader)
{
	//load object module
	m_moduleObject->LoadMeta(dataReader);

	return CMetaAttributeObject::LoadData(dataReader);
}

bool CMetaConstantObject::SaveData(CMemoryWriter &dataWritter)
{
	//save object module
	m_moduleObject->SaveMeta(dataWritter);

	return CMetaAttributeObject::SaveData(dataWritter);
}

bool CMetaConstantObject::DeleteData()
{
	return CMetaAttributeObject::DeleteData();
}

//***********************************************************************
//*                          Read&save property                         *
//***********************************************************************

void CMetaConstantObject::ReadProperty()
{
	CMetaAttributeObject::ReadProperty();
}

void CMetaConstantObject::SaveProperty()
{
	CMetaAttributeObject::SaveProperty();
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "appData.h"

bool CMetaConstantObject::OnCreateMetaObject(IMetadata *metaData)
{
	if (!CMetaAttributeObject::OnCreateMetaObject(metaData))
		return false;

	return m_moduleObject->OnCreateMetaObject(metaData);
}

bool CMetaConstantObject::OnLoadMetaObject(IMetadata *metaData)
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!m_moduleObject->OnLoadMetaObject(metaData))
		return false;

	return CMetaAttributeObject::OnLoadMetaObject(metaData);
}

bool CMetaConstantObject::OnSaveMetaObject()
{
	if (!m_moduleObject->OnSaveMetaObject())
		return false;

	return CMetaAttributeObject::OnSaveMetaObject();
}

bool CMetaConstantObject::OnDeleteMetaObject()
{
	if (!m_moduleObject->OnDeleteMetaObject())
		return false;

	return CMetaAttributeObject::OnDeleteMetaObject();
}

#include "metadata/singleMetaTypes.h"

bool CMetaConstantObject::OnRunMetaObject(int flags)
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!m_moduleObject->OnRunMetaObject(flags))
		return false;

	registerManager();

	if (appData->DesignerMode()) {
		return moduleManager->AddCompileModule(m_moduleObject, CreateObjectValue());
	}

	return CMetaAttributeObject::OnRunMetaObject(flags);
}

bool CMetaConstantObject::OnCloseMetaObject()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!m_moduleObject->OnCloseMetaObject())
		return false;

	unregisterManager();

	if (appData->DesignerMode()) {
		if (moduleManager->RemoveCompileModule(m_moduleObject)) {
			return CMetaAttributeObject::OnCloseMetaObject();
		}
		return false;
	}

	return CMetaAttributeObject::OnCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaConstantObject, "metaConstant", g_metaConstantCLSID);
