////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metamodule object
////////////////////////////////////////////////////////////////////////////

#include "metaModuleObject.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/metaCollection/partial/object.h"
#include "backend/appData.h"

//***********************************************************************
//*                           ModuleObject                              *
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectModule, IMetaObject)
wxIMPLEMENT_ABSTRACT_CLASS(CMetaObjectCommonModule, CMetaObjectModule)
wxIMPLEMENT_ABSTRACT_CLASS(CMetaObjectManagerModule, CMetaObjectCommonModule)

//***********************************************************************
//*                           Metamodule                                *
//***********************************************************************

CMetaObjectModule::CMetaObjectModule(const wxString& name, const wxString& synonym, const wxString& comment) : 
	IMetaObject(name, synonym, comment)
{
}

bool CMetaObjectModule::LoadData(CMemoryReader& reader)
{
	reader.r_stringZ(m_moduleData);
	return true;
}

bool CMetaObjectModule::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(m_moduleData);
	return true;
}

//***********************************************************************
//*                           System metaData                           *
//***********************************************************************

#include "backend/debugger/debugServer.h"
#include "backend/debugger/debugClient.h"
#include "backend/metaData.h"

bool CMetaObjectModule::OnCreateMetaObject(IMetaData* metaData)
{
	return IMetaObject::OnCreateMetaObject(metaData);
}

bool CMetaObjectModule::OnLoadMetaObject(IMetaData* metaData)
{
	return IMetaObject::OnLoadMetaObject(metaData);
}

bool CMetaObjectModule::OnSaveMetaObject()
{
	//initialize debugger server
	if (appData->DesignerMode()) {
		return debugClient->SaveBreakpoints(GetDocPath());
	}

	return IMetaObject::OnSaveMetaObject();
}

bool CMetaObjectModule::OnDeleteMetaObject()
{
	return IMetaObject::OnDeleteMetaObject();
}

bool CMetaObjectModule::OnBeforeRunMetaObject(int flags)
{
	//initialize debugger server
	unsigned int nNumber = 1 + m_moduleData.Replace('\n', '\n');
	if (appData->DesignerMode()) {
		debugClient->InitializeBreakpoints(GetDocPath(), 0, nNumber);
	}
	else {
		debugServer->InitializeBreakpoints(GetDocPath(), 0, nNumber);
	}

	return IMetaObject::OnBeforeRunMetaObject(flags);
}

bool CMetaObjectModule::OnAfterCloseMetaObject()
{
	return IMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                          default procedures						    *
//***********************************************************************

void CMetaObjectModule::SetDefaultProcedure(const wxString& procname, const eContentHelper& contentHelper, std::vector<wxString> args)
{
	m_contentHelper.insert_or_assign(procname, ContentData{ contentHelper , args });
}

//***********************************************************************
//*                           Metamodule                                *
//***********************************************************************

CMetaObjectCommonModule::CMetaObjectCommonModule(const wxString& name, const wxString& synonym, const wxString& comment) :
	CMetaObjectModule(name, synonym, comment)
{
}

bool CMetaObjectCommonModule::LoadData(CMemoryReader& reader)
{
	reader.r_stringZ(m_moduleData);
	m_properyGlobalModule->SetValue(reader.r_u8());
	return true;
}

bool CMetaObjectCommonModule::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(m_moduleData);
	writer.w_u8(m_properyGlobalModule->GetValueAsBoolean());
	return true;
}

bool CMetaObjectCommonModule::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_properyGlobalModule == property) {
		return CMetaObjectCommonModule::OnAfterCloseMetaObject();
	}

	return CMetaObjectModule::OnPropertyChanging(property, newValue);
}

void CMetaObjectCommonModule::OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (m_properyGlobalModule == property) {
		CMetaObjectCommonModule::OnBeforeRunMetaObject(newObjectFlag);
	}

	CMetaObjectModule::OnPropertyChanged(property, oldValue, newValue);
}

//***********************************************************************
//*                          common value object                        *
//***********************************************************************

bool CMetaObjectCommonModule::OnCreateMetaObject(IMetaData* metaData)
{
	return CMetaObjectModule::OnCreateMetaObject(metaData);
}

bool CMetaObjectCommonModule::OnLoadMetaObject(IMetaData* metaData)
{
	return CMetaObjectModule::OnLoadMetaObject(metaData);
}

bool CMetaObjectCommonModule::OnSaveMetaObject()
{
	return CMetaObjectModule::OnSaveMetaObject();
}

bool CMetaObjectCommonModule::OnDeleteMetaObject()
{
	return CMetaObjectModule::OnDeleteMetaObject();
}

bool CMetaObjectCommonModule::OnRenameMetaObject(const wxString& newName)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->RenameCommonModule(this, newName))
		return false;

	return CMetaObjectModule::OnRenameMetaObject(newName);
}

bool CMetaObjectCommonModule::OnBeforeRunMetaObject(int flags)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->AddCommonModule(this, false, (flags & newObjectFlag) != 0))
		return false;

	return CMetaObjectModule::OnBeforeRunMetaObject(flags);
}

bool CMetaObjectCommonModule::OnAfterCloseMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->RemoveCommonModule(this))
		return false;

	return CMetaObjectModule::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                          manager value object                       *
//***********************************************************************

bool CMetaObjectManagerModule::OnBeforeRunMetaObject(int flags)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->AddCommonModule(this, true, (flags & newObjectFlag) != 0))
		return false;

	return CMetaObjectModule::OnBeforeRunMetaObject(flags);
}

bool CMetaObjectManagerModule::OnAfterCloseMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->RemoveCommonModule(this))
		return false;

	return CMetaObjectModule::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(CMetaObjectCommonModule, "commonModule", g_metaCommonModuleCLSID);
METADATA_TYPE_REGISTER(CMetaObjectManagerModule, "managerModule", g_metaManagerCLSID);
METADATA_TYPE_REGISTER(CMetaObjectModule, "baseModule", g_metaModuleCLSID);
