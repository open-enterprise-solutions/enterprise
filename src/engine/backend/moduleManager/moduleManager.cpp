////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : module manager for common modules and compile value (in designer mode)
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"
#include "globalContextManager.h"

#include "backend/appData.h"

#define objectManager wxT("Manager")
#define objectMetadataManager wxT("Metadata")

//*********************************************************************************************************
//*                                   Singleton class "moduleManager"                                     *
//*********************************************************************************************************

ibValueModuleManager::ibValueModuleManager(ibMetaData* metadata, ibValueMetaObjectModule* obj) :
	ibValue(ibValueTypes::TYPE_VALUE), ibModuleDataObject(new ibCompileModule(obj)),
	m_objectManager(new ibValueGlobalContextManager(metadata)),
	m_metaManager(new ibValueMetadataUnit(metadata)),
	m_methodHelper(new ibValueMethodHelper()),
	m_initialized(false)
{
	//add global variables 
	m_listGlConstValue.insert_or_assign(objectMetadataManager, m_metaManager);
}

void ibValueModuleManager::Clear()
{
	//clear compile table 
	m_listCommonModuleValue.clear();
	m_listCommonModuleManager.clear();
}

ibValueModuleManager::~ibValueModuleManager()
{
	Clear();
	wxDELETE(m_methodHelper);
}

//*************************************************************************************************************************
//************************************************  support compile module ************************************************
//*************************************************************************************************************************

bool ibValueModuleManager::AddCompileModule(const ibValueMetaObject* mobj, ibValue* object)
{
	if (!appData->DesignerMode() || !object)
		return true;
	auto iterator = m_listCommonModuleValue.find(mobj);
	if (iterator == m_listCommonModuleValue.end()) {
		m_listCommonModuleValue.emplace(mobj, object);
		return true;
	}
	return false;
}

bool ibValueModuleManager::RemoveCompileModule(const ibValueMetaObject* obj)
{
	if (!appData->DesignerMode())
		return true;
	auto iterator = m_listCommonModuleValue.find(obj);
	if (iterator != m_listCommonModuleValue.end()) {
		m_listCommonModuleValue.erase(iterator);
		return true;
	}
	return false;
}

bool ibValueModuleManager::AddCommonModule(ibValueMetaObjectCommonModule* commonModule, bool managerModule, bool runModule)
{
	ibValuePtr<ibValueModuleUnit> moduleValue(new ibValueModuleUnit(this, commonModule, managerModule));

	if (!ibValueModuleManager::AddCompileModule(commonModule, moduleValue))
		return false;

	m_listCommonModuleManager.emplace_back(moduleValue);

	if (!commonModule->IsGlobalModule()) {
		const wxString& strModuleName = commonModule->GetName();
		m_listGlConstValue.insert_or_assign(strModuleName, moduleValue);
		m_compileModule->AddVariable(strModuleName, moduleValue);
	}
	else {
		const wxString& strModuleName = commonModule->GetName();
		m_compileModule->RemoveVariable(strModuleName);
		m_compileModule->AppendModule(moduleValue->GetCompileModule());
	}

	if (runModule) {
		if (!commonModule->IsGlobalModule()) {
			try {
				m_compileModule->Compile();
			}
			catch (const ibBackendException*) {
			};
		}
		return moduleValue->CreateCommonModule();
	}

	return true;
}

ibValueModuleManager::ibValueModuleUnit* ibValueModuleManager::FindCommonModule(ibValueMetaObjectCommonModule* commonModule) const
{
	auto moduleObjectIt = std::find_if(m_listCommonModuleManager.begin(), m_listCommonModuleManager.end(),
		[commonModule](ibValueModuleUnit* valueModule) {
			return commonModule == valueModule->GetModuleObject();
		}
	);

	if (moduleObjectIt != m_listCommonModuleManager.end())
		return *moduleObjectIt;

	return nullptr;
}

bool ibValueModuleManager::RenameCommonModule(ibValueMetaObjectCommonModule* commonModule, const wxString& newName)
{
	ibValue* moduleValue = FindCommonModule(commonModule);
	wxASSERT(moduleValue);

	if (!commonModule->IsGlobalModule()) {
		try {
			m_compileModule->AddVariable(newName, moduleValue);
			m_compileModule->RemoveVariable(commonModule->GetName());
			m_compileModule->Compile();
		}
		catch (const ibBackendException*) {
		};

		m_listGlConstValue.insert_or_assign(newName, moduleValue);
		m_listGlConstValue.erase(commonModule->GetName());
	}

	return true;
}

bool ibValueModuleManager::RemoveCommonModule(ibValueMetaObjectCommonModule* commonModule)
{
	ibValuePtr<ibValueModuleManager::ibValueModuleUnit> moduleValue(FindCommonModule(commonModule));
	wxASSERT(moduleValue);

	if (!ibValueModuleManager::RemoveCompileModule(commonModule))
		return false;

	auto iterator = std::find(m_listCommonModuleManager.begin(), m_listCommonModuleManager.end(), moduleValue);

	if (iterator == m_listCommonModuleManager.end())
		return false;

	if (!commonModule->IsGlobalModule()) {
		m_listGlConstValue.erase(commonModule->GetName());
	}

	m_listCommonModuleManager.erase(iterator);

	if (commonModule->IsGlobalModule()) {
		m_compileModule->RemoveModule(moduleValue->GetCompileModule());
	}

	return true;
}

void ibValueModuleManager::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	if (m_procUnit != nullptr) {
		ibByteCode* byteCode = m_procUnit->GetByteCode();
		if (byteCode != nullptr) {
			for (auto exportFunction : byteCode->m_listExportFunc) {
				m_methodHelper->AppendMethod(
					exportFunction.first,
					byteCode->GetNParams(exportFunction.second),
					byteCode->HasRetVal(exportFunction.second),
					exportFunction.second,
					eProcUnit
				);
			}
			for (auto exportVariable : byteCode->m_listExportVar) {
				m_methodHelper->AppendProp(
					exportVariable.first,
					exportVariable.second,
					eProcUnit
				);
			}
		}
	}

	m_objectManager->PrepareNames();
	m_metaManager->PrepareNames();

	for (auto& module : m_listCommonModuleManager) {
		module->PrepareNames();
	}
}

bool ibValueModuleManager::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	return ibModuleDataObject::ExecuteProc(
		GetMethodName(lMethodNum), paParams, lSizeArray
	);
}

bool ibValueModuleManager::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	return ibModuleDataObject::ExecuteFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}

bool ibValueModuleManager::SetPropVal(const long lPropNum, const ibValue& varPropVal)        //setting attribute
{
	if (m_procUnit != nullptr)
		return m_procUnit->SetPropVal(lPropNum, varPropVal);
	return false;
}

bool ibValueModuleManager::GetPropVal(const long lPropNum, ibValue& pvarPropVal)                   //attribute value
{
	if (m_procUnit != nullptr)
		return m_procUnit->GetPropVal(lPropNum, pvarPropVal);
	return false;
}

long ibValueModuleManager::FindProp(const wxString& strName) const
{
	if (m_procUnit != nullptr) {
		return m_procUnit->FindProp(strName);
	}

	return ibValue::FindProp(strName);
}

//////////////////////////////////////////////////////////////////////////////////
//  ibValueModuleManagerConfiguration
//////////////////////////////////////////////////////////////////////////////////

ibValueModuleManagerConfiguration::ibValueModuleManagerConfiguration(ibMetaData* metadata, ibValueMetaObjectConfiguration* metaObject)
	: ibValueModuleManager(metadata, metaObject ? metaObject->GetModuleObject() : nullptr)
{
}

//main module - initialize
bool ibValueModuleManagerConfiguration::CreateMainModule()
{
	if (m_initialized)
		return true;

	//Добавление глобальных констант
	for (auto variable : m_listGlConstValue) {
		m_compileModule->AddVariable(variable.first, variable.second);
	}

	//create singleton "manager"
	m_compileModule->AddContextVariable(objectManager, m_objectManager);

	for (auto ctor : ibValue::GetListCtorsByType(ibCtorObjectType_object_context)) {
		m_compileModule->AddContextVariable(ctor->GetClassName(), ctor->CreateObject());
	}

	//initialize procUnit
	wxDELETE(m_procUnit);

	if (!appData->DesignerMode()) {
		try {
			m_compileModule->Compile();

			m_procUnit = new ibProcUnit;
			m_procUnit->Execute(m_compileModule->m_cByteCode);
		}
		catch (const ibBackendException*) {
			return false;
		};
	}

	//Setup common modules
	for (auto& moduleValue : m_listCommonModuleManager) {
		if (!moduleValue->CreateCommonModule()) {
			return false;
		}
	}

	m_initialized = true;
	return true;
}

bool ibValueModuleManagerConfiguration::DestroyMainModule()
{
	if (!m_initialized)
		return true;

	//Добавление глобальных констант
	for (auto& variable : m_listGlConstValue) {
		m_compileModule->RemoveVariable(variable.first);
	}

	//create singleton "manager"
	m_compileModule->RemoveVariable(objectManager);

	for (auto ctor : ibValue::GetListCtorsByType(ibCtorObjectType_object_context)) {
		m_compileModule->RemoveVariable(ctor->GetClassName());
	}

	//reset global module
	m_compileModule->Reset();
	wxDELETE(m_procUnit);

	//Setup common modules
	for (auto& moduleValue : m_listCommonModuleManager) {
		if (!moduleValue->DestroyCommonModule()) {
			if (!appData->DesignerMode())
				return false;
		}
	}

	m_initialized = false;
	return true;
}

//main module - initialize
bool ibValueModuleManagerConfiguration::StartMainModule(bool force)
{
	if (force)
		return true;

	if (!m_initialized)
		return false;

	bool result = false;
	if (BeforeStart()) {
		OnStart();
		result = true;
	}

	if (ibApplicationData::IsForceExit())
		return false;

	return result;
}

//main module - destroy
bool ibValueModuleManagerConfiguration::ExitMainModule(bool force)
{
	if (force)
		return true;

	if (!m_initialized)
		return false;

	bool result = false;
	if (BeforeExit()) {
		OnExit(); /*m_initialized = false;*/ result = true;
	}

	if (ibApplicationData::IsForceExit())
		return true;

	return result;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueModuleManagerConfiguration, "ConfigModuleManager", string_to_clsid("SO_COMM"));

SYSTEM_TYPE_REGISTER(ibValueModuleManager::ibValueModuleUnit, "ModuleManager", string_to_clsid("SO_MODL"));
SYSTEM_TYPE_REGISTER(ibValueModuleManager::ibValueMetadataUnit, "Metadata", string_to_clsid("SO_METD"));