////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject common metaData
////////////////////////////////////////////////////////////////////////////

#include "metaObjectMetadata.h"
#include "metaModuleObject.h"

#define initModuleName wxT("configurationModule")

//*****************************************************************************************
//*                         metaData													  * 
//*****************************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObject, IMetaObject);

//*****************************************************************************************
//*                                  MetadataObject                                       *
//*****************************************************************************************

CMetaObject::CMetaObject() : IMetaObject(configurationDefaultName)
{
	m_metaId = defaultMetaID;
	m_commonModule = new CMetaObjectModule(initModuleName);

	//set child/parent
	m_commonModule->SetParent(this);
	AddChild(m_commonModule);

	//set default proc
	m_commonModule->SetDefaultProcedure("beforeStart", eContentHelper::eProcedureHelper, { "cancel" });
	m_commonModule->SetDefaultProcedure("onStart", eContentHelper::eProcedureHelper);
	m_commonModule->SetDefaultProcedure("beforeExit", eContentHelper::eProcedureHelper, { "cancel" });
	m_commonModule->SetDefaultProcedure("onExit", eContentHelper::eProcedureHelper);
}

CMetaObject::~CMetaObject()
{
	wxDELETE(m_commonModule);
}

bool CMetaObject::LoadData(CMemoryReader& dataReader)
{
	m_propertyVersion->SetValue(dataReader.r_s32());
	return m_commonModule->LoadMeta(dataReader);
}

bool CMetaObject::SaveData(CMemoryWriter& dataWritter)
{
	dataWritter.w_s32(m_propertyVersion->GetValueAsInteger());
	return m_commonModule->SaveMeta(dataWritter);
}

//***********************************************************************
//*                          common value object                        *
//***********************************************************************

#include "backend/metaData.h"

bool CMetaObject::OnCreateMetaObject(IMetaData* metaData)
{
	if (!m_commonModule->OnCreateMetaObject(metaData)) {
		return false;
	}

	return IMetaObject::OnCreateMetaObject(metaData);
}

bool CMetaObject::OnLoadMetaObject(IMetaData* metaData)
{
	if (!m_commonModule->OnLoadMetaObject(metaData)) {
		return false;
	}

	return IMetaObject::OnLoadMetaObject(metaData);
}

bool CMetaObject::OnSaveMetaObject()
{
	if (!m_commonModule->OnSaveMetaObject()) {
		return false;
	}

	return IMetaObject::OnSaveMetaObject();
}

bool CMetaObject::OnDeleteMetaObject()
{
	if (!m_commonModule->OnDeleteMetaObject()) {
		return false;
	}

	return IMetaObject::OnDeleteMetaObject();
}

bool CMetaObject::OnBeforeRunMetaObject(int flags)
{
	if (!m_commonModule->OnBeforeRunMetaObject(flags))
		return false;

	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->AddCompileModule(m_commonModule, moduleManager))
		return false;

	return IMetaObject::OnBeforeRunMetaObject(flags);
}

bool CMetaObject::OnAfterCloseMetaObject()
{
	if (!m_commonModule->OnAfterCloseMetaObject())
		return false;

	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->RemoveCompileModule(m_commonModule))
		return false;

	return IMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(CMetaObject, "commonMetadata", g_metaCommonMetadataCLSID);
