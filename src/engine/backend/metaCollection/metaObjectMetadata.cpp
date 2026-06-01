////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject common metaData
////////////////////////////////////////////////////////////////////////////

#include "metaObjectMetadata.h"
#include "metaModuleObject.h"
#include "backend/appData.h"
#include "backend/session/session.h"

//*****************************************************************************************
//*                         metaData													  * 
//*****************************************************************************************


//*****************************************************************************************
//*                                  MetadataObject                                       *
//*****************************************************************************************

#include "backend/metaCollection/metaLanguageObject.h"

wxString ibValueMetaObjectConfiguration::GetLangCode() const
{
	const ibValueMetaObjectLanguage* language =
		FindAnyObjectByFilter<ibValueMetaObjectLanguage>(GetLanguage());

	if (language != nullptr)
		return language->GetLangCode();

	return wxT("");
}

ibValueMetaObjectConfiguration::ibValueMetaObjectConfiguration() : ibValueMetaObject(configurationDefaultName)
{
	//set default proc
	(*m_propertyModuleConfiguration)->SetDefaultProcedure(wxT("BeforeStart"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyModuleConfiguration)->SetDefaultProcedure(wxT("OnStart"), ibContentHelper::eProcedureHelper);
	(*m_propertyModuleConfiguration)->SetDefaultProcedure(wxT("BeforeExit"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	(*m_propertyModuleConfiguration)->SetDefaultProcedure(wxT("OnExit"), ibContentHelper::eProcedureHelper);

	//set def metaid
	m_metaId = defaultMetaID;
}

ibValueMetaObjectConfiguration::~ibValueMetaObjectConfiguration()
{
}

bool ibValueMetaObjectConfiguration::LoadData(ibReaderMemory& dataReader)
{
	m_propertyVersion->SetValue(dataReader.r_s32());

	m_propertyDefRole->LoadData(dataReader);
	m_propertyDefLanguage->LoadData(dataReader);
	(*m_propertyModuleConfiguration)->LoadMeta(dataReader);
	m_propertySyntax->LoadData(dataReader);

	return true;
}

bool ibValueMetaObjectConfiguration::SaveData(ibWriterMemory& dataWritter)
{
	dataWritter.w_s32(m_propertyVersion->GetValueAsInteger());

	m_propertyDefRole->SaveData(dataWritter);
	m_propertyDefLanguage->SaveData(dataWritter);
	(*m_propertyModuleConfiguration)->SaveMeta(dataWritter);
	m_propertySyntax->SaveData(dataWritter);

	return true;
}

//***********************************************************************
//*                          common value object                        *
//***********************************************************************

#include "backend/metaData.h"

bool ibValueMetaObjectConfiguration::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!(*m_propertyModuleConfiguration)->OnCreateMetaObject(metaData, flags)) {
		return false;
	}

	return ibValueMetaObject::OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectConfiguration::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyModuleConfiguration)->OnLoadMetaObject(metaData)) {
		return false;
	}

	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectConfiguration::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyModuleConfiguration)->OnSaveMetaObject(flags)) {
		return false;
	}

	if (m_propertyDefLanguage->IsEmptyProperty()) {
		s_restructureInfo.AppendError(_("! Doesn't have default language ") + GetFullName());
		return false;
	}

	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectConfiguration::OnDeleteMetaObject()
{
	if (!(*m_propertyModuleConfiguration)->OnDeleteMetaObject()) {
		return false;
	}

	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectConfiguration::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyModuleConfiguration)->OnBeforeRunMetaObject(flags))
		return false;

	// Designer's compile-cache holds its OWN module manager (the mm the Designer lost
	// when runtime moved into per-session managers). Register THAT under the config
	// module — not the per-session runtime mm — so the editor reads common-module
	// units + named context from a manager that tracks the current designer state,
	// decoupled from session timing. Runtime configs have null cache → skip.
	if (auto* cc = m_metaData->GetCompileCache()) {
		// Register the designer holder under the config module so the editor reads
		// the "Manager" singleton + named context from it. The holder is STARTED
		// (CreateMainModule) by ibMetaDataConfigurationFile::RunDatabase, as the
		// common module did historically — not here.
		if (!cc->AddCompileModule(m_propertyModuleConfiguration->GetMetaObject(), cc->GetModuleManager()))
			return false;
	}

	ibCompileCode::SetCodeStyle(m_propertySyntax->GetValueAsEnum());
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectConfiguration::OnAfterCloseMetaObject()
{
	if (!(*m_propertyModuleConfiguration)->OnAfterCloseMetaObject())
		return false;

	if (auto* cc = m_metaData->GetCompileCache()) {
		if (!cc->RemoveCompileModule(m_propertyModuleConfiguration->GetMetaObject()))
			return false;
		// Holder teardown (DestroyMainModule) is driven by CloseDatabase, symmetric
		// with the RunDatabase start — not here.
	}

	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectConfiguration, "CommonMetadata", g_metaCommonMetadataCLSID);
