////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metamodule object
////////////////////////////////////////////////////////////////////////////

#include "metaModuleObject.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include "core/metadata/metaObjects/objects/object.h"
#include "appData.h"

//***********************************************************************
//*                           ModuleObject                              *
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaModuleObject, IMetaObject)
wxIMPLEMENT_ABSTRACT_CLASS(CMetaCommonModuleObject, CMetaModuleObject)
wxIMPLEMENT_ABSTRACT_CLASS(CMetaManagerModuleObject, CMetaCommonModuleObject)

//***********************************************************************
//*                           Metamodule                                *
//***********************************************************************

CMetaModuleObject::CMetaModuleObject(const wxString& name, const wxString& synonym, const wxString& comment) : 
	IMetaObject(name, synonym, comment)
{
}

bool CMetaModuleObject::LoadData(CMemoryReader& reader)
{
	reader.r_stringZ(m_moduleData);
	return true;
}

bool CMetaModuleObject::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(m_moduleData);
	return true;
}

//***********************************************************************
//*                           System metadata                           *
//***********************************************************************

#include "core/compiler/debugger/debugServer.h"
#include "core/compiler/debugger/debugClient.h"
#include "core/metadata/metadata.h"

bool CMetaModuleObject::OnCreateMetaObject(IMetadata* metaData)
{
	return IMetaObject::OnCreateMetaObject(metaData);
}

bool CMetaModuleObject::OnLoadMetaObject(IMetadata* metaData)
{
	//initialize debugger server
	unsigned int nNumber = 1 + m_moduleData.Replace('\n', '\n');
	if (appData->DesignerMode()) {
		debugClient->InitializeBreakpoints(GetDocPath(), 0, nNumber);
	}
	else {
		debugServer->InitializeBreakpoints(GetDocPath(), 0, nNumber);
	}

	return IMetaObject::OnLoadMetaObject(metaData);
}

bool CMetaModuleObject::OnSaveMetaObject()
{
	//initialize debugger server
	if (appData->DesignerMode()) {
		return debugClient->SaveBreakpoints(GetDocPath());
	}

	return IMetaObject::OnSaveMetaObject();
}

bool CMetaModuleObject::OnDeleteMetaObject()
{
	return IMetaObject::OnDeleteMetaObject();
}

bool CMetaModuleObject::OnBeforeRunMetaObject(int flags)
{
	return IMetaObject::OnBeforeRunMetaObject(flags);
}

bool CMetaModuleObject::OnAfterCloseMetaObject()
{
	return IMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                          default procedures						    *
//***********************************************************************

void CMetaModuleObject::SetDefaultProcedure(const wxString& procname, const eContentHelper& contentHelper, std::vector<wxString> args)
{
	m_contentHelper.insert_or_assign(procname, ContentData{ contentHelper , args });
}

//***********************************************************************
//*                           Metamodule                                *
//***********************************************************************

CMetaCommonModuleObject::CMetaCommonModuleObject(const wxString& name, const wxString& synonym, const wxString& comment) :
	CMetaModuleObject(name, synonym, comment)
{
}

bool CMetaCommonModuleObject::LoadData(CMemoryReader& reader)
{
	reader.r_stringZ(m_moduleData);
	m_properyGlobalModule->SetValue(reader.r_u8());
	return true;
}

bool CMetaCommonModuleObject::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(m_moduleData);
	writer.w_u8(m_properyGlobalModule->GetValueAsBoolean());
	return true;
}

bool CMetaCommonModuleObject::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_properyGlobalModule == property) {
		return CMetaCommonModuleObject::OnAfterCloseMetaObject();
	}

	return CMetaModuleObject::OnPropertyChanging(property, newValue);
}

void CMetaCommonModuleObject::OnPropertyChanged(Property* property)
{
	if (m_properyGlobalModule == property) {
		CMetaCommonModuleObject::OnBeforeRunMetaObject(metaNewObjectFlag);
	}

	CMetaModuleObject::OnPropertyChanged(property);
}

//***********************************************************************
//*                          common value object                        *
//***********************************************************************

bool CMetaCommonModuleObject::OnCreateMetaObject(IMetadata* metaData)
{
	return CMetaModuleObject::OnCreateMetaObject(metaData);
}

bool CMetaCommonModuleObject::OnLoadMetaObject(IMetadata* metaData)
{
	return CMetaModuleObject::OnLoadMetaObject(metaData);
}

bool CMetaCommonModuleObject::OnSaveMetaObject()
{
	return CMetaModuleObject::OnSaveMetaObject();
}

bool CMetaCommonModuleObject::OnDeleteMetaObject()
{
	return CMetaModuleObject::OnDeleteMetaObject();
}

bool CMetaCommonModuleObject::OnRenameMetaObject(const wxString& newName)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->RenameCommonModule(this, newName))
		return false;

	return CMetaModuleObject::OnRenameMetaObject(newName);
}

bool CMetaCommonModuleObject::OnBeforeRunMetaObject(int flags)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->AddCommonModule(this, false, (flags & metaNewObjectFlag) != 0))
		return false;

	return CMetaModuleObject::OnBeforeRunMetaObject(flags);
}

bool CMetaCommonModuleObject::OnAfterCloseMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->RemoveCommonModule(this))
		return false;

	return CMetaModuleObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                          manager value object                       *
//***********************************************************************

bool CMetaManagerModuleObject::OnBeforeRunMetaObject(int flags)
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->AddCommonModule(this, true, (flags & metaNewObjectFlag) != 0))
		return false;

	return CMetaModuleObject::OnBeforeRunMetaObject(flags);
}

bool CMetaManagerModuleObject::OnAfterCloseMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->RemoveCommonModule(this))
		return false;

	return CMetaModuleObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaCommonModuleObject, "commonModule", g_metaCommonModuleCLSID);
METADATA_REGISTER(CMetaManagerModuleObject, "managerModule", g_metaManagerCLSID);
METADATA_REGISTER(CMetaModuleObject, "baseModule", g_metaModuleCLSID);
