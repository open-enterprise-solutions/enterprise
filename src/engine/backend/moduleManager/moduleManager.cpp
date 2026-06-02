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
//*                          ibValueModuleManager — lightweight base                                    *
//*********************************************************************************************************

ibValueModuleManager::ibValueModuleManager(ibMetaData* metadata, const ibValueMetaObjectModule* obj) :
	ibValue(ibValueTypes::TYPE_VALUE), ibRuntimeModuleDataObject(new ibCompileModule(obj)),
	m_objectManager(new ibValueGlobalContextManager(metadata)),
	m_metaManager(new ibValueMetadataUnit(metadata)),
	m_methodHelper(new ibValueMethodHelper())
{
	// "Metadata" global — bound straight into the compile module's extern map
	// (the single source for globals; m_metaManager owns the value). m_compileModule
	// is live here (created in the ibRuntimeModuleDataObject base ctor above).
	BindExportVariable(objectMetadataManager, m_metaManager);
}

ibValueModuleManager::~ibValueModuleManager()
{
	wxDELETE(m_methodHelper);
}

void ibValueModuleManager::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	ExportNamesToHelper(m_methodHelper, eProcUnit);

	m_objectManager->PrepareNames();
	m_metaManager->PrepareNames();
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

//*********************************************************************************************************
//*                          ibValueModuleRuntimeManager — heavy runtime part                           *
//*********************************************************************************************************

ibValueModuleRuntimeManager::ibValueModuleRuntimeManager(ibMetaData* metadata, const ibValueMetaObjectModule* obj) :
	ibValueModuleManager(metadata, obj),
	m_initialized(false)
{
}

void ibValueModuleRuntimeManager::Clear()
{
	m_listCommonModuleManager.clear();
}

ibValueModuleRuntimeManager::~ibValueModuleRuntimeManager()
{
	Clear();
}

//*************************************************************************************************************************
//************************************************  support common module *************************************************
//*************************************************************************************************************************

bool ibValueModuleRuntimeManager::RuntimeRegisterCommonModule(ibValueMetaObjectCommonModule* commonModule, bool compileNow)
{
	ibValuePtr<ibValueRuntimeModuleUnit> moduleValue(
		new ibValueRuntimeModuleUnit(this, commonModule, commonModule->IsManagerModule()));

	m_listCommonModuleManager.emplace_back(moduleValue);

	if (!commonModule->IsGlobalModule()) {
		const wxString& strModuleName = commonModule->GetName();
		BindExportVariable(strModuleName, moduleValue);
	}
	else {
		const wxString& strModuleName = commonModule->GetName();
		UnbindVariable(strModuleName);
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

ibValueModuleManager::ibValueModuleUnit* ibValueModuleRuntimeManager::FindCommonModule(const ibValueMetaObjectCommonModule* commonModule) const
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

bool ibValueModuleRuntimeManager::RuntimeRenameCommonModule(ibValueMetaObjectCommonModule* commonModule, const wxString& newName)
{
	ibValue* moduleValue = FindCommonModule(commonModule);
	wxASSERT(moduleValue);

	if (!commonModule->IsGlobalModule()) {
		try {
			BindExportVariable(newName, moduleValue);
			UnbindVariable(commonModule->GetName());
			Compile();
		}
		catch (const ibBackendException& err) {
			wxLogWarning(_("Rename of common module '%s' to '%s' left compile in failed state: %s"),
				commonModule->GetName(), newName, err.GetErrorDescription());
		};
	}

	return true;
}

bool ibValueModuleRuntimeManager::RuntimeUnregisterCommonModule(ibValueMetaObjectCommonModule* commonModule)
{
	ibValuePtr<ibValueModuleManager::ibValueModuleUnit> moduleValue(FindCommonModule(commonModule));
	wxASSERT(moduleValue);

	auto iterator = std::find(m_listCommonModuleManager.begin(), m_listCommonModuleManager.end(), moduleValue);

	if (iterator == m_listCommonModuleManager.end())
		return false;

	if (!commonModule->IsGlobalModule()) {
		UnbindVariable(commonModule->GetName());
	}

	m_listCommonModuleManager.erase(iterator);

	if (commonModule->IsGlobalModule()) {
		m_compileModule->RemoveModule(moduleValue->GetCompileModule());
	}

	return true;
}

void ibValueModuleRuntimeManager::PrepareNames() const
{
	ibValueModuleManager::PrepareNames();

	for (auto& module : m_listCommonModuleManager) {
		module->PrepareNames();
	}
}

//**********************************************************************
//*          Per-session runtime (compile / runtime split)             *
//**********************************************************************

bool ibValueModuleRuntimeManager::AttachRuntime(ibSession* session)
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
			Run();                    // execute main module top-level (delta defaults true → runs the body)
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

	// LOAD-BEARING — do not drop. The compile-time PrepareNames (inside
	// CreateCommonModule) ran BEFORE this AttachRuntime, i.e. with no per-session
	// ProcUnit yet, so each common module's method-helper exported an empty/partial
	// name set. Symptom: a bound common module resolves as a non-aggregate at
	// runtime ("'<method>' - a variable is not an aggregate object"). Re-export here,
	// now that InitializeRuntime/Run above have wired every module's ProcUnit, so the
	// helpers carry the real exported methods/attributes.
	PrepareNames();
	return true;
}

void ibValueModuleRuntimeManager::DetachRuntime(ibSession* session)
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

//////////////////////////////////////////////////////////////////////////////////
//  ibValueModuleManagerRuntimeConfiguration
//////////////////////////////////////////////////////////////////////////////////

ibValueModuleManagerRuntimeConfiguration::ibValueModuleManagerRuntimeConfiguration(
	ibMetaData* metadata,
	ibValueMetaObjectConfiguration* metaObject)
	: ibValueModuleRuntimeManager(metadata, metaObject ? metaObject->GetObjectModule() : nullptr)
{
}

//main module - initialize
bool ibValueModuleManagerRuntimeConfiguration::CreateMainModule()
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
			for (auto* commonModule : storage->GetCompileModules()) {
				if (commonModule == nullptr || commonModule->IsDeleted())
					continue;
				if (!RuntimeRegisterCommonModule(commonModule, /*compileNow=*/false))
					return false;
			}
		}
	}

	// Global constants (Metadata + common modules) are already in the compile
	// module's extern map — bound at registration (ctor / RuntimeRegisterCommonModule)
	// and persistent across Reset. No materialization pass needed here.

	//create singleton "manager" — scope-context: its name isn't an editor
	// identifier, only its props/methods (Catalogs / Documents / …) surface.
	BindScopeVariable(objectManager, m_objectManager);

	for (auto ctor : ibValue::GetListCtorsByType(ibCtorObjectType_object_context)) {
		// EnumManager / SystemManager — transparent scope containers too.
		BindScopeVariable(ctor->GetClassName(), ctor->CreateObject());
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

bool ibValueModuleManagerRuntimeConfiguration::DestroyMainModule()
{
	if (!m_initialized)
		return true;

	// Global constants stay in the extern map across the runtime lifecycle —
	// they're owned by m_metaManager / m_listCommonModuleManager and unbound only
	// at unregister. Here we only tear down the per-bring-up scope bindings.

	//create singleton "manager"
	UnbindVariable(objectManager);

	for (auto ctor : ibValue::GetListCtorsByType(ibCtorObjectType_object_context)) {
		UnbindVariable(ctor->GetClassName());
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
bool ibValueModuleManagerRuntimeConfiguration::StartMainModule(bool force)
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
bool ibValueModuleManagerRuntimeConfiguration::ExitMainModule(bool force)
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

//*********************************************************************************************************
//*                          ibValueModuleManagerDesigner — lightweight designer holder                 *
//*********************************************************************************************************

ibValueModuleManagerDesigner::ibValueModuleManagerDesigner(
	ibMetaData* metaData,
	ibValueMetaObjectConfiguration* metaObject)
	: ibValueModuleManager(metaData, metaObject ? metaObject->GetObjectModule() : nullptr)
{
}

ibValueModuleManagerDesigner::ibValueModuleManagerDesigner(
	ibMetaData* metaData,
	const ibValueMetaObjectModule* objectModule)
	: ibValueModuleManager(metaData, objectModule)
{
	// Object-module ctor == external DP/Report holder (no configuration common
	// object). Mark it so GetGlobalVariables delegates to the configuration root.
	m_external = true;
}

std::map<wxString, ibValue*>& ibValueModuleManagerDesigner::GetGlobalVariables()
{
	// Config-root holder: own globals (compile module's extern map). External
	// DP/Report holder: delegate to the configuration root so its editor sees the
	// root's globals (Metadata + common modules surfaced as names), which aren't
	// seeded into this holder's own extern map.
	if (!m_external)
		return ibValueModuleManager::GetGlobalVariables();

	ibValueModuleManager* root = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	if (root == nullptr || root == this)
		return ibValueModuleManager::GetGlobalVariables();
	return root->GetGlobalVariables();
}

// Compile-side context only — see header. NO Compile(), NO runtime, and NOT the
// common-module registry (the editor surfaces common modules from the metadata
// storage + live text parsing). Gives the editor the "Manager" singleton +
// ctor-context (Catalogs / Documents / Enums) + global consts. Used by BOTH the
// configuration (ctor-from-ibValueMetaObjectConfiguration) and external data
// processors / reports (ctor-from-object-module) — same editor context everywhere.
bool ibValueModuleManagerDesigner::CreateMainModule()
{
	if (m_populated)
		return true;

	// Global constants (Metadata + common modules) already live in the compile
	// module's extern map — bound at registration, persistent across Reset.

	//singleton "manager" — scope-context (transparent container, name not an
	// editor identifier; its props Catalogs / Documents / Enums surface).
	BindScopeVariable(objectManager, m_objectManager);

	//ctor-context objects (EnumManager / SystemManager) — transparent too.
	for (auto ctor : ibValue::GetListCtorsByType(ibCtorObjectType_object_context)) {
		BindScopeVariable(ctor->GetClassName(), ctor->CreateObject());
	}

	// No unit compilation here — see AddCommonModule. The editor parses common
	// modules' text for exports; the designer never executes script.

	// Export the helper name tables now that the context is seeded — the editor's
	// first pass reads GetContextVariables() and calls PrepareNames() per value, but
	// the manager's own helper (and m_objectManager / m_metaManager) need exporting
	// here so names resolve on the very first autocomplete after load.
	PrepareNames();

	m_populated = true;
	return true;
}

bool ibValueModuleManagerDesigner::DestroyMainModule()
{
	if (!m_populated)
		return true;

	// Globals stay in the extern map (unbound only at common-module remove);
	// tear down just the per-bring-up scope bindings here.
	UnbindVariable(objectManager);

	for (auto ctor : ibValue::GetListCtorsByType(ibCtorObjectType_object_context)) {
		UnbindVariable(ctor->GetClassName());
	}

	m_compileModule->Reset();

	m_populated = false;
	return true;
}

// Independent common-module registry — see header. Builds a compiled lightweight
// unit (NO ProcUnit) per common module and indexes it in the compile cache so the
// editor resolves the module to a real compiled value. Mirrors the historical
// IValueModuleManager::AddCommonModule, but designer-only and ProcUnit-free.
bool ibValueModuleManagerDesigner::AddCommonModule(ibValueMetaObjectCommonModule* commonModule, bool managerModule, bool runModule)
{
	ibValuePtr<ibValueModuleUnit> moduleValue(
		new ibValueModuleUnit(commonModule, managerModule));

	// Index by meta-object in the compile cache (editor lookups go here).
	if (auto* cc = m_metaManager->GetMetaData()->GetCompileCache()) {
		if (!cc->AddCompileModule(commonModule, moduleValue))
			return false;
	}

	m_listCommonModule.emplace_back(moduleValue);

	if (!commonModule->IsGlobalModule()) {
		const wxString& strModuleName = commonModule->GetName();
		BindExportVariable(strModuleName, moduleValue);
	}

	// NB: the designer does NOT compile/execute the unit. The editor reads a
	// common module's exports by parsing its live text (ibParserModule in
	// PrepareModuleData). Driving ibCompileModule::Compile() here would enter the
	// Designer-mode parent-recompile walk and deref a stale meta-object (AV in
	// GetFullName). The unit just needs to exist in the cache so FindCompileModule
	// resolves the module being edited. runModule is unused for the designer.
	(void)runModule;

	return true;
}

ibValueModuleManager::ibValueModuleUnit* ibValueModuleManagerDesigner::FindCommonModule(const ibValueMetaObjectCommonModule* commonModule) const
{
	auto it = std::find_if(m_listCommonModule.begin(), m_listCommonModule.end(),
		[commonModule](ibValueModuleUnit* valueModule) {
			return commonModule == valueModule->GetObjectModule();
		}
	);

	if (it != m_listCommonModule.end())
		return *it;

	return nullptr;
}

bool ibValueModuleManagerDesigner::RenameCommonModule(ibValueMetaObjectCommonModule* commonModule, const wxString& newName)
{
	ibValue* moduleValue = FindCommonModule(commonModule);
	wxASSERT(moduleValue);

	if (!commonModule->IsGlobalModule()) {
		BindExportVariable(newName, moduleValue);
		UnbindVariable(commonModule->GetName());
	}

	return true;
}

bool ibValueModuleManagerDesigner::RemoveCommonModule(ibValueMetaObjectCommonModule* commonModule)
{
	ibValuePtr<ibValueModuleUnit> moduleValue(FindCommonModule(commonModule));
	wxASSERT(moduleValue);

	if (auto* cc = m_metaManager->GetMetaData()->GetCompileCache())
		cc->RemoveCompileModule(commonModule);

	auto iterator = std::find(m_listCommonModule.begin(), m_listCommonModule.end(), moduleValue);
	if (iterator == m_listCommonModule.end())
		return false;

	if (!commonModule->IsGlobalModule()) {
		UnbindVariable(commonModule->GetName());
	}

	m_listCommonModule.erase(iterator);

	if (commonModule->IsGlobalModule()) {
		m_compileModule->RemoveModule(moduleValue->GetCompileModule());
	}

	return true;
}

void ibValueModuleManagerDesigner::PrepareNames() const
{
	ibValueModuleManager::PrepareNames();

	for (auto& module : m_listCommonModule) {
		module->PrepareNames();
	}
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueModuleManagerRuntimeConfiguration, "ConfigModuleManager", string_to_clsid("SO_COMM"));

// The lightweight base unit (designer's compiled common-module value) needs its
// OWN factory registration: the designer puts it into the compile module's
// context, and PrepareModuleData calls GetClassType() on it → GetTypeIDByRef
// asserts on an unregistered wxClassInfo. The runtime unit (SO_MODL) derives from
// it but is a distinct type.
SYSTEM_TYPE_REGISTER(ibValueModuleManager::ibValueModuleUnit, "ModuleUnit", string_to_clsid("SO_MODB"));
SYSTEM_TYPE_REGISTER(ibValueModuleRuntimeManager::ibValueRuntimeModuleUnit, "ModuleManager", string_to_clsid("SO_MODL"));
SYSTEM_TYPE_REGISTER(ibValueModuleManager::ibValueMetadataUnit, "Metadata", string_to_clsid("SO_METD"));
