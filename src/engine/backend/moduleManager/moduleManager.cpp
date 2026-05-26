////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : module manager for common modules and compile value (in designer mode)
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"
#include "globalContextManager.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "backend/metadataConfiguration.h"

#define objectManager wxT("Manager")
#define objectMetadataManager wxT("Metadata")

//*********************************************************************************************************
//*                                   Singleton class "moduleManager"                                     *
//*********************************************************************************************************

ibValueModuleManager::ibValueModuleManager(ibMetaData* metadata, const ibValueMetaObjectModule* obj) :
	ibValue(ibValueTypes::TYPE_VALUE), ibRuntimeModuleDataObject(new ibCompileModule(obj)),
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
	m_listCommonModuleManager.clear();
}

ibValueModuleManager::~ibValueModuleManager()
{
	Clear();
	wxDELETE(m_methodHelper);
}

//*************************************************************************************************************************
//************************************************  support common module *************************************************
//*************************************************************************************************************************

bool ibValueModuleManager::RuntimeRegisterCommonModule(ibValueMetaObjectCommonModule* commonModule, bool compileNow)
{
	ibValuePtr<ibValueModuleUnit> moduleValue(
		new ibValueModuleUnit(this, commonModule, commonModule->IsManagerModule()));

	if (auto* cc = m_metaManager->GetMetaData()->GetCompileCache()) {
		if (!cc->AddCompileModule(commonModule, moduleValue))
			return false;
	}

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

	if (compileNow) {
		if (!commonModule->IsGlobalModule()) {
			try {
				Compile();
			}
			catch (const ibBackendException& err) {
				wxLogWarning(_("Common module '%s' failed to compile: %s"),
					commonModule->GetName(), err.GetErrorDescription());
			};
		}
		return moduleValue->CreateCommonModule();
	}

	return true;
}

ibValueModuleManager::ibValueModuleUnit* ibValueModuleManager::FindCommonModule(const ibValueMetaObjectCommonModule* commonModule) const
{
	auto moduleObjectIt = std::find_if(m_listCommonModuleManager.begin(), m_listCommonModuleManager.end(),
		[commonModule](ibValueModuleUnit* valueModule) {
			return commonModule == valueModule->GetObjectModule();
		}
	);

	if (moduleObjectIt != m_listCommonModuleManager.end())
		return *moduleObjectIt;

	return nullptr;
}

bool ibValueModuleManager::RuntimeRenameCommonModule(ibValueMetaObjectCommonModule* commonModule, const wxString& newName)
{
	ibValue* moduleValue = FindCommonModule(commonModule);
	wxASSERT(moduleValue);

	if (!commonModule->IsGlobalModule()) {
		try {
			m_compileModule->AddVariable(newName, moduleValue);
			m_compileModule->RemoveVariable(commonModule->GetName());
			Compile();
		}
		catch (const ibBackendException& err) {
			wxLogWarning(_("Rename of common module '%s' to '%s' left compile in failed state: %s"),
				commonModule->GetName(), newName, err.GetErrorDescription());
		};

		m_listGlConstValue.insert_or_assign(newName, moduleValue);
		m_listGlConstValue.erase(commonModule->GetName());
	}

	return true;
}

bool ibValueModuleManager::RuntimeUnregisterCommonModule(ibValueMetaObjectCommonModule* commonModule)
{
	ibValuePtr<ibValueModuleManager::ibValueModuleUnit> moduleValue(FindCommonModule(commonModule));
	wxASSERT(moduleValue);

	if (auto* cc = m_metaManager->GetMetaData()->GetCompileCache()) {
		if (!cc->RemoveCompileModule(commonModule))
			return false;
	}

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
	ExportNamesToHelper(m_methodHelper, eProcUnit);

	m_objectManager->PrepareNames();
	m_metaManager->PrepareNames();

	for (auto& module : m_listCommonModuleManager) {
		module->PrepareNames();
	}
}

bool ibValueModuleManager::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	return ibRuntimeModuleDataObject::ExecAsProc(
		GetMethodName(lMethodNum), paParams, lSizeArray
	);
}

bool ibValueModuleManager::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	return ibRuntimeModuleDataObject::ExecAsFunc(
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

ibValueModuleManagerConfiguration::ibValueModuleManagerConfiguration(
	ibMetaData* metadata,
	ibValueMetaObjectConfiguration* metaObject)
	: ibValueModuleManager(metadata, metaObject ? metaObject->GetObjectModule() : nullptr)
{
}

//main module - initialize
bool ibValueModuleManagerConfiguration::CreateMainModule()
{
	if (m_initialized)
		return true;

	// Read the init-modules list from metadata's storage and create per-
	// runtime ibValueModuleUnit instances for each. Metadata's storage is
	// the registry (designer-side mutation through ibModuleStorage's
	// Add/Rename/Remove); every runtime mm reads it here to spawn its
	// own runtime objects.
	if (auto* metaData = m_metaManager ? m_metaManager->GetMetaData() : nullptr) {
		if (auto* storage = metaData->GetModuleStorage()) {
			for (auto* commonModule : storage->GetInitModules()) {
				if (commonModule == nullptr || commonModule->IsDeleted())
					continue;
				if (!RuntimeRegisterCommonModule(commonModule, /*compileNow=*/false))
					return false;
			}
		}
	}

	//Добавление глобальных констант
	for (auto variable : m_listGlConstValue) {
		m_compileModule->AddVariable(variable.first, variable.second);
	}

	//create singleton "manager"
	m_compileModule->AddContextVariable(objectManager, m_objectManager);

	for (auto ctor : ibValue::GetListCtorsByType(ibCtorObjectType_object_context)) {
		m_compileModule->AddContextVariable(ctor->GetClassName(), ctor->CreateObject());
	}

	// Compile only — runtime (ibProcUnit) is created per session by
	// AttachRuntime(ctx). The manager itself no longer carries
	// a ProcUnit field.
	if (!appData->DesignerMode()) {
		try {
			Compile();
		}
		catch (const ibBackendException& err) {
			wxLogWarning(_("Global module init failed: %s"), err.GetErrorDescription());
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

	// Fire AfterCompile — post-compile hook for AOT-cache writes,
	// diagnostics, etc. Owner session is resolved via the registry's
	// reverse lookup (m_own scan, match by root mm pointer) so the
	// fire site doesn't depend on ibSession::Current()'s thread-binding
	// state at compile time.
	if (auto* reg = ibApplicationData::GetSessionRegistry()) {
		if (auto* session = reg->FindSessionByRoot(this))
			reg->NotifyAfterCompile(session);
	}

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
	// m_procUnit is no longer populated by CreateMainModule (compile-
	// only since the runtime split 2026-04-19). Session ProcUnits are
	// torn down by DetachRuntime, not here.

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

	if (ibSession::IsCurrentForceExit())
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

	if (ibSession::IsCurrentForceExit())
		return true;

	return result;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueModuleManagerConfiguration, "ConfigModuleManager", string_to_clsid("SO_COMM"));

//**********************************************************************
//*          Per-session runtime (compile / runtime split)             *
//**********************************************************************

bool ibValueModuleManager::AttachRuntime(ibSession* session)
{
	if (session == nullptr)
		return false;
	// Serialize against other sessions' Init/Exit — the Execute of
	// top-level module init + per-session parent ProcUnit chain isn't
	// thread-safe against concurrent Execute on the same compileModule.
	// Rapid F5 reliably hit this as an OOB operator[] inside Execute.
	std::lock_guard<std::mutex> lock(m_runtimeMutex);
	// Runtime only for sessions that represent user work:
	//   Enterprise — desktop thick client's single user.
	//   WebClient  — one per browser tab through wes.
	//   Service    — daemon / codeRunner batch runs.
	// Skip WebServer (wes process's technical row), Designer (compile-
	// only) and Launcher (no metadata). userInfo-empty is NOT a valid
	// discriminator: open-access configurations (empty sys_user) have
	// empty userInfo even for legitimate user sessions.
	const ibSessionKind kind = session->GetKind();
	const bool wantsRuntime =
		(kind == ibSessionKind::Enterprise) ||
		(kind == ibSessionKind::WebClient)  ||
		(kind == ibSessionKind::Service);
	if (!wantsRuntime)
		return true;
	// Imperative pipeline — each descriptor owns its m_procUnit.
	// CreateMainModule already compiled m_compileModule; now we just
	// allocate the runtime slot and execute the top-level.
	if (!appData->DesignerMode() && m_compileModule != nullptr) {
		try {
			InitializeRuntime();     // ensure root's ProcUnit exists
			Run();                    // execute main module top-level
		}
		catch (const ibBackendException& err) {
			wxLogWarning(_("AttachRuntime main: %s"), err.GetErrorDescription());
			return false;
		}
	}
	// Common modules — each has its own compile + m_procUnit. Parent
	// is wired in ibValueModuleUnit's ctor (SetParent(moduleManager)),
	// which cascades procUnit->SetParent on creation inside
	// InitializeRuntime() below.
	for (auto& moduleValue : m_listCommonModuleManager) {
		if (!moduleValue)
			continue;
		if (moduleValue->GetCompileModule() == nullptr)
			continue;
		if (moduleValue->IsGlobalModule())
			// Global modules are *inlined* into the main module at
			// translation time — the translator splices their lexemes
			// into the main compile unit and emits debugger hints
			// noting the origin. There's no separate bytecode to run,
			// so no separate ProcUnit either. Main's ProcUnit executes
			// the spliced code as part of its own top-level.
			continue;
		try {
			moduleValue->InitializeRuntime();
			moduleValue->Run(false);
		}
		catch (const ibBackendException& err) {
			wxLogWarning(_("AttachRuntime common: %s"), err.GetErrorDescription());
			return false;
		}
	}
	return true;
}

void ibValueModuleManager::DetachRuntime(ibSession* session)
{
	if (session == nullptr)
		return;
	// Pairs with AttachRuntime's lock — concurrent Init + Exit
	// would race on m_listCommonModuleManager iteration + common-module
	// ProcUnit drop. Hot code path but brief.
	std::lock_guard<std::mutex> lock(m_runtimeMutex);
	// Drop common modules first — procUnit parent chain breaks cleanly
	// when children release before the root (leaf → root order).
	for (auto& moduleValue : m_listCommonModuleManager) {
		if (moduleValue)
			moduleValue->ResetRuntime();
	}
	ResetRuntime();
}

SYSTEM_TYPE_REGISTER(ibValueModuleManager::ibValueModuleUnit, "ModuleManager", string_to_clsid("SO_MODL"));
SYSTEM_TYPE_REGISTER(ibValueModuleManager::ibValueMetadataUnit, "Metadata", string_to_clsid("SO_METD"));