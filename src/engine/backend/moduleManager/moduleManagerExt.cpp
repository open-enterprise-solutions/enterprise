
#include "moduleManagerExt.h"
#include "backend/metadataConfiguration.h"

#include "backend/appData.h"
#include "backend/session/session.h"

//////////////////////////////////////////////////////////////////////////////////
#define thisObject ibT("ThisObject")
//////////////////////////////////////////////////////////////////////////////////
//  ibValueModuleRuntimeManagerExternalDataProcessor
//////////////////////////////////////////////////////////////////////////////////

// These delegate to the CONFIGURATION's module manager (an external DP inherits
// the active config's context) — designer manager in the Designer, session root
// mm at runtime. Resolved against the active config metadata, not the DP's own.
ibCompileModule* ibValueModuleRuntimeManagerExternalDataProcessor::GetCompileModule() const
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	wxASSERT(moduleManager);
	return moduleManager ? moduleManager->GetCompileModule() : nullptr;
}

std::shared_ptr<ibProcUnit> ibValueModuleRuntimeManagerExternalDataProcessor::GetProcUnit() const
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	wxASSERT(moduleManager);
	return moduleManager ? moduleManager->GetProcUnit() : nullptr;
}

std::map<ibString, ibContextVar>& ibValueModuleRuntimeManagerExternalDataProcessor::GetContextVariables()
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	wxASSERT(moduleManager);
	// Returns a reference — no null fallback possible from the manager, so guard
	// with a process-static empty map rather than dereferencing null in release
	// (the sibling GetCompileModule/GetProcUnit return pointers and null-fold).
	if (moduleManager == nullptr) {
		static std::map<ibString, ibContextVar> s_empty;
		return s_empty;
	}
	return moduleManager->GetContextVariables();
}

std::map<ibString, ibValue*>& ibValueModuleRuntimeManagerExternalDataProcessor::GetGlobalVariables()
{
	// Same delegation as GetContextVariables: an external data processor's editor
	// must see the configuration root's globals (Metadata + common modules
	// surfaced as names), not its own extern map.
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	wxASSERT(moduleManager);
	if (moduleManager == nullptr) {
		static std::map<ibString, ibValue*> s_empty;
		return s_empty;
	}
	return moduleManager->GetGlobalVariables();
}

ibValueModuleRuntimeManagerExternalDataProcessor::ibValueModuleRuntimeManagerExternalDataProcessor(std::shared_ptr<ibMetaData> metadata, ibValueMetaObjectDataProcessor* metaObject)
	: ibValueModuleRuntimeManager(appEnv::ActiveMetaData(), metaObject ? metaObject->GetObjectModule() : nullptr)
{
	// metadata is the container the object works in — LoadFromFile creates this manager and passes
	// itself, and at runtime the object holds it. In designer the document holds it, never the object.
	ibValueRecordDataObjectExternalDataProcessor* const objectValue = new ibValueRecordDataObjectExternalDataProcessor(
		metaObject, appData->DesignerMode() ? nullptr : std::move(metadata));
	// Not owned: known by a weak pointer, and lent what this manager compiles and runs (ibExternalOwnerHelper).
	m_objectValue = objectValue->GetWeakObject();
	objectValue->BorrowRuntime(m_compileModule, m_procUnit);

	if (appData->DesignerMode()) {
		//incrRef — in designer this manager is the object's holder
		objectValue->IncrRef();
	}

	// Surface = the external object value's methods/props (copied below), then module
	// exports as the helper's tail (descriptor autobind in the base ctor).
	m_members.Bind(this, &ibValueModuleRuntimeManagerExternalDataProcessor::FillMembers);
}

ibValueModuleRuntimeManagerExternalDataProcessor::~ibValueModuleRuntimeManagerExternalDataProcessor()
{
	// RAII protection: always tear down the runtime on release. Idempotent — guards
	// on m_initialized, so it's a no-op if DestroyMainModule already ran and a
	// cleanup if a caller (e.g. an ibValuePtr auto-release) skipped it.
	DestroyMainModule();
	// The object's borrow needs no clearing: it is weak, and empties as this manager lets go.
	if (appData->DesignerMode()) {
		//decrRef — the holder's reference taken in the ctor, so the object is still there
		GetObjectValue()->DecrRef();
	}
}

bool ibValueModuleRuntimeManagerExternalDataProcessor::CreateMainModule()
{
	if (m_initialized)
		return true;

	// The object this module is the module of; empty once it is gone, and then there is nothing to create.
	ibValueRecordDataObjectExternalDataProcessor* const objectValue = GetExternalObject();
	if (objectValue == nullptr)
		return false;

	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	wxASSERT(moduleManager);

	// ⭐⭐ incrRef - for control delete — TAKEN BEFORE THE MODULE'S FIRST LINE, let go at the end of
	// StartMainModule, where it always was: the form takes the object there, or nobody does and it unloads,
	// container and all. It used to be taken in StartMainModule, but the module body runs HERE, and `ThisObject`
	// in it loads the object into a temporary — zero, one, zero, and the object went with its container and this
	// manager in the middle of the run (issue #154). A load that fails before StartMainModule leaves it held, as
	// it used to leave it at zero — never let go, so the opener's own delete of the container stays the only one.
	objectValue->IncrRef();


	// Imperative pipeline — SetParent cascades compile+procUnit parents
	// to configuration root's; BindContextVariable wires thisObject;
	// InitializeRuntime / Compile / Run drive the top-level.
	ibRuntimeModuleDataObject::SetParent(moduleManager);
	BindContextVariable(thisObject, objectValue);

	// The external DP's script-visible object borrows the same compile module — it's the script-side
	// wrapper around the same source. Lent before InitializeObject so the object sees it.
	objectValue->BorrowRuntime(m_compileModule, m_procUnit);

	objectValue->InitializeObject();

	if (!appData->DesignerMode()) {
		InitializeRuntime();
		// Lend the procUnit to the object — both descriptors reach the
		// same runtime; this manager owns it, the object's is weak.
		objectValue->BorrowRuntime(m_compileModule, m_procUnit);

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
			const ibValueMetaObjectRecordData* metaObject = objectValue->GetMetaObject();
			ibJournalWarning(ibT("module"),_("External module '%s' init failed: %s"),
				metaObject != nullptr ? metaObject->GetName() : ibString(ibEmptyString),
				err.GetErrorDescription());
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

bool ibValueModuleRuntimeManagerExternalDataProcessor::DestroyMainModule()
{
	if (!m_initialized)
		return true;

	m_compileModule->Reset();
	// The object's borrowed proc unit empties with this — it was weak.
	m_procUnit.reset();

	//Setup common modules — best-effort: destroy every module even if one
	//fails, so teardown never leaks the remainder; report the aggregate.
	bool ok = true;
	for (auto& moduleValue : m_listCommonModuleManager) {
		if (!moduleValue->DestroyCommonModule())
			ok = false;
	}

	m_initialized = false;
	return ok;
}

//main module - initialize
bool ibValueModuleRuntimeManagerExternalDataProcessor::StartMainModule(bool force)
{
	if (!m_initialized)
		return false;

	ibValueRecordDataObjectExternalDataProcessor* const objectValue = GetExternalObject();
	if (objectValue == nullptr)
		return false;

	// (incrRef - for control delete: taken in CreateMainModule, before the module ran)

	const ibValueMetaObjectRecordData* commonObject = objectValue->GetMetaObject();
	wxASSERT(commonObject);
	ibValueMetaObjectFormBase* defFormObject = commonObject->GetDefaultFormByID
	(
		ibValueMetaObjectDataProcessor::eFormDataProcessor
	);

	if (defFormObject != nullptr) {

		ibBackendValueForm* cached = nullptr;
		// Cache lives on the form's own metadata (external DP for .epf, main config
		// for embedded DP) — m_metaManager->GetMetaData() is the configuration
		// passed to base ctor and would miss for external DPs.
		ibCompileValueCache* cc = defFormObject->GetMetaData()->GetCompileCache();

		if (!cc || !cc->FindCompileModule(defFormObject, cached)) {

			const ibFormPtr<ibBackendValueForm> result = ibValueMetaObjectFormBase::CreateAndBuildForm(ibFormRequest(), defFormObject, nullptr, objectValue);

			if (result) {
				result->ShowForm();
			}
			else if (!appData->DesignerMode()) {
				//decrRef - for control delete
				objectValue->DecrRef();
				return false;
			}

		}
	}
	//else {
	//	ibBackendValueForm* valueForm = ibBackendValueForm::CreateNewForm(nullptr, nullptr, objectValue, ibGuid::newGuid());
	//	valueForm->BuildForm(ibValueMetaObjectDataProcessor::eFormDataProcessor);
	//	try {
	//		valueForm->ShowForm();
	//	}
	//	catch (...) {
	//		wxDELETE(valueForm);
	//		if (appData->EnterpriseMode() ||
	//			appData->ServiceMode()) {
	//			//decrRef - for control delete 
	//			objectValue->DecrRef();
	//			return false;
	//		}
	//	}
	//}

	//decrRef - for control delete 
	objectValue->DecrRef();

	return true;
}

//main module - destroy
bool ibValueModuleRuntimeManagerExternalDataProcessor::ExitMainModule(bool force)
{
	if (force)
		return true;

	if (!m_initialized)
		return false;

	return true;
}

//////////////////////////////////////////////////////////////////////////////////
//  ibValueModuleRuntimeManagerExternalReport
//////////////////////////////////////////////////////////////////////////////////

// Delegate to the CONFIGURATION's module manager — see the DataProcessor variant.
ibCompileModule* ibValueModuleRuntimeManagerExternalReport::GetCompileModule() const
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	wxASSERT(moduleManager);
	return moduleManager ? moduleManager->GetCompileModule() : nullptr;
}

std::shared_ptr<ibProcUnit> ibValueModuleRuntimeManagerExternalReport::GetProcUnit() const
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	wxASSERT(moduleManager);
	return moduleManager ? moduleManager->GetProcUnit() : nullptr;
}

std::map<ibString, ibContextVar>& ibValueModuleRuntimeManagerExternalReport::GetContextVariables()
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	wxASSERT(moduleManager);
	// Returns a reference — no null fallback possible from the manager, so guard
	// with a process-static empty map rather than dereferencing null in release
	// (the sibling GetCompileModule/GetProcUnit return pointers and null-fold).
	if (moduleManager == nullptr) {
		static std::map<ibString, ibContextVar> s_empty;
		return s_empty;
	}
	return moduleManager->GetContextVariables();
}

std::map<ibString, ibValue*>& ibValueModuleRuntimeManagerExternalReport::GetGlobalVariables()
{
	// Delegate to the configuration root — see DataProcessor::GetGlobalVariables.
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	wxASSERT(moduleManager);
	if (moduleManager == nullptr) {
		static std::map<ibString, ibValue*> s_empty;
		return s_empty;
	}
	return moduleManager->GetGlobalVariables();
}

ibValueModuleRuntimeManagerExternalReport::ibValueModuleRuntimeManagerExternalReport(std::shared_ptr<ibMetaData> metadata, ibValueMetaObjectReport* metaObject)
	: ibValueModuleRuntimeManager(appEnv::ActiveMetaData(), metaObject ? metaObject->GetObjectModule() : nullptr)
{
	// See the data-processor manager above: metadata is the container (passed by LoadFromFile),
	// held by the object at runtime, never in designer.
	ibValueRecordDataObjectExternalReport* const objectValue = new ibValueRecordDataObjectExternalReport(
		metaObject, appData->DesignerMode() ? nullptr : std::move(metadata));
	// Not owned: known by a weak pointer, and lent what this manager compiles and runs (ibExternalOwnerHelper).
	m_objectValue = objectValue->GetWeakObject();
	objectValue->BorrowRuntime(m_compileModule, m_procUnit);

	if (appData->DesignerMode()) {
		//incrRef — in designer this manager is the object's holder
		objectValue->IncrRef();
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
	// The object's borrow needs no clearing: it is weak, and empties as this manager lets go.
	if (appData->DesignerMode()) {
		//decrRef — the holder's reference taken in the ctor, so the object is still there
		GetObjectValue()->DecrRef();
	}
}

bool ibValueModuleRuntimeManagerExternalReport::CreateMainModule()
{
	if (m_initialized)
		return true;

	// The object this module is the module of; empty once it is gone, and then there is nothing to create.
	ibValueRecordDataObjectExternalReport* const objectValue = GetExternalObject();
	if (objectValue == nullptr)
		return false;

	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	wxASSERT(moduleManager);

	// incrRef - for control delete — before the module's first line, as the data processor's twin says.
	objectValue->IncrRef();

	// Imperative pipeline — see ExternalDataProcessor::CreateMainModule
	// for the same shape; parent cascade, context var, compile module and
	// procUnit lent to the object.
	ibRuntimeModuleDataObject::SetParent(moduleManager);
	BindContextVariable(thisObject, objectValue);

	objectValue->BorrowRuntime(m_compileModule, m_procUnit);
	objectValue->InitializeObject();

	if (!appData->DesignerMode()) {
		InitializeRuntime();
		objectValue->BorrowRuntime(m_compileModule, m_procUnit);

		try {
			Compile();
			Run();
		}
		catch (const ibCoreException& err) {
			// Same rule as the data processor's twin above: the handler asks the METAOBJECT for a
			// name, because a value's class name is a registry lookup and the registry entry may
			// already be gone on this very path — a reporting call that throws replaces the fault
			// with a complaint about itself.
			const ibValueMetaObjectRecordData* metaObject = objectValue->GetMetaObject();
			ibJournalWarning(ibT("module"),_("External module '%s' re-init failed: %s"),
				metaObject != nullptr ? metaObject->GetName() : ibString(ibEmptyString),
				err.GetErrorDescription());
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

bool ibValueModuleRuntimeManagerExternalReport::DestroyMainModule()
{
	if (!m_initialized)
		return true;

	m_compileModule->Reset();
	// The object's borrowed proc unit empties with this — it was weak.
	m_procUnit.reset();

	//Setup common modules — best-effort: destroy every module even if one
	//fails, so teardown never leaks the remainder; report the aggregate.
	bool ok = true;
	for (auto& moduleValue : m_listCommonModuleManager) {
		if (!moduleValue->DestroyCommonModule())
			ok = false;
	}

	m_initialized = false;
	return ok;
}

//main module - initialize
bool ibValueModuleRuntimeManagerExternalReport::StartMainModule(bool force)
{
	if (!m_initialized)
		return false;

	ibValueRecordDataObjectExternalReport* const objectValue = GetExternalObject();
	if (objectValue == nullptr)
		return false;

	// (incrRef - for control delete: taken in CreateMainModule, before the module ran)

	// THE METAOBJECT IS A REPORT'S, and it says so — so both questions below are asked of it
	// directly: which form is the default one, and which composer is.
	const ibValueMetaObjectReport* commonObject = objectValue->GetMetaObject();
	wxASSERT(commonObject);
	ibValueMetaObjectFormBase* defFormObject = commonObject->GetDefaultFormByID
	(
		ibValueMetaObjectReport::eFormReport
	);

	if (defFormObject != nullptr) {
		ibBackendValueForm* cached = nullptr;
		// Cache lives on the form's own metadata — see the symmetric DataProcessor
		// path above for rationale.
		ibCompileValueCache* cc = defFormObject->GetMetaData()->GetCompileCache();
		if (!cc || !cc->FindCompileModule(defFormObject, cached)) {

			const ibFormPtr<ibBackendValueForm> result = ibValueMetaObjectFormBase::CreateAndBuildForm(ibFormRequest(), defFormObject, nullptr, objectValue);

			if (result) {
				result->ShowForm();
			}
			else if (!appData->DesignerMode()) {
				//decrRef - for control delete
				objectValue->DecrRef();
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
	else if (commonObject->GetDefComposer() != wxNOT_FOUND) {
		// No key in the request — the form is generated, so it takes a fresh key of its own.
		const ibFormPtr<ibBackendValueForm> valueForm =
			ibBackendValueForm::CreateNewForm(ibFormRequest(), nullptr, nullptr, objectValue);
		valueForm->BuildForm(ibValueMetaObjectReport::eFormReport);
		try {
			valueForm->ShowForm();
		}
		catch (...) {
			// A form that failed to show goes with its holder — nobody else took it.
			if (appData->EnterpriseMode() || appData->ServiceMode()) {
				//decrRef - for control delete
				objectValue->DecrRef();
				return false;
			}
		}
	}

	//decrRef - for control delete 
	objectValue->DecrRef();

	return true;
}

//main module - destroy
bool ibValueModuleRuntimeManagerExternalReport::ExitMainModule(bool force)
{
	if (force)
		return true;

	if (!m_initialized)
		return false;

	return true;
}

//****************************************************************************
//*                      ibValueModuleRuntimeManagerExternalDataProcessor                 *
//****************************************************************************

void ibValueModuleRuntimeManagerExternalDataProcessor::FillMembers(ibMemberTable& helper) const
{
	ibValueRecordDataObjectExt* const objectValue = GetObjectValue();
	// Copy the external object value's surface; the module exports are appended after
	// these (the helper's tail, autobound by the ibRuntimeModuleDataObject ctor).
	if (objectValue != nullptr) {
		ibMemberTable* methodHelper = objectValue->GetPMethods();
		wxASSERT(methodHelper);
		for (long idx = 0; idx < methodHelper->GetNMethods(); idx++) {
			helper.CopyMethod(methodHelper, idx);
		}
		for (long idx = 0; idx < methodHelper->GetNProps(); idx++) {
			helper.CopyProp(methodHelper, idx);
		}
	}
}

bool ibValueModuleRuntimeManagerExternalDataProcessor::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	ibValueRecordDataObjectExt* const objectValue = GetObjectValue();
	if (objectValue &&
		objectValue->FindMethod(GetMethodName(lMethodNum)) != wxNOT_FOUND) {
		return objectValue->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	}

	return ibRuntimeModuleDataObject::ExecAsFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}

bool ibValueModuleRuntimeManagerExternalDataProcessor::GetParamDefValue(const long lMethodNum, const long lParamNum, ibValue& pvarParamDefValue) const
{
	ibValueRecordDataObjectExt* const objectValue = GetObjectValue();
	if (objectValue &&
		objectValue->FindMethod(GetMethodName(lMethodNum)) != wxNOT_FOUND) {
		return objectValue->GetParamDefValue(lMethodNum, lParamNum, pvarParamDefValue);
	}

	return ibRuntimeModuleDataObject::GetParamDefValue(
		GetMethodName(lMethodNum), lParamNum, pvarParamDefValue
	);
}

bool ibValueModuleRuntimeManagerExternalDataProcessor::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	ibValueRecordDataObjectExt* const objectValue = GetObjectValue();
	if (objectValue &&
		objectValue->FindProp(GetPropName(lPropNum)) != wxNOT_FOUND) {
		return objectValue->SetPropVal(lPropNum, varPropVal);
	}

	if (m_procUnit != nullptr) {
		return m_procUnit->SetPropVal(lPropNum, varPropVal);
	}

	return false;
}

bool ibValueModuleRuntimeManagerExternalDataProcessor::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	ibValueRecordDataObjectExt* const objectValue = GetObjectValue();
	if (objectValue &&
		objectValue->FindProp(GetPropName(lPropNum)) != wxNOT_FOUND) {
		return objectValue->GetPropVal(lPropNum, pvarPropVal);
	}

	if (m_procUnit != nullptr) {
		return m_procUnit->GetPropVal(lPropNum, pvarPropVal);
	}

	return false;
}

long ibValueModuleRuntimeManagerExternalDataProcessor::FindProp(const ibString& strName) const
{
	ibValueRecordDataObjectExt* const objectValue = GetObjectValue();
	if (objectValue &&
		objectValue->FindProp(strName) != wxNOT_FOUND) {
		return objectValue->FindProp(strName);
	}

	if (m_procUnit != nullptr) {
		return m_procUnit->FindProp(strName);
	}

	return ibValue::FindProp(strName);
}

//****************************************************************************
//*                      ibValueModuleRuntimeManagerExternalReport		                 *
//****************************************************************************

void ibValueModuleRuntimeManagerExternalReport::FillMembers(ibMemberTable& helper) const
{
	ibValueRecordDataObjectExt* const objectValue = GetObjectValue();
	// Copy the external object value's surface; the module exports are appended after
	// these (the helper's tail, autobound by the ibRuntimeModuleDataObject ctor).
	if (objectValue != nullptr) {
		ibMemberTable* methodHelper = objectValue->GetPMethods();
		wxASSERT(methodHelper);
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
	ibValueRecordDataObjectExt* const objectValue = GetObjectValue();
	if (objectValue &&
		objectValue->FindMethod(GetMethodName(lMethodNum)) != wxNOT_FOUND) {
		return objectValue->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	}

	return ibRuntimeModuleDataObject::ExecAsFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}

bool ibValueModuleRuntimeManagerExternalReport::GetParamDefValue(const long lMethodNum, const long lParamNum, ibValue& pvarParamDefValue) const
{
	ibValueRecordDataObjectExt* const objectValue = GetObjectValue();
	if (objectValue &&
		objectValue->FindMethod(GetMethodName(lMethodNum)) != wxNOT_FOUND) {
		return objectValue->GetParamDefValue(lMethodNum, lParamNum, pvarParamDefValue);
	}

	return ibRuntimeModuleDataObject::GetParamDefValue(
		GetMethodName(lMethodNum), lParamNum, pvarParamDefValue
	);
}

bool ibValueModuleRuntimeManagerExternalReport::SetPropVal(const long lPropNum, const ibValue& varPropVal)        //setting attribute
{
	ibValueRecordDataObjectExt* const objectValue = GetObjectValue();
	if (objectValue &&
		objectValue->FindProp(GetPropName(lPropNum)) != wxNOT_FOUND) {
		return objectValue->SetPropVal(lPropNum, varPropVal);
	}

	if (m_procUnit != nullptr) {
		return m_procUnit->SetPropVal(lPropNum, varPropVal);
	}

	return false;
}

bool ibValueModuleRuntimeManagerExternalReport::GetPropVal(const long lPropNum, ibValue& pvarPropVal)                   //attribute value
{
	ibValueRecordDataObjectExt* const objectValue = GetObjectValue();
	if (objectValue &&
		objectValue->FindProp(GetPropName(lPropNum)) != wxNOT_FOUND) {
		return objectValue->GetPropVal(lPropNum, pvarPropVal);
	}

	if (m_procUnit != nullptr) {
		return m_procUnit->GetPropVal(lPropNum, pvarPropVal);
	}

	return false;
}

long ibValueModuleRuntimeManagerExternalReport::FindProp(const ibString& strName) const
{
	ibValueRecordDataObjectExt* const objectValue = GetObjectValue();
	if (objectValue &&
		objectValue->FindProp(strName) != wxNOT_FOUND) {
		return objectValue->FindProp(strName);
	}

	if (m_procUnit != nullptr) {
		return m_procUnit->FindProp(strName);
	}

	return ibValue::FindProp(strName);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueModuleRuntimeManagerExternalDataProcessor, "ExternalDataProcessorModuleManager", system_to_clsid("SO_EDMM"));
SYSTEM_TYPE_REGISTER(ibValueModuleRuntimeManagerExternalReport, "ExternalReportModuleManager", system_to_clsid("SO_ERMM"));
