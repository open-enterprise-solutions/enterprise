////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject common metaData
////////////////////////////////////////////////////////////////////////////

#include "metaObjectMetadata.h"
#include "backend/serialize/dataBuilder.h"
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

	// ONE procedure, and it is the module's whole purpose: fill the session
	// parameters. It runs for every session — client, web and background job alike —
	// before the first read, which is why row access can be written against what it
	// sets.
	(*m_propertyModuleSession)->SetDefaultProcedure(wxT("SetSessionParameters"), ibContentHelper::eProcedureHelper);

	//set def metaid
	m_metaId = defaultMetaID;
}

ibValueMetaObjectConfiguration::~ibValueMetaObjectConfiguration()
{
}

bool ibValueMetaObjectConfiguration::ReadData(const ibDataNode& node)
{
	m_propertyVersion->ReadNodeValue(node.GetProperty(m_propertyVersion->GetName()));

	m_propertyDefRole->ReadNodeValue(node.GetProperty(m_propertyDefRole->GetName()));
	m_propertyDefLanguage->ReadNodeValue(node.GetProperty(m_propertyDefLanguage->GetName()));
	m_propertyModuleConfiguration->ReadNodeValue(node.GetProperty(m_propertyModuleConfiguration->GetName()));
	m_propertyModuleSession->ReadNodeValue(node.GetProperty(m_propertyModuleSession->GetName()));
	m_propertySyntax->ReadNodeValue(node.GetProperty(m_propertySyntax->GetName()));

	m_homePage.ReadNode(node.GetProperty(wxT("HomePage")));

	return true;
}

bool ibValueMetaObjectConfiguration::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyVersion->GetName(), m_propertyVersion->GetNodeValue());

	node.SetProperty(m_propertyDefRole->GetName(), m_propertyDefRole->GetNodeValue());
	node.SetProperty(m_propertyDefLanguage->GetName(), m_propertyDefLanguage->GetNodeValue());
	node.SetProperty(m_propertyModuleConfiguration->GetName(), m_propertyModuleConfiguration->GetNodeValue());
	node.SetProperty(m_propertyModuleSession->GetName(), m_propertyModuleSession->GetNodeValue());
	node.SetProperty(m_propertySyntax->GetName(), m_propertySyntax->GetNodeValue());

	ibDataValue homePageValue;
	if (m_homePage.WriteNode(homePageValue))
		node.SetProperty(wxT("HomePage"), homePageValue);

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

	if (!(*m_propertyModuleSession)->OnCreateMetaObject(metaData, flags)) {
		return false;
	}

	return ibValueMetaObject::OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectConfiguration::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyModuleConfiguration)->OnLoadMetaObject(metaData)) {
		return false;
	}

	if (!(*m_propertyModuleSession)->OnLoadMetaObject(metaData)) {
		return false;
	}

	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectConfiguration::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyModuleConfiguration)->OnSaveMetaObject(flags)) {
		return false;
	}

	if (!(*m_propertyModuleSession)->OnSaveMetaObject(flags)) {
		return false;
	}

	if (m_propertyDefLanguage->IsEmptyProperty()) {
		RestructureError(_("! Doesn't have default language ") + GetFullName());
		return false;
	}

	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectConfiguration::OnDeleteMetaObject()
{
	if (!(*m_propertyModuleConfiguration)->OnDeleteMetaObject()) {
		return false;
	}

	if (!(*m_propertyModuleSession)->OnDeleteMetaObject()) {
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

	// AFTER the main module is in the cache, and the order is the whole point. The
	// session module is an ordinary common module — its one peculiarity is that it
	// starts EARLIER than the before-start events, and registering it above put it
	// into a manager that had no main module yet, so the editor's syntax check found
	// no global name in it: `SessionParameters`, `ScheduledJobs`, `Catalogs` alike
	// answered "Var is not found" while the very same text compiled and ran.
	if (!(*m_propertyModuleSession)->OnBeforeRunMetaObject(flags))
		return false;

	ibCompileCode::SetCodeStyle(m_propertySyntax->GetValueAsEnum());
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectConfiguration::OnBeforeCloseMetaObject()
{
	// CLOSE-BEFORE unloads forms and MODULES — the configuration module goes here,
	// alongside the form's RemoveCompileModule and the common module's
	// RemoveCommonModule. It used to run in CLOSE-AFTER, which dropped the module
	// before the un-register phase could still reach it.
	if (auto* cc = m_metaData->GetCompileCache()) {
		cc->RemoveCompileModule(m_propertyModuleConfiguration->GetMetaObject());
		// Holder teardown (DestroyMainModule) is driven by CloseDatabase, symmetric
		// with the RunDatabase start — not here.
	}

	return ibValueMetaObject::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectConfiguration::OnAfterCloseMetaObject()
{
	// CLOSE-AFTER un-registers the runtime only.
	if (!(*m_propertyModuleConfiguration)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyModuleSession)->OnAfterCloseMetaObject())
		return false;

	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectConfiguration, "CommonMetadata", g_metaCommonMetadataCLSID);
