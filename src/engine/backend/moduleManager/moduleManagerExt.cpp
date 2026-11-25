
#include "moduleManagerExt.h"
#include "backend/metadataApplication.h"

#include "backend/appData.h"
#include "backend/session/session.h"

//////////////////////////////////////////////////////////////////////////////////
#define thisObject ibT("ThisObject")
//////////////////////////////////////////////////////////////////////////////////
//  ibValueModuleRuntimeManagerExternalDataTool
//////////////////////////////////////////////////////////////////////////////////

// These delegate to the APPLICATION's module manager (an external data tool inherits
// the active application's context) — studio manager in the Studio, session root
// mm at runtime. Resolved against the active application metadata, not the data tool's own.
const ibByteCode* ibValueModuleRuntimeManagerExternalDataTool::GetByteCode() const
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	ibASSERT(moduleManager);
	return moduleManager ? moduleManager->GetByteCode() : nullptr;
}

std::unique_ptr<ibCompileModule> ibValueModuleRuntimeManagerExternalDataTool::CreateCompiler() const
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	ibASSERT(moduleManager);
	return moduleManager ? moduleManager->CreateCompiler() : nullptr;
}

std::shared_ptr<ibProcUnit> ibValueModuleRuntimeManagerExternalDataTool::GetProcUnit() const
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	ibASSERT(moduleManager);
	return moduleManager ? moduleManager->GetProcUnit() : nullptr;
}

std::map<ibString, ibContextVar>& ibValueModuleRuntimeManagerExternalDataTool::GetContextVariables()
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	ibASSERT(moduleManager);
	// Returns a reference — no null fallback possible from the manager, so guard
	// with a process-static empty map rather than dereferencing null in release
	// (the sibling GetByteCode/GetProcUnit return pointers and null-fold).
	if (moduleManager == nullptr) {
		static std::map<ibString, ibContextVar> s_empty;
		return s_empty;
	}
	return moduleManager->GetContextVariables();
}

std::map<ibString, ibValue*>& ibValueModuleRuntimeManagerExternalDataTool::GetGlobalVariables()
{
	// Same delegation as GetContextVariables: an external data tool's editor
	// must see the application root's globals (Metadata + shared modules
	// surfaced as names), not its own extern map.
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	ibASSERT(moduleManager);
	if (moduleManager == nullptr) {
		static std::map<ibString, ibValue*> s_empty;
		return s_empty;
	}
	return moduleManager->GetGlobalVariables();
}

ibValueModuleRuntimeManagerExternalDataTool::ibValueModuleRuntimeManagerExternalDataTool(std::shared_ptr<ibMetaData> metadata, ibValueMetaObjectDataTool* metaObject)
	: ibValueModuleRuntimeManager(appEnv::ActiveMetaData(), metaObject ? metaObject->GetObjectModule() : nullptr)
{
	// metadata is the container the object works in — LoadFromFile creates this manager and passes
	// itself, and at runtime the object holds it. In studio the document holds it, never the object.
	m_objectValue = new ibValueRecordDataObjectExternalDataTool(
		metaObject, appData->StudioMode() ? nullptr : std::move(metadata));
	// One module, so one set of binds and one bytecode: the object is the script-side wrapper around the same source.
	m_byteCode = std::make_shared<ibByteCode>();
	m_objectValue->m_binds = m_binds;
	m_objectValue->m_byteCode = m_byteCode;
	//set proc unit
	m_objectValue->m_procUnit = m_procUnit;

	if (appData->StudioMode()) {
		//incrRef
		m_objectValue->IncrRef();
	}

	// Surface = the external object value's methods/props (copied below), then module
	// exports as the helper's tail (descriptor autobind in the base ctor).
	m_members.Bind(this, &ibValueModuleRuntimeManagerExternalDataTool::FillMembers);
}

ibValueModuleRuntimeManagerExternalDataTool::~ibValueModuleRuntimeManagerExternalDataTool()
{
	// RAII protection: always tear down the runtime on release. Idempotent — guards
	// on m_initialized, so it's a no-op if DestroyMainModule already ran and a
	// cleanup if a caller (e.g. an ibValuePtr auto-release) skipped it.
	DestroyMainModule();
	if (m_objectValue) {
		m_objectValue->m_procUnit = nullptr;
	}
	if (appData->StudioMode()) {
		//decrRef
		m_objectValue->DecrRef();
	}
}

bool ibValueModuleRuntimeManagerExternalDataTool::CreateMainModule()
{
	if (m_initialized)
		return true;

	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	ibASSERT(moduleManager);

	// ⭐⭐ incrRef - for control delete — TAKEN BEFORE THE MODULE'S FIRST LINE, let go at the end of
	// StartMainModule, where it always was: the form takes the object there, or nobody does and it unloads,
	// container and all. It used to be taken in StartMainModule, but the module body runs HERE, and `ThisObject`
	// in it loads the object into a temporary — zero, one, zero, and the object went with its container and this
	// manager in the middle of the run (issue #154). A load that fails before StartMainModule leaves it held, as
	// it used to leave it at zero — never let go, so the opener's own delete of the container stays the only one.
	m_objectValue->IncrRef();


	// Imperative pipeline — SetParent wires the parent to application
	// root; BindContextVariable wires thisObject;
	// InitializeRuntime / Compile / Run drive the top-level.
	ibRuntimeModuleDataObject::SetParent(moduleManager);
	BindContextVariable(thisObject, m_objectValue);

	// m_objectValue (the external data tool's script-visible object) stands where
	// this manager stands: a compile chain of its forms goes up through it.
	m_objectValue->ibRuntimeModuleDataObject::SetParent(moduleManager);

	m_objectValue->InitializeObject();

	if (!appData->StudioMode()) {
		InitializeRuntime();
		// Share procUnit with m_objectValue — both descriptors reach
		// the same runtime; shared_ptr co-ownership keeps the unit
		// alive until the last of them drops.
		m_objectValue->m_procUnit = m_procUnit;

		try {
			Compile();
			Run();
		}
		catch (const ibCoreException& err) {
			// 🛑 THE HANDLER MUST NOT THROW — and `GetClassName()` can (Max, 2026-08-20: "it is
			// addressed after it was taken off the registry"). A value's class NAME is looked up in
			// the ctor registry by its dynamic clsid, and the external pair is registered on RUN and
			// dropped on CLOSE — so on the very path that reports a failed init the entry may be
			// gone, `GetNameObjectFromID` raises, and the SECOND exception replaces the first:
			// what reached the user was "Object with id '…' is not exist" instead of the real cause,
			// and the create/open was aborted by the report rather than by the fault.
			//
			// The METAOBJECT's name needs no registry — it is plain metadata, and it is the name a
			// person recognises anyway.
			const ibValueMetaObjectRecordData* metaObject =
				m_objectValue != nullptr ? m_objectValue->GetMetaObject() : nullptr;
			ibJournalWarning(ibT("module"),_("External module '%s' init failed: %s"),
				metaObject != nullptr ? metaObject->GetName() : ibString(ibEmptyString),
				err.GetErrorDescription());
			return false;
		};
	}

	//Setup shared modules
	for (auto& moduleValue : m_listSharedModuleManager) {
		if (!moduleValue->CreateSharedModule()) {
			return false;
		}
	}

	//set initialized true
	m_initialized = true;
	return true;
}

bool ibValueModuleRuntimeManagerExternalDataTool::DestroyMainModule()
{
	if (!m_initialized)
		return true;

	ResetByteCode();
	m_procUnit.reset();

	//set proc unit — the object is gone already when nobody took it (StartMainModule)
	if (m_objectValue != nullptr)
		m_objectValue->m_procUnit = nullptr;

	//Setup shared modules — best-effort: destroy every module even if one
	//fails, so teardown never leaks the remainder; report the aggregate.
	bool ok = true;
	for (auto& moduleValue : m_listSharedModuleManager) {
		if (!moduleValue->DestroySharedModule())
			ok = false;
	}

	m_initialized = false;
	return ok;
}

//main module - initialize
bool ibValueModuleRuntimeManagerExternalDataTool::StartMainModule(const ibRuntimeRequest& request)
{
	if (!m_initialized)
		return false;

	// (incrRef - for control delete: taken in CreateMainModule, before the module ran)

	// THE FORM IS OPENED WITH THE RUN'S PARAMETERS — whoever ran it knows them, and the run is what opens it.
	ibFormRequest formRequest;
	formRequest.m_values = request.m_values;

	// ⭐ WHETHER A FORM TOOK THE OBJECT. At runtime nothing else holds it, so when none did the run's reference is
	// the last one and the object goes with it — and the pointer to it is cleared, not left dangling (Max,
	// 2026-11-03): ExternalDataTools.Create then hands back nothing instead of a freed object. In the studio
	// the manager holds it (ctor), so it never goes here.
	bool taken = false;

	const ibValueMetaObjectRecordData* commonObject = m_objectValue->GetMetaObject();
	ibASSERT(commonObject);
	ibValueMetaObjectFormBase* defFormObject = commonObject->GetDefaultFormByID
	(
		ibValueMetaObjectDataTool::eFormDataTool
	);

	if (defFormObject != nullptr) {

		ibBackendValueForm* cached = nullptr;
		// Cache lives on the form's own metadata (external data tool for .otl, main application
		// for embedded data tool) — m_metaManager->GetMetaData() is the application
		// passed to base ctor and would miss for external data tools.
		ibCompileValueCache* cc = defFormObject->GetMetaData()->GetCompileCache();

		if (!cc || !cc->FindCompileModule(defFormObject, cached)) {

			const ibFormPtr<ibBackendValueForm> result = ibValueMetaObjectFormBase::CreateAndBuildForm(formRequest, defFormObject, nullptr, m_objectValue);

			if (result) {
				result->ShowForm();
				taken = true;
			}
			else if (!appData->StudioMode()) {
				//decrRef - for control delete
				m_objectValue->DecrRef();
				m_objectValue = nullptr;
				return false;
			}

		}
	}
	//else {
	//	ibBackendValueForm* valueForm = ibBackendValueForm::CreateNewForm(nullptr, nullptr, m_objectValue, ibGuid::newGuid());
	//	valueForm->BuildForm(ibValueMetaObjectDataTool::eFormDataTool);
	//	try {
	//		valueForm->ShowForm();
	//	}
	//	catch (...) {
	//		wxDELETE(valueForm);
	//		if (appData->WorkspaceMode() ||
	//			appData->ServiceMode()) {
	//			//decrRef - for control delete 
	//			m_objectValue->DecrRef();
	//			return false;
	//		}
	//	}
	//}

	//decrRef - for control delete
	m_objectValue->DecrRef();
	
	if (!taken && !appData->StudioMode())
		m_objectValue = nullptr;

	return true;
}

//main module - destroy
bool ibValueModuleRuntimeManagerExternalDataTool::ExitMainModule(const ibRuntimeRequest& request)
{
	if (request.m_flags & forceCloseFlag)
		return true;

	if (!m_initialized)
		return false;

	return true;
}

//////////////////////////////////////////////////////////////////////////////////
//  ibValueModuleRuntimeManagerExternalReport
//////////////////////////////////////////////////////////////////////////////////

// Delegate to the APPLICATION's module manager — see the DataTool variant.
const ibByteCode* ibValueModuleRuntimeManagerExternalReport::GetByteCode() const
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	ibASSERT(moduleManager);
	return moduleManager ? moduleManager->GetByteCode() : nullptr;
}

std::unique_ptr<ibCompileModule> ibValueModuleRuntimeManagerExternalReport::CreateCompiler() const
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	ibASSERT(moduleManager);
	return moduleManager ? moduleManager->CreateCompiler() : nullptr;
}

std::shared_ptr<ibProcUnit> ibValueModuleRuntimeManagerExternalReport::GetProcUnit() const
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	ibASSERT(moduleManager);
	return moduleManager ? moduleManager->GetProcUnit() : nullptr;
}

std::map<ibString, ibContextVar>& ibValueModuleRuntimeManagerExternalReport::GetContextVariables()
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	ibASSERT(moduleManager);
	// Returns a reference — no null fallback possible from the manager, so guard
	// with a process-static empty map rather than dereferencing null in release
	// (the sibling GetByteCode/GetProcUnit return pointers and null-fold).
	if (moduleManager == nullptr) {
		static std::map<ibString, ibContextVar> s_empty;
		return s_empty;
	}
	return moduleManager->GetContextVariables();
}

std::map<ibString, ibValue*>& ibValueModuleRuntimeManagerExternalReport::GetGlobalVariables()
{
	// Delegate to the application root — see DataTool::GetGlobalVariables.
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	ibASSERT(moduleManager);
	if (moduleManager == nullptr) {
		static std::map<ibString, ibValue*> s_empty;
		return s_empty;
	}
	return moduleManager->GetGlobalVariables();
}

ibValueModuleRuntimeManagerExternalReport::ibValueModuleRuntimeManagerExternalReport(std::shared_ptr<ibMetaData> metadata, ibValueMetaObjectReport* metaObject)
	: ibValueModuleRuntimeManager(appEnv::ActiveMetaData(), metaObject ? metaObject->GetObjectModule() : nullptr)
{
	// See the data-tool manager above: metadata is the container (passed by LoadFromFile),
	// held by the object at runtime, never in studio.
	m_objectValue = new ibValueRecordDataObjectExternalReport(
		metaObject, appData->StudioMode() ? nullptr : std::move(metadata));
	// One module, so one set of binds and one bytecode: the object is the script-side wrapper around the same source.
	m_byteCode = std::make_shared<ibByteCode>();
	m_objectValue->m_binds = m_binds;
	m_objectValue->m_byteCode = m_byteCode;
	//set proc unit
	m_objectValue->m_procUnit = m_procUnit;

	if (appData->StudioMode()) {
		//incrRef
		m_objectValue->IncrRef();
	}

	// Surface = the external object value's methods/props (copied below), then module
	// exports as the helper's tail (descriptor autobind in the base ctor).
	m_members.Bind(this, &ibValueModuleRuntimeManagerExternalReport::FillMembers);
}

ibValueModuleRuntimeManagerExternalReport::~ibValueModuleRuntimeManagerExternalReport()
{
	// RAII protection: always tear down the runtime on release. Idempotent — guards
	// on m_initialized (init sets it, DestroyMainModule clears it), so it's a no-op
	// if already destroyed and a cleanup if a caller skipped it.
	DestroyMainModule();
	if (m_objectValue) {
		m_objectValue->m_procUnit = nullptr;
	}
	if (appData->StudioMode()) {
		//decrRef
		m_objectValue->DecrRef();
	}
}

bool ibValueModuleRuntimeManagerExternalReport::CreateMainModule()
{
	if (m_initialized)
		return true;

	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	ibASSERT(moduleManager);

	// incrRef - for control delete — before the module's first line, as the data tool's twin says.
	m_objectValue->IncrRef();

	// Imperative pipeline — see ExternalDataTool::CreateMainModule
	// for the same shape; parent cascade, context var, shared procUnit
	// with m_objectValue.
	ibRuntimeModuleDataObject::SetParent(moduleManager);
	BindContextVariable(thisObject, m_objectValue);

	m_objectValue->ibRuntimeModuleDataObject::SetParent(moduleManager);
	m_objectValue->InitializeObject();

	if (!appData->StudioMode()) {
		InitializeRuntime();
		m_objectValue->m_procUnit = m_procUnit;

		try {
			Compile();
			Run();
		}
		catch (const ibCoreException& err) {
			// Same rule as the data tool's twin above: the handler asks the METAOBJECT for a
			// name, because a value's class name is a registry lookup and the registry entry may
			// already be gone on this very path — a reporting call that throws replaces the fault
			// with a complaint about itself.
			const ibValueMetaObjectRecordData* metaObject =
				m_objectValue != nullptr ? m_objectValue->GetMetaObject() : nullptr;
			ibJournalWarning(ibT("module"),_("External module '%s' re-init failed: %s"),
				metaObject != nullptr ? metaObject->GetName() : ibString(ibEmptyString),
				err.GetErrorDescription());
			return false;
		};
	}

	//Setup shared modules
	for (auto& moduleValue : m_listSharedModuleManager) {
		if (!moduleValue->CreateSharedModule()) {
			return false;
		}
	}

	//set initialized true
	m_initialized = true;
	return true;
}

bool ibValueModuleRuntimeManagerExternalReport::DestroyMainModule()
{
	if (!m_initialized)
		return true;

	ResetByteCode();
	m_procUnit.reset();

	//set proc unit — the object is gone already when nobody took it (StartMainModule)
	if (m_objectValue != nullptr)
		m_objectValue->m_procUnit = nullptr;

	//Setup shared modules — best-effort: destroy every module even if one
	//fails, so teardown never leaks the remainder; report the aggregate.
	bool ok = true;
	for (auto& moduleValue : m_listSharedModuleManager) {
		if (!moduleValue->DestroySharedModule())
			ok = false;
	}

	m_initialized = false;
	return ok;
}

//main module - initialize
bool ibValueModuleRuntimeManagerExternalReport::StartMainModule(const ibRuntimeRequest& request)
{
	if (!m_initialized)
		return false;

	// (incrRef - for control delete: taken in CreateMainModule, before the module ran)

	// The form is opened with the run's parameters — see the data tool's twin above.
	ibFormRequest formRequest;
	formRequest.m_values = request.m_values;

	// Whether a form took the object — see the twin: when none did, the object goes and its pointer is cleared.
	bool taken = false;

	// THE METAOBJECT IS A REPORT'S, and it says so — so both questions below are asked of it
	// directly: which form is the default one, and which composer is.
	const ibValueMetaObjectReport* commonObject = m_objectValue->GetMetaObject();
	ibASSERT(commonObject);
	ibValueMetaObjectFormBase* defFormObject = commonObject->GetDefaultFormByID
	(
		ibValueMetaObjectReport::eFormReport
	);

	if (defFormObject != nullptr) {
		ibBackendValueForm* cached = nullptr;
		// Cache lives on the form's own metadata — see the symmetric DataTool
		// path above for rationale.
		ibCompileValueCache* cc = defFormObject->GetMetaData()->GetCompileCache();
		if (!cc || !cc->FindCompileModule(defFormObject, cached)) {

			const ibFormPtr<ibBackendValueForm> result = ibValueMetaObjectFormBase::CreateAndBuildForm(formRequest, defFormObject, nullptr, m_objectValue);

			if (result) {
				result->ShowForm();
				taken = true;
			}
			else if (!appData->StudioMode()) {
				//decrRef - for control delete
				m_objectValue->DecrRef();
				m_objectValue = nullptr;
				return false;
			}
		}
	}
	// ⭐ A REPORT WITH A COMPOSER NEEDS NO FORM OF ITS OWN (Max): you add a composer with the mouse
	// and the report opens — the generated form is a gridbox bound to the default composer, and
	// drawing one by hand to hold a single control is time spent on nothing.
	//
	// Gated on the composer, not offered unconditionally: with neither a form NOR a composer there
	// is nothing to show, and an empty window would be a worse answer than none. Without this the
	// module started and quietly died, which is the same outcome with no explanation.
	else if (commonObject->GetDefComposer() != ibNOT_FOUND) {
		// No key in the request — the form is generated, so it takes a fresh key of its own.
		const ibFormPtr<ibBackendValueForm> valueForm =
			ibBackendValueForm::CreateNewForm(formRequest, nullptr, nullptr, m_objectValue);
		valueForm->BuildForm(ibValueMetaObjectReport::eFormReport);
		try {
			valueForm->ShowForm();
			taken = true;
		}
		catch (...) {
			// A form that failed to show goes with its holder — nobody else took it.
			if (appData->WorkspaceMode() || appData->ServiceMode()) {
				//decrRef - for control delete
				m_objectValue->DecrRef();
				m_objectValue = nullptr;
				return false;
			}
		}
	}

	//decrRef - for control delete
	m_objectValue->DecrRef();
	
	if (!taken && !appData->StudioMode())
		m_objectValue = nullptr;

	return true;
}

//main module - destroy
bool ibValueModuleRuntimeManagerExternalReport::ExitMainModule(const ibRuntimeRequest& request)
{
	if (request.m_flags & forceCloseFlag)
		return true;

	if (!m_initialized)
		return false;

	return true;
}

//****************************************************************************
//*                      ibValueModuleRuntimeManagerExternalDataTool                 *
//****************************************************************************

void ibValueModuleRuntimeManagerExternalDataTool::FillMembers(ibMemberTable& helper) const
{
	// Copy the external object value's surface, after the module's names (bound first by
	// ibValueModuleDataObject) — so a row here has another number in the object: it is asked by the name.
	if (m_objectValue != nullptr) {
		ibMemberTable* methodHelper = m_objectValue->GetPMethods();
		ibASSERT(methodHelper);
		for (long idx = 0; idx < methodHelper->GetNMethods(); idx++) {
			helper.CopyMethod(methodHelper, idx);
		}
		for (long idx = 0; idx < methodHelper->GetNProps(); idx++) {
			helper.CopyProp(methodHelper, idx);
		}
	}
}

bool ibValueModuleRuntimeManagerExternalDataTool::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	const long lObjectNum = m_objectValue != nullptr ? m_objectValue->FindMethod(GetMethodName(lMethodNum)) : ibNOT_FOUND;
	if (lObjectNum != ibNOT_FOUND)
		return m_objectValue->CallAsFunc(lObjectNum, pvarRetValue, paParams, lSizeArray);
	return ibValueModuleDataObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}

bool ibValueModuleRuntimeManagerExternalDataTool::GetParamDefValue(const long lMethodNum, const long lParamNum, ibValue& pvarParamDefValue) const
{
	const long lObjectNum = m_objectValue != nullptr ? m_objectValue->FindMethod(GetMethodName(lMethodNum)) : ibNOT_FOUND;
	if (lObjectNum != ibNOT_FOUND)
		return m_objectValue->GetParamDefValue(lObjectNum, lParamNum, pvarParamDefValue);
	return ibValueModuleDataObject::GetParamDefValue(lMethodNum, lParamNum, pvarParamDefValue);
}

bool ibValueModuleRuntimeManagerExternalDataTool::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lObjectNum = m_objectValue != nullptr ? m_objectValue->FindProp(GetPropName(lPropNum)) : ibNOT_FOUND;
	if (lObjectNum != ibNOT_FOUND)
		return m_objectValue->SetPropVal(lObjectNum, varPropVal);
	return ibValueModuleDataObject::SetPropVal(lPropNum, varPropVal);
}

bool ibValueModuleRuntimeManagerExternalDataTool::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lObjectNum = m_objectValue != nullptr ? m_objectValue->FindProp(GetPropName(lPropNum)) : ibNOT_FOUND;
	if (lObjectNum != ibNOT_FOUND)
		return m_objectValue->GetPropVal(lObjectNum, pvarPropVal);
	return ibValueModuleDataObject::GetPropVal(lPropNum, pvarPropVal);
}

//****************************************************************************
//*                      ibValueModuleRuntimeManagerExternalReport		                 *
//****************************************************************************

void ibValueModuleRuntimeManagerExternalReport::FillMembers(ibMemberTable& helper) const
{
	// Copy the external object value's surface, after the module's names (bound first by
	// ibValueModuleDataObject) — so a row here has another number in the object: it is asked by the name.
	if (m_objectValue != nullptr) {
		ibMemberTable* methodHelper = m_objectValue->GetPMethods();
		ibASSERT(methodHelper);
		for (long idx = 0; idx < methodHelper->GetNMethods(); idx++) {
			helper.CopyMethod(methodHelper, idx);
		}
		for (long idx = 0; idx < methodHelper->GetNProps(); idx++) {
			helper.CopyProp(methodHelper, idx);
		}
	}
}

bool ibValueModuleRuntimeManagerExternalReport::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	const long lObjectNum = m_objectValue != nullptr ? m_objectValue->FindMethod(GetMethodName(lMethodNum)) : ibNOT_FOUND;
	if (lObjectNum != ibNOT_FOUND)
		return m_objectValue->CallAsFunc(lObjectNum, pvarRetValue, paParams, lSizeArray);
	return ibValueModuleDataObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}

bool ibValueModuleRuntimeManagerExternalReport::GetParamDefValue(const long lMethodNum, const long lParamNum, ibValue& pvarParamDefValue) const
{
	const long lObjectNum = m_objectValue != nullptr ? m_objectValue->FindMethod(GetMethodName(lMethodNum)) : ibNOT_FOUND;
	if (lObjectNum != ibNOT_FOUND)
		return m_objectValue->GetParamDefValue(lObjectNum, lParamNum, pvarParamDefValue);
	return ibValueModuleDataObject::GetParamDefValue(lMethodNum, lParamNum, pvarParamDefValue);
}

bool ibValueModuleRuntimeManagerExternalReport::SetPropVal(const long lPropNum, const ibValue& varPropVal)        //setting attribute
{
	const long lObjectNum = m_objectValue != nullptr ? m_objectValue->FindProp(GetPropName(lPropNum)) : ibNOT_FOUND;
	if (lObjectNum != ibNOT_FOUND)
		return m_objectValue->SetPropVal(lObjectNum, varPropVal);
	return ibValueModuleDataObject::SetPropVal(lPropNum, varPropVal);
}

bool ibValueModuleRuntimeManagerExternalReport::GetPropVal(const long lPropNum, ibValue& pvarPropVal)                   //attribute value
{
	const long lObjectNum = m_objectValue != nullptr ? m_objectValue->FindProp(GetPropName(lPropNum)) : ibNOT_FOUND;
	if (lObjectNum != ibNOT_FOUND)
		return m_objectValue->GetPropVal(lObjectNum, pvarPropVal);
	return ibValueModuleDataObject::GetPropVal(lPropNum, pvarPropVal);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueModuleRuntimeManagerExternalDataTool, "ExternalDataToolModuleManager", system_to_clsid("SO_EDMM"));
SYSTEM_TYPE_REGISTER(ibValueModuleRuntimeManagerExternalReport, "ExternalReportModuleManager", system_to_clsid("SO_ERMM"));
