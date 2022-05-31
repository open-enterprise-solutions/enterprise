////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : module manager for common modules and compile value (in designer mode)
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"
#include "metadata/metaObjects/objects/systemManager.h"
#include "compiler/enumFactory.h"
#include "compiler/methods.h"
#include "compiler/systemObjects.h"
#include "utils/stringUtils.h"
#include "appData.h"

#define objectManager wxT("manager")
#define objectSysManager wxT("sysManager")
#define objectEnumManager wxT("enumManager")

#define objectMetadataManager wxT("metadata")

#define thisObject wxT("thisObject")

//*********************************************************************************************************
//*                                   Singleton class "moduleManager"                                     *
//*********************************************************************************************************

#include "metadata/metadata.h"

IModuleManager::IModuleManager(IMetadata* metaData, CMetaModuleObject* obj) :
	CValue(eValueTypes::TYPE_MODULE), IModuleInfo(new CGlobalCompileModule(obj)),
	m_methods(new CMethods()),
	m_initialized(false)
{
	//increment reference
	m_objectManager = new CSystemManager(metaData);
	m_objectManager->IncrRef();

	m_objectSysManager = new CSystemObjects();
	m_objectSysManager->IncrRef();

	m_metaManager = new CMetadataValue(metaData);
	m_metaManager->IncrRef();

	//add global variables 
	m_aValueGlVariables.insert_or_assign(objectMetadataManager, m_metaManager);
}

void IModuleManager::Clear()
{
	for (auto compileModule : m_aCompileModules)
	{
		CValue* dataRef = compileModule.second;
		wxASSERT(dataRef);
		dataRef->DecrRef();
	}

	for (auto moduleValue : m_aCommonModules)
	{
		moduleValue->DecrRef();
	}

	//clear compile table 
	m_aCompileModules.clear();
	m_aCommonModules.clear();
}

IModuleManager::~IModuleManager()
{
	Clear();

	wxDELETE(m_methods);

	wxDELETE(m_objectManager);
	wxDELETE(m_objectSysManager);
	wxDELETE(m_metaManager);
}

//*************************************************************************************************************************
//************************************************  support compile module ************************************************
//*************************************************************************************************************************

bool IModuleManager::AddCompileModule(IMetaObject* mobj, CValue* object)
{
	if (!appData->DesignerMode() || !object)
		return true;

	std::map<IMetaObject*, CValue*>::iterator founded = m_aCompileModules.find(mobj);

	if (founded == m_aCompileModules.end()) {
		m_aCompileModules.insert_or_assign(mobj, object);
		object->IncrRef();
		return true;
	}

	return false;
}

bool IModuleManager::RemoveCompileModule(IMetaObject* obj)
{
	if (!appData->DesignerMode())
		return true;

	std::map<IMetaObject*, CValue*>::iterator founded = m_aCompileModules.find(obj);

	if (founded != m_aCompileModules.end()) {
		CValue* dataRef = founded->second;
		m_aCompileModules.erase(founded);
		dataRef->DecrRef();
		return true;
	}

	return false;
}

bool IModuleManager::AddCommonModule(CMetaCommonModuleObject* commonModule, bool managerModule, bool runModule)
{
	CModuleValue* moduleValue = new CModuleValue(this, commonModule, managerModule);
	moduleValue->IncrRef();

	if (!IModuleManager::AddCompileModule(commonModule, moduleValue))
		return false;

	m_aCommonModules.push_back(moduleValue);

	if (!commonModule->IsGlobalModule()) {
		wxString moduleName = commonModule->GetName();
		m_aValueGlVariables.insert_or_assign(moduleName, moduleValue);
		m_compileModule->AddVariable(moduleName, moduleValue);
	}
	else {
		wxString moduleName = commonModule->GetName();
		m_compileModule->RemoveVariable(moduleName);
		m_compileModule->AppendModule(moduleValue->GetCompileModule());
	}

	if (runModule) {
		if (!commonModule->IsGlobalModule()) {
			try {
				m_compileModule->Compile();
			}
			catch (const CTranslateError*) {
			};
		}
		return moduleValue->CreateCommonModule();
	}

	return true;
}

IModuleManager::CModuleValue* IModuleManager::FindCommonModule(CMetaCommonModuleObject* commonModule)
{
	auto moduleObjectIt = std::find_if(m_aCommonModules.begin(), m_aCommonModules.end(),
		[commonModule](CModuleValue* valueModule) {
			return commonModule == valueModule->GetModuleObject();
		}
	);

	if (moduleObjectIt != m_aCommonModules.end())
		return *moduleObjectIt;

	return NULL;
}

bool IModuleManager::RenameCommonModule(CMetaCommonModuleObject* commonModule, const wxString& newName)
{
	CValue* moduleValue = FindCommonModule(commonModule);
	wxASSERT(moduleValue);

	if (!commonModule->IsGlobalModule()) {
		try {
			m_compileModule->AddVariable(newName, moduleValue);
			m_compileModule->RemoveVariable(commonModule->GetName());
			m_compileModule->Compile();
		}
		catch (const CTranslateError*) {
		};

		m_aValueGlVariables.insert_or_assign(newName, moduleValue);
		m_aValueGlVariables.erase(commonModule->GetName());
	}

	return true;
}

bool IModuleManager::RemoveCommonModule(CMetaCommonModuleObject* commonModule)
{
	IModuleManager::CModuleValue* moduleValue = FindCommonModule(commonModule);
	wxASSERT(moduleValue);

	if (!IModuleManager::RemoveCompileModule(commonModule))
		return false;

	auto moduleObjectIt = std::find(m_aCommonModules.begin(), m_aCommonModules.end(), moduleValue);

	if (moduleObjectIt == m_aCommonModules.end())
		return false;

	if (!commonModule->IsGlobalModule()) {
		m_aValueGlVariables.erase(commonModule->GetName());
	}

	m_aCommonModules.erase(moduleObjectIt);

	if (commonModule->IsGlobalModule()) {
		m_compileModule->RemoveModule(moduleValue->GetCompileModule());
	}

	moduleValue->DecrRef();
	return true;
}

//////////////////////////////////////////////////////////////////////////////////
//  CModuleManager
//////////////////////////////////////////////////////////////////////////////////

CModuleManager::CModuleManager(IMetadata* metaData, CMetaObject* metaObject)
	: IModuleManager(metaData, metaObject->GetModuleObject())
{
}

//main module - initialize
bool CModuleManager::CreateMainModule()
{
	if (m_initialized)
		return true;

	//Добавление глобальных констант
	for (auto variable : m_aValueGlVariables) {
		m_compileModule->AddVariable(variable.first, variable.second);
	}

	//create singleton "manager"
	m_compileModule->AddContextVariable(objectManager, m_objectManager);
	//create singleton "sys.manager"
	m_compileModule->AddContextVariable(objectSysManager, m_objectSysManager);
	//create singleton "enum.manager"
	m_compileModule->AddContextVariable(objectEnumManager, enumFactory);

	//initialize procUnit
	wxDELETE(m_procUnit);

	if (appData->EnterpriseMode() ||
		appData->ServiceMode()) {

		try {
			m_compileModule->Compile();
		}
		catch (const CTranslateError*)
		{
			return false;
		};

		m_procUnit = new CProcUnit;
		m_procUnit->Execute(m_compileModule->m_cByteCode, true);
	}

	//Setup common modules
	for (auto moduleValue : m_aCommonModules) {
		if (!moduleValue->CreateCommonModule()) {
			return false;
		}
	}

	m_initialized = true;
	return true;
}

bool CModuleManager::DestroyMainModule()
{
	if (!m_initialized)
		return true;

	//Добавление глобальных констант
	for (auto variable : m_aValueGlVariables) {
		m_compileModule->RemoveVariable(variable.first);
	}

	//create singleton "manager"
	m_compileModule->RemoveVariable(objectManager);
	//create singleton "sys.manager"
	m_compileModule->RemoveVariable(objectSysManager);
	//create singleton "enum.manager"
	m_compileModule->RemoveVariable(objectEnumManager);

	//reset global module
	m_compileModule->Reset();
	wxDELETE(m_procUnit);

	//Setup common modules
	for (auto moduleValue : m_aCommonModules)
	{
		if (!moduleValue->DestroyCommonModule())
		{
			if (appData->EnterpriseMode() ||
				appData->ServiceMode())
				return false;
		}
	}

	m_initialized = false;
	return true;
}

//main module - initialize
bool CModuleManager::StartMainModule()
{
	if (!m_initialized)
		return false;

	if (BeforeStart()) {
		OnStart(); return true;
	}

	return false;
}

//main module - destroy
bool CModuleManager::ExitMainModule(bool force)
{
	if (force)
		return true;

	if (!m_initialized)
		return false;

	if (BeforeExit()) {
		OnExit(); m_initialized = false; return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////////
//  CExternalDataProcessorModuleManager
//////////////////////////////////////////////////////////////////////////////////

CCompileModule* CExternalDataProcessorModuleManager::GetCompileModule() const
{
	IModuleManager* moduleManager = metadata->GetModuleManager();
	wxASSERT(moduleManager);
	return moduleManager->GetCompileModule();
}

CProcUnit* CExternalDataProcessorModuleManager::GetProcUnit() const
{
	IModuleManager* moduleManager = metadata->GetModuleManager();
	wxASSERT(moduleManager);
	return moduleManager->GetProcUnit();
}

std::map<wxString, CValue*>& CExternalDataProcessorModuleManager::GetContextVariables()
{
	IModuleManager* moduleManager = metadata->GetModuleManager();
	wxASSERT(moduleManager);
	return moduleManager->GetContextVariables();
}

CExternalDataProcessorModuleManager::CExternalDataProcessorModuleManager(IMetadata* metaData, CMetaObjectDataProcessor* metaObject)
	: IModuleManager(CConfigMetadata::Get(), metaObject->GetModuleObject())
{
	m_objectValue = new CObjectDataProcessor(metaObject);
	//set complile module 
	m_objectValue->m_compileModule = m_compileModule;
	//set proc unit 
	m_objectValue->m_procUnit = m_procUnit;
}

CExternalDataProcessorModuleManager::~CExternalDataProcessorModuleManager()
{
}

bool CExternalDataProcessorModuleManager::CreateMainModule()
{
	if (m_initialized)
		return true;

	IModuleManager* moduleManager = metadata->GetModuleManager();
	wxASSERT(moduleManager);

	m_compileModule->SetParent(moduleManager->GetCompileModule());
	m_compileModule->AddContextVariable(thisObject, m_objectValue);

	//initialize procUnit
	wxDELETE(m_procUnit);

	//set complile module 
	m_objectValue->m_compileModule = m_compileModule;

	//initialize object 
	m_objectValue->InitializeObject();

	if (appData->EnterpriseMode() ||
		appData->ServiceMode())
	{
		m_procUnit = new CProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());

		try
		{
			m_compileModule->Compile();
		}
		catch (const CTranslateError*)
		{
			return false;
		};

		//set proc unit 
		m_objectValue->m_procUnit = m_procUnit;

		//and run... 
		m_procUnit->Execute(m_compileModule->m_cByteCode, true);
	}

	//Setup common modules
	for (auto moduleValue : m_aCommonModules) {
		if (!moduleValue->CreateCommonModule()) {
			return false;
		}
	}

	//set initialized true
	m_initialized = true;
	return true;
}

bool CExternalDataProcessorModuleManager::DestroyMainModule()
{
	if (!m_initialized)
		return true;

	m_compileModule->Reset();

	//set complile module 
	m_objectValue->m_compileModule = NULL;

	wxDELETE(m_procUnit);

	//set proc unit 
	m_objectValue->m_procUnit = NULL;

	//Setup common modules
	for (auto moduleValue : m_aCommonModules) {
		if (!moduleValue->DestroyCommonModule()) {
			return false;
		}
	}

	m_initialized = false;
	return true;
}

#include "frontend/visualView/controls/form.h"

//main module - initialize
bool CExternalDataProcessorModuleManager::StartMainModule()
{
	if (!m_initialized)
		return false;

	//incrRef - for control delete 
	m_objectValue->IncrRef();

	IMetaObjectRecordData* commonObject = m_objectValue->GetMetaObject();
	wxASSERT(commonObject);
	IMetaFormObject* defFormObject = commonObject->GetDefaultFormByID(
		CMetaObjectDataProcessor::eFormDataProcessor
	);

	if (defFormObject) {

		CValueForm* valueForm = NULL;

		if (!IModuleManager::FindCompileModule(defFormObject, valueForm)) {

			valueForm = defFormObject->GenerateForm(
				NULL, m_objectValue
			);

			try
			{
				if (valueForm->InitializeFormModule()) {
					valueForm->ShowForm();
				}
			}
			catch (...) {

				wxDELETE(valueForm);

				if (appData->EnterpriseMode() ||
					appData->ServiceMode()) {
					//decrRef - for control delete 
					m_objectValue->DecrRef();
					return false;
				}
			}
		}
	}

	//decrRef - for control delete 
	m_objectValue->DecrRef();

	return true;
}

//main module - destroy
bool CExternalDataProcessorModuleManager::ExitMainModule(bool force)
{
	if (force)
		return true;

	if (!m_initialized)
		return false;

	return true;
}

//////////////////////////////////////////////////////////////////////////////////
//  CExternalReportModuleManager
//////////////////////////////////////////////////////////////////////////////////

CCompileModule* CExternalReportModuleManager::GetCompileModule() const
{
	IModuleManager* moduleManager = metadata->GetModuleManager();
	wxASSERT(moduleManager);
	return moduleManager->GetCompileModule();
}

CProcUnit* CExternalReportModuleManager::GetProcUnit() const
{
	IModuleManager* moduleManager = metadata->GetModuleManager();
	wxASSERT(moduleManager);
	return moduleManager->GetProcUnit();
}

std::map<wxString, CValue*>& CExternalReportModuleManager::GetContextVariables()
{
	IModuleManager* moduleManager = metadata->GetModuleManager();
	wxASSERT(moduleManager);
	return moduleManager->GetContextVariables();
}

CExternalReportModuleManager::CExternalReportModuleManager(IMetadata* metaData, CMetaObjectReport* metaObject)
	: IModuleManager(CConfigMetadata::Get(), metaObject->GetModuleObject())
{
	m_objectValue = new CObjectReport(metaObject);
	//set complile module 
	m_objectValue->m_compileModule = m_compileModule;
	//set proc unit 
	m_objectValue->m_procUnit = m_procUnit;
}

CExternalReportModuleManager::~CExternalReportModuleManager()
{
}

bool CExternalReportModuleManager::CreateMainModule()
{
	if (m_initialized)
		return true;

	IModuleManager* moduleManager = metadata->GetModuleManager();
	wxASSERT(moduleManager);

	m_compileModule->SetParent(moduleManager->GetCompileModule());
	m_compileModule->AddContextVariable(thisObject, m_objectValue);

	//initialize procUnit
	wxDELETE(m_procUnit);

	//set complile module 
	m_objectValue->m_compileModule = m_compileModule;

	//initialize object 
	m_objectValue->InitializeObject();

	if (appData->EnterpriseMode() ||
		appData->ServiceMode())
	{
		m_procUnit = new CProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());

		try
		{
			m_compileModule->Compile();
		}
		catch (const CTranslateError*)
		{
			return false;
		};

		//set proc unit 
		m_objectValue->m_procUnit = m_procUnit;

		m_procUnit->Execute(m_compileModule->m_cByteCode, true);
	}

	//Setup common modules
	for (auto moduleValue : m_aCommonModules) {
		if (!moduleValue->CreateCommonModule()) {
			return false;
		}
	}

	//set initialized true
	m_initialized = true;
	return true;
}

bool CExternalReportModuleManager::DestroyMainModule()
{
	if (!m_initialized)
		return true;

	m_compileModule->Reset();

	//set complile module 
	m_objectValue->m_compileModule = NULL;

	wxDELETE(m_procUnit);

	//set proc unit 
	m_objectValue->m_procUnit = NULL;

	//Setup common modules
	for (auto moduleValue : m_aCommonModules) {
		if (!moduleValue->DestroyCommonModule()) {
			if (appData->EnterpriseMode() ||
				appData->ServiceMode())
				return false;
		}
	}

	m_initialized = false;
	return true;
}

#include "frontend/visualView/controls/form.h"

//main module - initialize
bool CExternalReportModuleManager::StartMainModule()
{
	if (!m_initialized)
		return false;

	//incrRef - for control delete 
	m_objectValue->IncrRef();

	IMetaObjectRecordData* commonObject = m_objectValue->GetMetaObject();
	wxASSERT(commonObject);
	IMetaFormObject* defFormObject = commonObject->GetDefaultFormByID
	(
		CMetaObjectReport::eFormReport
	);

	if (defFormObject) {

		CValueForm* valueForm = NULL;

		if (!IModuleManager::FindCompileModule(defFormObject, valueForm)) {

			valueForm = defFormObject->GenerateForm(
				NULL, m_objectValue
			);

			try
			{
				if (valueForm->InitializeFormModule()) {
					valueForm->ShowForm();
				}
			}
			catch (...) {

				wxDELETE(valueForm);

				if (appData->EnterpriseMode() ||
					appData->ServiceMode()) {
					//decrRef - for control delete 
					m_objectValue->DecrRef();
					return false;
				}
			}
		}
	}

	//decrRef - for control delete 
	m_objectValue->DecrRef();

	return true;
}

//main module - destroy
bool CExternalReportModuleManager::ExitMainModule(bool force)
{
	if (force)
		return true;

	if (!m_initialized)
		return false;

	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SO_VALUE_REGISTER(IModuleManager::CModuleValue, "module", CModuleValue, TEXT2CLSID("SO_MODL"));
SO_VALUE_REGISTER(IModuleManager::CMetadataValue, "metadata", CMetadataValue, TEXT2CLSID("SO_METD"));