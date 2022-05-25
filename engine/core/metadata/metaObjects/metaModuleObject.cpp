////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metamodule object
////////////////////////////////////////////////////////////////////////////

#include "metaModuleObject.h"
#include "databaseLayer/databaseLayer.h"
#include "metadata/metaObjects/objects/baseObject.h"
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

CMetaModuleObject::CMetaModuleObject(const wxString &name, const wxString &synonym, const wxString &comment) : IMetaObject(name, synonym, comment)
{
}

bool CMetaModuleObject::LoadData(CMemoryReader &reader)
{
	reader.r_stringZ(m_moduleData);
	return true;
}

bool CMetaModuleObject::SaveData(CMemoryWriter &writer)
{
	writer.w_stringZ(m_moduleData);
	return true;
}

//***********************************************************************
//*                           System metadata                           *
//***********************************************************************

#include "compiler/debugger/debugServer.h"
#include "compiler/debugger/debugClient.h"
#include "metadata/metadata.h"

bool CMetaModuleObject::OnCreateMetaObject(IMetadata *metaData)
{
	return IMetaObject::OnCreateMetaObject(metaData);
}

bool CMetaModuleObject::OnLoadMetaObject(IMetadata *metaData)
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

void CMetaModuleObject::SetDefaultProcedure(const wxString &procname, eContentHelper contentHelper, std::vector<wxString> args)
{
	m_contentHelper.insert_or_assign(procname, ContentData{ contentHelper , args });
}

//***********************************************************************
//*                           Metamodule                                *
//***********************************************************************

CMetaCommonModuleObject::CMetaCommonModuleObject(const wxString &name, const wxString &synonym, const wxString &comment) : CMetaModuleObject(name, synonym, comment),
m_globalModule(false)
{
	PropertyContainer *moduleCategory = IObjectBase::CreatePropertyContainer("Module");
	moduleCategory->AddProperty("global_module", PropertyType::PT_BOOL);
	m_category->AddCategory(moduleCategory);
}

bool CMetaCommonModuleObject::LoadData(CMemoryReader &reader)
{
	reader.r_stringZ(m_moduleData);
	m_globalModule = reader.r_u8();
	return true;
}

bool CMetaCommonModuleObject::SaveData(CMemoryWriter &writer)
{
	writer.w_stringZ(m_moduleData);
	writer.w_u8(m_globalModule);
	return true;
}

bool CMetaCommonModuleObject::OnPropertyChanging(Property *property, const wxVariant& oldValue)
{
	if (property->GetName() == wxT("global_module")) {
		return CMetaCommonModuleObject::OnAfterCloseMetaObject();
	}

	return CMetaModuleObject::OnPropertyChanging(property, oldValue);
}

void CMetaCommonModuleObject::OnPropertyChanged(Property *property)
{
	if (property->GetName() == wxT("global_module")) {
		CMetaCommonModuleObject::OnBeforeRunMetaObject(metaNewObjectFlag);
	}

	CMetaModuleObject::OnPropertyChanged(property);
}

//***********************************************************************
//*                           read & save property                      *
//***********************************************************************

void CMetaCommonModuleObject::ReadProperty()
{
	CMetaModuleObject::ReadProperty();
	IObjectBase::SetPropertyValue("global_module", m_globalModule);
}

void CMetaCommonModuleObject::SaveProperty()
{
	CMetaModuleObject::SaveProperty();
	IObjectBase::GetPropertyValue("global_module", m_globalModule);
}

//***********************************************************************
//*                          common value object                        *
//***********************************************************************

bool CMetaCommonModuleObject::OnCreateMetaObject(IMetadata *metaData)
{
	return CMetaModuleObject::OnCreateMetaObject(metaData);
}

bool CMetaCommonModuleObject::OnLoadMetaObject(IMetadata *metaData)
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

bool CMetaCommonModuleObject::OnRenameMetaObject(const wxString &sNewName)
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->RenameCommonModule(this, sNewName))
		return false;

	return CMetaModuleObject::OnRenameMetaObject(sNewName);
}

bool CMetaCommonModuleObject::OnBeforeRunMetaObject(int flags)
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->AddCommonModule(this, false, (flags & metaNewObjectFlag) != 0))
		return false;

	return CMetaModuleObject::OnBeforeRunMetaObject(flags);
}

bool CMetaCommonModuleObject::OnAfterCloseMetaObject()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
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
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->AddCommonModule(this, true, (flags & metaNewObjectFlag) != 0))
		return false;

	return CMetaModuleObject::OnBeforeRunMetaObject(flags);
}

bool CMetaManagerModuleObject::OnAfterCloseMetaObject()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
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
