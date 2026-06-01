////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : module manager - common modules
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"

#include "backend/system/systemManager.h"
#include "backend/appData.h"
#include "backend/session/session.h"


// Lightweight (managerless) — what the designer compile cache builds for autocomplete.
ibValueModuleManager::ibValueModuleUnit::ibValueModuleUnit(ibValueMetaObjectModuleBase *moduleObject, bool managerModule) :
	ibValue(ibValueTypes::TYPE_VALUE, true), ibRuntimeModuleDataObject(new ibCompileCommonModule(moduleObject)),
	m_methodHelper(new ibValueMethodHelper()),
	m_moduleObject(moduleObject)
{
}

// Runtime — wires the owning manager into the parent chain (GetSession() walks).
ibValueModuleRuntimeManager::ibValueRuntimeModuleUnit::ibValueRuntimeModuleUnit(ibValueModuleRuntimeManager *moduleManager, ibValueMetaObjectModuleBase *moduleObject, bool managerModule) :
	ibValueModuleUnit(moduleObject, managerModule),
	m_moduleManager(moduleManager)
{
	// Parent chain in the runtime tree — common module sits directly
	// under its owning root module manager. Enables GetSession() walks
	// from nested scripts without relying on ambient ibSessionScope.
	SetParent(moduleManager);
}

ibValueModuleManager::ibValueModuleUnit::~ibValueModuleUnit()
{
	wxDELETE(m_methodHelper);
}

#define objectManager wxT("Manager")

//common module
bool ibValueModuleRuntimeManager::ibValueRuntimeModuleUnit::CreateCommonModule()
{
	wxASSERT(m_moduleManager != nullptr);

	// Parent already wired in ctor (SetParent(moduleManager)). Bind the
	// module-scope "Manager" singleton that scripts reach via Manager.<method>()
	// inside common modules — transparent scope container (name not an editor
	// identifier, only its members surface).
	BindScopeVariable(objectManager, m_moduleManager->GetObjectManager());

	// Compile only — per-session ProcUnit comes from AttachRuntime.
	try {
		Compile();
	}
	catch (const ibBackendException& err) {
		wxLogWarning(_("Common module init failed: %s"), err.GetErrorDescription());
		return false;
	};

	ibValueModuleUnit::PrepareNames();
	return true;
}

bool ibValueModuleRuntimeManager::ibValueRuntimeModuleUnit::DestroyCommonModule()
{
	wxASSERT(m_moduleManager != nullptr);

	UnbindVariable(objectManager);
	m_compileModule->Reset();

	m_procUnit.reset();
	return true;
}

void ibValueModuleManager::ibValueModuleUnit::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	ExportNamesToHelper(m_methodHelper, eProcUnit);
}

bool ibValueModuleManager::ibValueModuleUnit::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	return ibRuntimeModuleDataObject::ExecAsProc(
		GetMethodName(lMethodNum), paParams, lSizeArray
	);
}

bool ibValueModuleManager::ibValueModuleUnit::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	return ibRuntimeModuleDataObject::ExecAsFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}
