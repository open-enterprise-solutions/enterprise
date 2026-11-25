////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject root metadata
////////////////////////////////////////////////////////////////////////////

#include "metaObjectMetadata.h"
#include "core/serialize/dataBuilder.h"
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

ibString ibValueMetaObjectApplication::GetLangCode() const
{
	const ibValueMetaObjectLanguage* language =
		FindAnyObjectByFilter<ibValueMetaObjectLanguage>(GetLanguage());

	if (language != nullptr)
		return language->GetLangCode();

	return ibT("");
}

ibValueMetaObjectApplication::ibValueMetaObjectApplication() : ibValueMetaObject(applicationDefaultName)
{
	//set default proc
	(*m_propertyModuleApplication)->SetDefaultProcedure(ibT("BeforeStart"), ibContentHelper::eProcedureHelper, { ibT("Cancel") });
	(*m_propertyModuleApplication)->SetDefaultProcedure(ibT("OnStart"), ibContentHelper::eProcedureHelper);
	(*m_propertyModuleApplication)->SetDefaultProcedure(ibT("BeforeExit"), ibContentHelper::eProcedureHelper, { ibT("Cancel") });
	(*m_propertyModuleApplication)->SetDefaultProcedure(ibT("OnExit"), ibContentHelper::eProcedureHelper);

	// ONE procedure, and it is the module's whole purpose: fill the session
	// parameters. It runs for every session — client, web and background job alike —
	// before the first read, which is why row access can be written against what it
	// sets.
	(*m_propertyModuleSession)->SetDefaultProcedure(ibT("SetSessionParameters"), ibContentHelper::eProcedureHelper);

	//set def metaid
	m_metaId = defaultMetaID;
}

ibValueMetaObjectApplication::~ibValueMetaObjectApplication()
{
}

bool ibValueMetaObjectApplication::ReadData(const ibDataNode& node)
{
	m_propertyVersion->SetNodeValue(node.GetProperty(m_propertyVersion->GetName()));

	m_propertyDefRole->SetNodeValue(node.GetProperty(m_propertyDefRole->GetName()));
	m_propertyDefLanguage->SetNodeValue(node.GetProperty(m_propertyDefLanguage->GetName()));
	m_propertyModuleApplication->SetNodeValue(node.GetProperty(m_propertyModuleApplication->GetName()));
	m_propertyModuleSession->SetNodeValue(node.GetProperty(m_propertyModuleSession->GetName()));
	m_propertySyntax->SetNodeValue(node.GetProperty(m_propertySyntax->GetName()));

	m_homePage.ReadNode(node.GetProperty(ibT("HomePage")));

	return true;
}

bool ibValueMetaObjectApplication::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyVersion->GetName(), m_propertyVersion->GetNodeValue());

	node.SetProperty(m_propertyDefRole->GetName(), m_propertyDefRole->GetNodeValue());
	node.SetProperty(m_propertyDefLanguage->GetName(), m_propertyDefLanguage->GetNodeValue());
	node.SetProperty(m_propertyModuleApplication->GetName(), m_propertyModuleApplication->GetNodeValue());
	node.SetProperty(m_propertyModuleSession->GetName(), m_propertyModuleSession->GetNodeValue());
	node.SetProperty(m_propertySyntax->GetName(), m_propertySyntax->GetNodeValue());

	ibDataValue homePageValue;
	if (m_homePage.WriteNode(homePageValue))
		node.SetProperty(ibT("HomePage"), homePageValue);

	return true;
}

//***********************************************************************
//*                          common value object                        *
//***********************************************************************

#include "backend/metaData.h"

bool ibValueMetaObjectApplication::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!(*m_propertyModuleApplication)->OnCreateMetaObject(metaData, flags)) {
		return false;
	}

	if (!(*m_propertyModuleSession)->OnCreateMetaObject(metaData, flags)) {
		return false;
	}

	return ibValueMetaObject::OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectApplication::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyModuleApplication)->OnLoadMetaObject(metaData)) {
		return false;
	}

	if (!(*m_propertyModuleSession)->OnLoadMetaObject(metaData)) {
		return false;
	}

	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectApplication::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyModuleApplication)->OnSaveMetaObject(flags)) {
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

bool ibValueMetaObjectApplication::OnDeleteMetaObject()
{
	if (!(*m_propertyModuleApplication)->OnDeleteMetaObject()) {
		return false;
	}

	if (!(*m_propertyModuleSession)->OnDeleteMetaObject()) {
		return false;
	}

	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectApplication::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyModuleApplication)->OnBeforeRunMetaObject(flags))
		return false;

	// Studio's compile-cache holds its OWN module manager (the mm the Studio lost
	// when runtime moved into per-session managers). Register THAT under the application
	// module — not the per-session runtime mm — so the editor reads shared-module
	// units + named context from a manager that tracks the current studio state,
	// decoupled from session timing. Runtime applications have null cache → skip.
	if (auto* cc = m_metaData->GetCompileCache()) {
		// Register the studio holder under the application module so the editor reads
		// the "Manager" singleton + named context from it. The holder is STARTED
		// (CreateMainModule) by ibMetaDataApplicationFile::RunDatabase, as the
		// shared module did historically — not here.
		if (!cc->AddCompileModule(m_propertyModuleApplication->GetMetaObject(), cc->GetModuleManager()))
			return false;
	}

	// AFTER the main module is in the cache, and the order is the whole point. The
	// session module is an ordinary shared module — its one peculiarity is that it
	// starts EARLIER than the before-start events, and registering it above put it
	// into a manager that had no main module yet, so the editor's syntax check found
	// no global name in it: `SessionParameters`, `ScheduledJobs`, `Catalogs` alike
	// answered "Var is not found" while the very same text compiled and ran.
	if (!(*m_propertyModuleSession)->OnBeforeRunMetaObject(flags))
		return false;

	ibCompileCode::SetCodeStyle(m_propertySyntax->GetValueAsEnum());
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectApplication::OnBeforeCloseMetaObject()
{
	// CLOSE-BEFORE unloads forms and MODULES — the application module goes here,
	// alongside the form's RemoveCompileModule and the shared module's
	// RemoveSharedModule. It used to run in CLOSE-AFTER, which dropped the module
	// before the un-register phase could still reach it.
	if (auto* cc = m_metaData->GetCompileCache()) {
		cc->RemoveCompileModule(m_propertyModuleApplication->GetMetaObject());
		// Holder teardown (DestroyMainModule) is driven by CloseDatabase, symmetric
		// with the RunDatabase start — not here.
	}

	return ibValueMetaObject::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectApplication::OnAfterCloseMetaObject()
{
	// CLOSE-AFTER un-registers the runtime only.
	if (!(*m_propertyModuleApplication)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyModuleSession)->OnAfterCloseMetaObject())
		return false;

	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectApplication, "ApplicationMetadata", g_metaApplicationMetadataCLSID);
