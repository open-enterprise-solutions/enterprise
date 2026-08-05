////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : module manager - common modules
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"

#include "backend/system/systemManager.h"
#include "backend/appData.h"
#include "backend/session/session.h"


// A common module sits directly under the manager that owns it, and it is the ctor
// that says so — for the runtime and the designer alike, because the chain does the
// same work in both. Downwards it carries the scope a module compiles against
// (ibCompileModule::Compile reads the parent's root context); upwards it is what
// GetSession() walks from a nested script, with no ambient ibSessionScope needed.
//
// This used to be two ctors, one of them managerless, and the designer got that one:
// its units stood outside the chain, so the editor's syntax check resolved no global
// name in any common module while the runtime compiled the same text without a word.
ibValueModuleManager::ibValueModuleUnit::ibValueModuleUnit(ibValueModuleManager *moduleManager, ibValueMetaObjectModuleBase *moduleObject, bool managerModule) :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true),
	ibRuntimeModuleDataObject(m_members, this, new ibCompileCommonModule(moduleObject)),
	m_moduleObject(moduleObject)
{
	SetParent(moduleManager);
}

ibValueModuleRuntimeManager::ibValueRuntimeModuleUnit::ibValueRuntimeModuleUnit(ibValueModuleRuntimeManager *moduleManager, ibValueMetaObjectModuleBase *moduleObject, bool managerModule) :
	ibValueModuleUnit(moduleManager, moduleObject, managerModule),
	m_moduleManager(moduleManager)
{
}

ibValueModuleManager::ibValueModuleUnit::~ibValueModuleUnit()
{
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

	// Exports changed after Compile — mark the surface stale so the next access
	// rebuilds it (descriptor export tail) from the fresh compile module.
	m_members.Invalidate();
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
