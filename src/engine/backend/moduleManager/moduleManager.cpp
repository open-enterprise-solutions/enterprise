////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : module manager for common modules and compile value (in designer mode)
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"
#include "backend/metaCollection/partial/contextManager.h"

#include "backend/appData.h"

#define objectManager wxT("manager")
#define objectMetadataManager wxT("metadata")

#define thisObject wxT("thisObject")

//*********************************************************************************************************
//*                                   Singleton class "moduleManager"                                     *
//*********************************************************************************************************

#include "backend/metadataConfiguration.h"

IModuleManager::IModuleManager(IMetaData* metadata, CMetaObjectModule* obj) :
	CValue(eValueTypes::TYPE_VALUE), IModuleInfo(new CCompileGlobalModule(obj)),
	m_methodHelper(new CMethodHelper()),
	m_initialized(false)
{
	//increment reference
	m_objectManager = new CContextSystemManager(metadata);
	m_objectManager->IncrRef();

	m_metaManager = new CMetadataUnit(metadata);
	m_metaManager->IncrRef();

	//add global variables 
	m_listGlConstValue.insert_or_assign(objectMetadataManager, m_metaManager);
}

void IModuleManager::Clear()
{
	for (auto& compileCode : m_listCommonModuleValue) {
		CValue* dataRef = compileCode.second;
		wxASSERT(dataRef);
		dataRef->DecrRef();
	}

	for (auto& moduleValue : m_listCommonModuleManager) {
		moduleValue->DecrRef();
	}

	//clear compile table 
	m_listCommonModuleValue.clear();
	m_listCommonModuleManager.clear();
}

IModuleManager::~IModuleManager()
{
	Clear();

	wxDELETE(m_objectManager);
	wxDELETE(m_metaManager);

	wxDELETE(m_methodHelper);
}

//*************************************************************************************************************************
//************************************************  support compile module ************************************************
//*************************************************************************************************************************

bool IModuleManager::AddCompileModule(IMetaObject* mobj, CValue* object)
{
	if (!appData->DesignerMode() || !object)
		return true;

	std::map<IMetaObject*, CValue*>::iterator founded = m_listCommonModuleValue.find(mobj);

	if (founded == m_listCommonModuleValue.end()) {
		m_listCommonModuleValue.insert_or_assign(mobj, object);
		object->IncrRef();
		return true;
	}

	return false;
}

bool IModuleManager::RemoveCompileModule(IMetaObject* obj)
{
	if (!appData->DesignerMode())
		return true;

	std::map<IMetaObject*, CValue*>::iterator founded = m_listCommonModuleValue.find(obj);

	if (founded != m_listCommonModuleValue.end()) {
		CValue* dataRef = founded->second;
		m_listCommonModuleValue.erase(founded);
		dataRef->DecrRef();
		return true;
	}

	return false;
}

bool IModuleManager::AddCommonModule(CMetaObjectCommonModule* commonModule, bool managerModule, bool runModule)
{
	CModuleUnit* moduleValue = new CModuleUnit(this, commonModule, managerModule);
	moduleValue->IncrRef();

	if (!IModuleManager::AddCompileModule(commonModule, moduleValue))
		return false;

	m_listCommonModuleManager.push_back(moduleValue);

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
			catch (const CBackendException*) {
			};
		}
		return moduleValue->CreateCommonModule();
	}

	return true;
}

IModuleManager::CModuleUnit* IModuleManager::FindCommonModule(CMetaObjectCommonModule* commonModule) const
{
	auto moduleObjectIt = std::find_if(m_listCommonModuleManager.begin(), m_listCommonModuleManager.end(),
		[commonModule](CModuleUnit* valueModule) {
			return commonModule == valueModule->GetModuleObject();
		}
	);

	if (moduleObjectIt != m_listCommonModuleManager.end())
		return *moduleObjectIt;

	return nullptr;
}

bool IModuleManager::RenameCommonModule(CMetaObjectCommonModule* commonModule, const wxString& newName)
{
	CValue* moduleValue = FindCommonModule(commonModule);
	wxASSERT(moduleValue);

	if (!commonModule->IsGlobalModule()) {
		try {
			m_compileModule->AddVariable(newName, moduleValue);
			m_compileModule->RemoveVariable(commonModule->GetName());
			m_compileModule->Compile();
		}
		catch (const CBackendException*) {
		};

		m_listGlConstValue.insert_or_assign(newName, moduleValue);
		m_listGlConstValue.erase(commonModule->GetName());
	}

	return true;
}

bool IModuleManager::RemoveCommonModule(CMetaObjectCommonModule* commonModule)
{
	IModuleManager::CModuleUnit* moduleValue = FindCommonModule(commonModule);
	wxASSERT(moduleValue);

	if (!IModuleManager::RemoveCompileModule(commonModule))
		return false;

	auto moduleObjectIt = std::find(m_listCommonModuleManager.begin(), m_listCommonModuleManager.end(), moduleValue);

	if (moduleObjectIt == m_listCommonModuleManager.end())
		return false;

	if (!commonModule->IsGlobalModule()) {
		m_listGlConstValue.erase(commonModule->GetName());
	}

	m_listCommonModuleManager.erase(moduleObjectIt);

	if (commonModule->IsGlobalModule()) {
		m_compileModule->RemoveModule(moduleValue->GetCompileModule());
	}

	moduleValue->DecrRef();
	return true;
}

//////////////////////////////////////////////////////////////////////////////////
//  CModuleManagerConfiguration
//////////////////////////////////////////////////////////////////////////////////

CModuleManagerConfiguration::CModuleManagerConfiguration(IMetaData* metadata, CMetaObject* metaObject)
	: IModuleManager(metadata, metaObject ? metaObject->GetModuleObject() : nullptr)
{
}

//main module - initialize
bool CModuleManagerConfiguration::CreateMainModule()
{
	if (m_initialized)
		return true;

	//Добавление глобальных констант
	for (auto variable : m_listGlConstValue) {
		m_compileModule->AddVariable(variable.first, variable.second);
	}

	//create singleton "manager"
	m_compileModule->AddContextVariable(objectManager, m_objectManager);

	for (auto ctor : CValue::GetListCtorsByType(eCtorObjectType_object_context)) {
		m_compileModule->AddContextVariable(ctor->GetClassName(), ctor->CreateObject());
	}

	//initialize procUnit
	wxDELETE(m_procUnit);

	if (!appData->DesignerMode()) {
		try {
			m_compileModule->Compile();

			m_procUnit = new CProcUnit;
			m_procUnit->Execute(m_compileModule->m_cByteCode);
		}
		catch (const CBackendException*) {
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

bool CModuleManagerConfiguration::DestroyMainModule()
{
	if (!m_initialized)
		return true;

	//Добавление глобальных констант
	for (auto &variable : m_listGlConstValue) {
		m_compileModule->RemoveVariable(variable.first);
	}

	//create singleton "manager"
	m_compileModule->RemoveVariable(objectManager);

	for (auto ctor : CValue::GetListCtorsByType(eCtorObjectType_object_context)) {
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
bool CModuleManagerConfiguration::StartMainModule(bool force)
{
	if (force)
		return true;

	if (!m_initialized)
		return false;

	if (BeforeStart()) {
		OnStart(); return true;
	}

	return false;
}

//main module - destroy
bool CModuleManagerConfiguration::ExitMainModule(bool force)
{
	if (force)
		return true;

	if (!m_initialized)
		return false;

	if (BeforeExit()) {
		OnExit(); /*m_initialized = false;*/ return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////////
//  CModuleManagerExternalDataProcessor
//////////////////////////////////////////////////////////////////////////////////

CCompileModule* CModuleManagerExternalDataProcessor::GetCompileModule() const
{
	IModuleManager* moduleManager = commonMetaData->GetModuleManager();
	wxASSERT(moduleManager);
	return moduleManager->GetCompileModule();
}

CProcUnit* CModuleManagerExternalDataProcessor::GetProcUnit() const
{
	IModuleManager* moduleManager = commonMetaData->GetModuleManager();
	wxASSERT(moduleManager);
	return moduleManager->GetProcUnit();
}

std::map<wxString, CValue*>& CModuleManagerExternalDataProcessor::GetContextVariables()
{
	IModuleManager* moduleManager = commonMetaData->GetModuleManager();
	wxASSERT(moduleManager);
	return moduleManager->GetContextVariables();
}

CModuleManagerExternalDataProcessor::CModuleManagerExternalDataProcessor(IMetaData* metadata, CMetaObjectDataProcessor* metaObject)
	: IModuleManager(CMetaDataConfiguration::Get(), metaObject ? metaObject->GetModuleObject() : nullptr)
{
	m_objectValue = new CRecordDataObjectDataProcessor(metaObject);
	//set complile module 
	//set proc unit 
	m_objectValue->m_compileModule = m_compileModule;
	m_objectValue->m_procUnit = m_procUnit;

	if (appData->DesignerMode()) {
		//incrRef
		m_objectValue->IncrRef();
	}
}

CModuleManagerExternalDataProcessor::~CModuleManagerExternalDataProcessor()
{
	if (appData->DesignerMode()) {
		//decrRef
		m_objectValue->DecrRef();
	}
}

bool CModuleManagerExternalDataProcessor::CreateMainModule()
{
	if (m_initialized)
		return true;

	IModuleManager* moduleManager = commonMetaData->GetModuleManager();
	wxASSERT(moduleManager);

	m_compileModule->SetParent(moduleManager->GetCompileModule());
	m_compileModule->AddContextVariable(thisObject, m_objectValue);

	//initialize procUnit
	wxDELETE(m_procUnit);

	//set complile module 
	m_objectValue->m_compileModule = m_compileModule;

	//initialize object 
	m_objectValue->InitializeObject();

	if (!appData->DesignerMode()) {

		m_procUnit = new CProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());

		//set proc unit 
		m_objectValue->m_procUnit = m_procUnit;

		try {
			m_compileModule->Compile();
			//and run... 
			m_procUnit->Execute(m_compileModule->m_cByteCode);
		}
		catch (const CBackendException*) {
			return false;
		};
	}

	//Setup common modules
	for (auto& moduleValue : m_listCommonModuleManager) {
		if (!moduleValue->CreateCommonModule()) {
			return false;
		}
	}

	//set initialized true
	m_initialized = true;
	return true;
}

bool CModuleManagerExternalDataProcessor::DestroyMainModule()
{
	if (!m_initialized)
		return true;

	m_compileModule->Reset();
	wxDELETE(m_procUnit);

	//set complile module 
	//set proc unit
	m_objectValue->m_compileModule = nullptr;
	m_objectValue->m_procUnit = nullptr;

	//Setup common modules
	for (auto& moduleValue : m_listCommonModuleManager) {
		if (!moduleValue->DestroyCommonModule()) {
			return false;
		}
	}

	m_initialized = false;
	return true;
}

//main module - initialize
bool CModuleManagerExternalDataProcessor::StartMainModule(bool force)
{
	if (!m_initialized)
		return false;

	//incrRef - for control delete 
	m_objectValue->IncrRef();

	IMetaObjectRecordData* commonObject = m_objectValue->GetMetaObject();
	wxASSERT(commonObject);
	IMetaObjectForm* defFormObject = commonObject->GetDefaultFormByID(
		CMetaObjectDataProcessor::eFormDataProcessor
	);

	if (defFormObject != nullptr) {
		IBackendValueForm* valueForm = nullptr;
		if (!IModuleManager::FindCompileModule(defFormObject, valueForm)) {
			valueForm = defFormObject->GenerateForm(
				nullptr, m_objectValue
			);
			try {
				if (valueForm->InitializeFormModule()) {
					valueForm->ShowForm();
				}
			}
			catch (...) {
				wxDELETE(valueForm);
				if (!appData->DesignerMode()) {
					//decrRef - for control delete 
					m_objectValue->DecrRef();
					return false;
				}
			}
		}
	}
	//else {
	//	IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(nullptr, nullptr, m_objectValue, Guid::newGuid());
	//	valueForm->BuildForm(CMetaObjectDataProcessor::eFormDataProcessor);
	//	try {
	//		valueForm->ShowForm();
	//	}
	//	catch (...) {
	//		wxDELETE(valueForm);
	//		if (appData->EnterpriseMode() ||
	//			appData->ServiceMode()) {
	//			//decrRef - for control delete 
	//			m_objectValue->DecrRef();
	//			return false;
	//		}
	//	}
	//}

	//decrRef - for control delete 
	m_objectValue->DecrRef();

	return true;
}

//main module - destroy
bool CModuleManagerExternalDataProcessor::ExitMainModule(bool force)
{
	if (force)
		return true;

	if (!m_initialized)
		return false;

	return true;
}

//////////////////////////////////////////////////////////////////////////////////
//  CModuleManagerExternalReport
//////////////////////////////////////////////////////////////////////////////////

CCompileModule* CModuleManagerExternalReport::GetCompileModule() const
{
	IModuleManager* moduleManager = commonMetaData->GetModuleManager();
	wxASSERT(moduleManager);
	return moduleManager->GetCompileModule();
}

CProcUnit* CModuleManagerExternalReport::GetProcUnit() const
{
	IModuleManager* moduleManager = commonMetaData->GetModuleManager();
	wxASSERT(moduleManager);
	return moduleManager->GetProcUnit();
}

std::map<wxString, CValue*>& CModuleManagerExternalReport::GetContextVariables()
{
	IModuleManager* moduleManager = commonMetaData->GetModuleManager();
	wxASSERT(moduleManager);
	return moduleManager->GetContextVariables();
}

CModuleManagerExternalReport::CModuleManagerExternalReport(IMetaData* metadata, CMetaObjectReport* metaObject)
	: IModuleManager(CMetaDataConfiguration::Get(), metaObject ? metaObject->GetModuleObject() : nullptr)
{
	m_objectValue = new CRecordDataObjectReport(metaObject);
	//set complile module 
	//set proc unit 
	m_objectValue->m_compileModule = m_compileModule;
	m_objectValue->m_procUnit = m_procUnit;

	if (appData->DesignerMode()) {
		//incrRef
		m_objectValue->IncrRef();
	}
}

CModuleManagerExternalReport::~CModuleManagerExternalReport()
{
	if (appData->DesignerMode()) {
		//decrRef
		m_objectValue->DecrRef();
	}
}

bool CModuleManagerExternalReport::CreateMainModule()
{
	if (m_initialized)
		return true;

	IModuleManager* moduleManager = commonMetaData->GetModuleManager();
	wxASSERT(moduleManager);

	m_compileModule->SetParent(moduleManager->GetCompileModule());
	m_compileModule->AddContextVariable(thisObject, m_objectValue);

	//initialize procUnit
	wxDELETE(m_procUnit);

	//set complile module 
	m_objectValue->m_compileModule = m_compileModule;

	//initialize object 
	m_objectValue->InitializeObject();

	if (!appData->DesignerMode()) {

		m_procUnit = new CProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());
		//set proc unit 
		m_objectValue->m_procUnit = m_procUnit;

		try {
			m_compileModule->Compile();
			m_procUnit->Execute(m_compileModule->m_cByteCode);
		}
		catch (const CBackendException*) {
			return false;
		};
	}

	//Setup common modules
	for (auto& moduleValue : m_listCommonModuleManager) {
		if (!moduleValue->CreateCommonModule()) {
			return false;
		}
	}

	//set initialized true
	m_initialized = true;
	return true;
}

bool CModuleManagerExternalReport::DestroyMainModule()
{
	if (!m_initialized)
		return true;

	m_compileModule->Reset();
	wxDELETE(m_procUnit);

	//set complile module 
	//set proc unit 
	m_objectValue->m_compileModule = nullptr;
	m_objectValue->m_procUnit = nullptr;

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
bool CModuleManagerExternalReport::StartMainModule(bool force)
{
	if (!m_initialized)
		return false;

	//incrRef - for control delete 
	m_objectValue->IncrRef();

	IMetaObjectRecordData* commonObject = m_objectValue->GetMetaObject();
	wxASSERT(commonObject);
	IMetaObjectForm* defFormObject = commonObject->GetDefaultFormByID
	(
		CMetaObjectReport::eFormReport
	);

	if (defFormObject != nullptr) {
		IBackendValueForm* valueForm = nullptr;
		if (!IModuleManager::FindCompileModule(defFormObject, valueForm)) {
			valueForm = defFormObject->GenerateForm(
				nullptr, m_objectValue
			);
			try {
				if (valueForm->InitializeFormModule()) {
					valueForm->ShowForm();
				}
			}
			catch (...) {
				wxDELETE(valueForm);
				if (!appData->DesignerMode()) {
					//decrRef - for control delete 
					m_objectValue->DecrRef();
					return false;
				}
			}
		}
	}
	//else {
	//	IBackendValueForm* valueForm = IBackendValueForm::CreateNewForm(nullptr, nullptr, m_objectValue, Guid::newGuid());
	//	valueForm->BuildForm(CMetaObjectReport::eFormReport);
	//	try {
	//		valueForm->ShowForm();
	//	}
	//	catch (...) {
	//		wxDELETE(valueForm);
	//		if (appData->EnterpriseMode() ||
	//			appData->ServiceMode()) {
	//			//decrRef - for control delete 
	//			m_objectValue->DecrRef();
	//			return false;
	//		}
	//	}
	//}

	//decrRef - for control delete 
	m_objectValue->DecrRef();

	return true;
}

//main module - destroy
bool CModuleManagerExternalReport::ExitMainModule(bool force)
{
	if (force)
		return true;

	if (!m_initialized)
		return false;

	return true;
}

//**********************************************************************

void IModuleManager::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	if (m_procUnit != nullptr) {
		CByteCode* byteCode = m_procUnit->GetByteCode();
		if (byteCode != nullptr) {
			for (auto exportFunction : byteCode->m_aExportFuncList) {
				m_methodHelper->AppendMethod(
					exportFunction.first,
					byteCode->GetNParams(exportFunction.second),
					byteCode->HasRetVal(exportFunction.second),
					exportFunction.second,
					eProcUnit
				);
			}
			for (auto exportVariable : byteCode->m_aExportVarList) {
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

bool IModuleManager::CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray)
{
	return IModuleInfo::ExecuteProc(
		GetMethodName(lMethodNum), paParams, lSizeArray
	);
}

bool IModuleManager::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	return IModuleInfo::ExecuteFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}

bool IModuleManager::SetPropVal(const long lPropNum, const CValue& varPropVal)        //установка атрибута
{
	if (m_procUnit != nullptr)
		return m_procUnit->SetPropVal(lPropNum, varPropVal);
	return false;
}

bool IModuleManager::GetPropVal(const long lPropNum, CValue& pvarPropVal)                   //значение атрибута
{
	if (m_procUnit != nullptr)
		return m_procUnit->GetPropVal(lPropNum, pvarPropVal);
	return false;
}

long IModuleManager::FindProp(const wxString& strName) const
{
	if (m_procUnit != nullptr) {
		return m_procUnit->FindProp(strName);
	}

	return CValue::FindProp(strName);
}

//****************************************************************************
//*                      CModuleManagerExternalDataProcessor                 *
//****************************************************************************

void CModuleManagerExternalDataProcessor::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	if (m_objectValue != nullptr) {
		m_objectValue->PrepareNames();
		CMethodHelper* methodHelper = m_objectValue->GetPMethods();
		wxASSERT(methodHelper);
		for (long idx = 0; idx < methodHelper->GetNMethods(); idx++) {
			m_methodHelper->CopyMethod(methodHelper, idx);
		}
		for (long idx = 0; idx < methodHelper->GetNProps(); idx++) {
			m_methodHelper->CopyProp(methodHelper, idx);
		}
	}

	if (m_procUnit != nullptr) {
		CByteCode* byteCode = m_procUnit->GetByteCode();
		if (byteCode != nullptr) {
			for (auto exportFunction : byteCode->m_aExportFuncList) {
				m_methodHelper->AppendMethod(
					exportFunction.first,
					byteCode->GetNParams(exportFunction.second),
					byteCode->HasRetVal(exportFunction.second),
					exportFunction.second,
					eProcUnit
				);
			}
			for (auto exportVariable : byteCode->m_aExportVarList) {
				m_methodHelper->AppendProp(
					exportVariable.first,
					exportVariable.second,
					eProcUnit
				);
			}
		}
	}
}

bool CModuleManagerExternalDataProcessor::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	if (m_objectValue &&
		m_objectValue->FindMethod(GetMethodName(lMethodNum)) != wxNOT_FOUND) {
		return m_objectValue->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	}

	return IModuleInfo::ExecuteFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}

bool CModuleManagerExternalDataProcessor::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	if (m_objectValue &&
		m_objectValue->FindProp(GetPropName(lPropNum)) != wxNOT_FOUND) {
		return m_objectValue->SetPropVal(lPropNum, varPropVal);
	}

	if (m_procUnit != nullptr) {
		return m_procUnit->SetPropVal(lPropNum, varPropVal);
	}

	return false;
}

bool CModuleManagerExternalDataProcessor::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	if (m_objectValue &&
		m_objectValue->FindProp(GetPropName(lPropNum)) != wxNOT_FOUND) {
		return m_objectValue->GetPropVal(lPropNum, pvarPropVal);
	}

	if (m_procUnit != nullptr) {
		return m_procUnit->GetPropVal(lPropNum, pvarPropVal);
	}

	return false;
}

long CModuleManagerExternalDataProcessor::FindProp(const wxString& strName) const
{
	if (m_objectValue &&
		m_objectValue->FindProp(strName) != wxNOT_FOUND) {
		return m_objectValue->FindProp(strName);
	}

	if (m_procUnit != nullptr) {
		return m_procUnit->FindProp(strName);
	}

	return CValue::FindProp(strName);
}

//****************************************************************************
//*                      CModuleManagerExternalReport		                 *
//****************************************************************************

void CModuleManagerExternalReport::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	if (m_objectValue != nullptr) {
		m_objectValue->PrepareNames();
		CMethodHelper* methodHelper = m_objectValue->GetPMethods();
		wxASSERT(methodHelper);
		for (long idx = 0; idx < methodHelper->GetNMethods(); idx++) {
			m_methodHelper->CopyMethod(methodHelper, idx);
		}
		for (long idx = 0; idx < methodHelper->GetNProps(); idx++) {
			m_methodHelper->CopyProp(methodHelper, idx);
		}
	}
	if (m_procUnit != nullptr) {
		CByteCode* byteCode = m_procUnit->GetByteCode();
		if (byteCode != nullptr) {
			for (auto exportFunction : byteCode->m_aExportFuncList) {
				m_methodHelper->AppendMethod(
					exportFunction.first,
					byteCode->GetNParams(exportFunction.second),
					byteCode->HasRetVal(exportFunction.second),
					exportFunction.second,
					eProcUnit
				);
			}
			for (auto exportVariable : byteCode->m_aExportVarList) {
				m_methodHelper->AppendProp(
					exportVariable.first,
					exportVariable.second,
					eProcUnit
				);
			}
		}
	}
}

bool CModuleManagerExternalReport::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	if (m_objectValue &&
		m_objectValue->FindMethod(GetMethodName(lMethodNum)) != wxNOT_FOUND) {
		return m_objectValue->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	}

	return IModuleInfo::ExecuteFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}

bool CModuleManagerExternalReport::SetPropVal(const long lPropNum, const CValue& varPropVal)        //установка атрибута
{
	if (m_objectValue &&
		m_objectValue->FindProp(GetPropName(lPropNum)) != wxNOT_FOUND) {
		return m_objectValue->SetPropVal(lPropNum, varPropVal);
	}

	if (m_procUnit != nullptr) {
		return m_procUnit->SetPropVal(lPropNum, varPropVal);
	}

	return false;
}

bool CModuleManagerExternalReport::GetPropVal(const long lPropNum, CValue& pvarPropVal)                   //значение атрибута
{
	if (m_objectValue &&
		m_objectValue->FindProp(GetPropName(lPropNum)) != wxNOT_FOUND) {
		return m_objectValue->GetPropVal(lPropNum, pvarPropVal);
	}

	if (m_procUnit != nullptr) {
		return m_procUnit->GetPropVal(lPropNum, pvarPropVal);
	}

	return false;
}

long CModuleManagerExternalReport::FindProp(const wxString& strName) const
{
	if (m_objectValue &&
		m_objectValue->FindProp(strName) != wxNOT_FOUND) {
		return m_objectValue->FindProp(strName);
	}

	if (m_procUnit != nullptr) {
		return m_procUnit->FindProp(strName);
	}

	return CValue::FindProp(strName);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(CModuleManagerConfiguration, "configModuleManager", string_to_clsid("SO_COMM"));
SYSTEM_TYPE_REGISTER(CModuleManagerExternalDataProcessor, "externalDataProcessorModuleManager", string_to_clsid("SO_EDMM"));
SYSTEM_TYPE_REGISTER(CModuleManagerExternalReport, "externalReportModuleManager", string_to_clsid("SO_ERMM"));

SYSTEM_TYPE_REGISTER(IModuleManager::CModuleUnit, "module", string_to_clsid("SO_MODL"));
SYSTEM_TYPE_REGISTER(IModuleManager::CMetadataUnit, "metaData", string_to_clsid("SO_METD"));