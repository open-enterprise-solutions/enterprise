////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject common metadata
////////////////////////////////////////////////////////////////////////////

#include "metaObjectMetadata.h"
#include "metaModuleObject.h"

#define initModuleName wxT("initModule")

//***********************************************************************
//*                         metadata                                    * 
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObject, IMetaObject)

//*****************************************************************************************
//*                                  MetadataObject                                       *
//*****************************************************************************************

CMetaObject::CMetaObject() : IMetaObject(configurationDefaultName)
{
	m_metaId = defaultMetaID;

	PropertyContainer *moduleCategory = IObjectBase::CreatePropertyContainer("Compatibility");
	moduleCategory->AddProperty("version", PropertyType::PT_OPTION, &CMetaObject::GetVersions);
	m_category->AddCategory(moduleCategory);

	m_commonModule = new CMetaModuleObject(initModuleName);
	m_commonModule->SetClsid(g_metaModuleCLSID);

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

bool CMetaObject::LoadData(CMemoryReader &dataReader)
{
	return m_commonModule->LoadMeta(dataReader);
}

bool CMetaObject::SaveData(CMemoryWriter &dataWritter)
{
	return m_commonModule->SaveMeta(dataWritter);
}

//***********************************************************************
//*                          common value object                        *
//***********************************************************************

#include "metadata/metadata.h"

bool CMetaObject::OnCreateMetaObject(IMetadata *metaData)
{
	if (!m_commonModule->OnCreateMetaObject(metaData)) {
		return false;
	}

	return IMetaObject::OnCreateMetaObject(metaData);
}

bool CMetaObject::OnLoadMetaObject(IMetadata *metaData)
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

	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->AddCompileModule(m_commonModule, moduleManager))
		return false;

	return IMetaObject::OnBeforeRunMetaObject(flags);
}

bool CMetaObject::OnAfterCloseMetaObject()
{
	if (!m_commonModule->OnAfterCloseMetaObject())
		return false;

	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->RemoveCompileModule(m_commonModule))
		return false;

	return IMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                           read & save property                      *
//***********************************************************************

void CMetaObject::ReadProperty()
{
	IMetaObject::ReadProperty();

	int version = m_metaData->GetVersion();
	if (IObjectBase::SetPropertyValue("version", version)) {
	}
}

void CMetaObject::SaveProperty()
{
	IMetaObject::SaveProperty();

	int version = 0;
	if (IObjectBase::GetPropertyValue("version", version)) {
		m_metaData->SetVersion(version);
	}
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaObject, "commonMetadata", g_metaCommonMetadataCLSID);
