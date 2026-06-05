
#include "moduleManagerExt.h"
#include "backend/metadataConfiguration.h"

#include "backend/appData.h"
#include "backend/session/session.h"

//////////////////////////////////////////////////////////////////////////////////
#define thisObject wxT("ThisObject")
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

std::map<wxString, ibContextVar>& ibValueModuleRuntimeManagerExternalDataProcessor::GetContextVariables()
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	wxASSERT(moduleManager);
	// Returns a reference — no null fallback possible from the manager, so guard
	// with a process-static empty map rather than dereferencing null in release
	// (the sibling GetCompileModule/GetProcUnit return pointers and null-fold).
	if (moduleManager == nullptr) {
		static std::map<wxString, ibContextVar> s_empty;
		return s_empty;
	}
	return moduleManager->GetContextVariables();
}

std::map<wxString, ibValue*>& ibValueModuleRuntimeManagerExternalDataProcessor::GetGlobalVariables()
{
	// Same delegation as GetContextVariables: an external data processor's editor
	// must see the configuration root's globals (Metadata + common modules
	// surfaced as names), not its own extern map.
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	wxASSERT(moduleManager);
	if (moduleManager == nullptr) {
		static std::map<wxString, ibValue*> s_empty;
		return s_empty;
	}
	return moduleManager->GetGlobalVariables();
}

ibValueModuleRuntimeManagerExternalDataProcessor::ibValueModuleRuntimeManagerExternalDataProcessor(ibMetaData* metadata, ibValueMetaObjectDataProcessor* metaObject)
	: ibValueModuleRuntimeManager(appEnv::ActiveMetaData(), metaObject ? metaObject->GetObjectModule() : nullptr)
{
	// metadata is the owning container — LoadFromFile creates this manager and passes
	// `this` (there is no bootstrap manager). In designer the value object never owns
	// the metadata. No member access on metadata here → this TU needs no container
	// header; the External value object's out-of-line dtor instantiates the drop elsewhere.
	m_objectValue = new ibValueRecordDataObjectExternalDataProcessor(
		metaObject, appData->DesignerMode() ? nullptr : metadata);
	//set complile module
	//set proc unit
	m_objectValue->m_compileModule = m_compileModule;
	m_objectValue->m_procUnit = m_procUnit;

	if (appData->DesignerMode()) {
		//incrRef
		m_objectValue->IncrRef();
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
	if (m_objectValue) {
		// m_objectValue BORROWS m_compileModule / m_procUnit from this manager (shared
		// in the ctor, raw pointer). This manager's base dtor is about to wxDELETE
		// m_compileModule, so the borrow must be cleared first or m_objectValue's dtor
		// double-frees it. DestroyMainModule only clears it when m_initialized — a
		// never-run manager (released by a detached-root swap) would skip that → the
		// double free. Clear unconditionally here.
		m_objectValue->m_compileModule = nullptr;
		m_objectValue->m_procUnit = nullptr;
	}
	if (appData->DesignerMode()) {
		//decrRef
		m_objectValue->DecrRef();
	}
}

bool ibValueModuleRuntimeManagerExternalDataProcessor::CreateMainModule()
{
	if (m_initialized)
		return true;

	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	wxASSERT(moduleManager);

	// Imperative pipeline — SetParent cascades compile+procUnit parents
	// to configuration root's; BindContextVariable wires thisObject;
	// InitializeRuntime / Compile / Run drive the top-level.
	ibRuntimeModuleDataObject::SetParent(moduleManager);
	BindContextVariable(thisObject, m_objectValue);

	// m_objectValue (the external DP's script-visible object) shares the
	// same compile module — it's the script-side wrapper around the
	// same source. Set before InitializeObject so the object sees it.
	m_objectValue->m_compileModule = m_compileModule;

	m_objectValue->InitializeObject();

	if (!appData->DesignerMode()) {
		InitializeRuntime();
		// Share procUnit with m_objectValue — both descriptors reach
		// the same runtime; shared_ptr co-ownership keeps the unit
		// alive until the last of them drops.
		m_objectValue->m_procUnit = m_procUnit;

		try {
			Compile();
			Run();
		}
		catch (const ibBackendException& err) {
			wxLogWarning(_("External module '%s' init failed: %s"),
				m_objectValue ? m_objectValue->GetClassName() : wxString(wxEmptyString),
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
	m_procUnit.reset();

	//set complile module 
	//set proc unit
	m_objectValue->m_compileModule = nullptr;
	m_objectValue->m_procUnit = nullptr;

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

	//incrRef - for control delete
	m_objectValue->IncrRef();

	const ibValueMetaObjectRecordData* commonObject = m_objectValue->GetMetaObject();
	wxASSERT(commonObject);
	ibValueMetaObjectFormBase* defFormObject = commonObject->GetDefaultFormByID
	(
		ibValueMetaObjectDataProcessor::eFormDataProcessor
	);

	if (defFormObject != nullptr) {

		ibBackendValueForm* result = nullptr;
		// Cache lives on the form's own metadata (external DP for .epf, main config
		// for embedded DP) — m_metaManager->GetMetaData() is the configuration
		// passed to base ctor and would miss for external DPs.
		ibCompileValueCache* cc = defFormObject->GetMetaData()->GetCompileCache();

		if (!cc || !cc->FindCompileModule(defFormObject, result)) {

			result = ibValueMetaObjectFormBase::CreateAndBuildForm(defFormObject, nullptr, m_objectValue);

			if (result != nullptr) {
				result->ShowForm();
			}
			else if (!appData->DesignerMode()) {
				//decrRef - for control delete
				m_objectValue->DecrRef();
				return false;
			}

		}
	}
	//else {
	//	ibBackendValueForm* valueForm = ibBackendValueForm::CreateNewForm(nullptr, nullptr, m_objectValue, ibGuid::newGuid());
	//	valueForm->BuildForm(ibValueMetaObjectDataProcessor::eFormDataProcessor);
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

std::map<wxString, ibContextVar>& ibValueModuleRuntimeManagerExternalReport::GetContextVariables()
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	wxASSERT(moduleManager);
	// Returns a reference — no null fallback possible from the manager, so guard
	// with a process-static empty map rather than dereferencing null in release
	// (the sibling GetCompileModule/GetProcUnit return pointers and null-fold).
	if (moduleManager == nullptr) {
		static std::map<wxString, ibContextVar> s_empty;
		return s_empty;
	}
	return moduleManager->GetContextVariables();
}

std::map<wxString, ibValue*>& ibValueModuleRuntimeManagerExternalReport::GetGlobalVariables()
{
	// Delegate to the configuration root — see DataProcessor::GetGlobalVariables.
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	wxASSERT(moduleManager);
	if (moduleManager == nullptr) {
		static std::map<wxString, ibValue*> s_empty;
		return s_empty;
	}
	return moduleManager->GetGlobalVariables();
}

ibValueModuleRuntimeManagerExternalReport::ibValueModuleRuntimeManagerExternalReport(ibMetaData* metadata, ibValueMetaObjectReport* metaObject)
	: ibValueModuleRuntimeManager(appEnv::ActiveMetaData(), metaObject ? metaObject->GetObjectModule() : nullptr)
{
	// See the data-processor manager above: metadata is the owning container (passed
	// by LoadFromFile), never owned in designer. No member access here.
	m_objectValue = new ibValueRecordDataObjectExternalReport(
		metaObject, appData->DesignerMode() ? nullptr : metadata);
	//set complile module
	//set proc unit
	m_objectValue->m_compileModule = m_compileModule;
	m_objectValue->m_procUnit = m_procUnit;

	if (appData->DesignerMode()) {
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
		// m_objectValue BORROWS m_compileModule / m_procUnit from this manager (shared
		// in the ctor, raw pointer). The base dtor is about to wxDELETE m_compileModule,
		// so clear the borrow first or m_objectValue's dtor double-frees it. A never-run
		// manager (released by a detached-root swap) skips this in DestroyMainModule
		// (m_initialized false) → the double free. Clear unconditionally here.
		m_objectValue->m_compileModule = nullptr;
		m_objectValue->m_procUnit = nullptr;
	}
	if (appData->DesignerMode()) {
		//decrRef
		m_objectValue->DecrRef();
	}
}

bool ibValueModuleRuntimeManagerExternalReport::CreateMainModule()
{
	if (m_initialized)
		return true;

	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(appEnv::ActiveMetaData());
	wxASSERT(moduleManager);

	// Imperative pipeline — see ExternalDataProcessor::CreateMainModule
	// for the same shape; parent cascade, context var, shared procUnit
	// with m_objectValue.
	ibRuntimeModuleDataObject::SetParent(moduleManager);
	BindContextVariable(thisObject, m_objectValue);

	m_objectValue->m_compileModule = m_compileModule;
	m_objectValue->InitializeObject();

	if (!appData->DesignerMode()) {
		InitializeRuntime();
		m_objectValue->m_procUnit = m_procUnit;

		try {
			Compile();
			Run();
		}
		catch (const ibBackendException& err) {
			wxLogWarning(_("External module '%s' re-init failed: %s"),
				m_objectValue ? m_objectValue->GetClassName() : wxString(wxEmptyString),
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
	m_procUnit.reset();

	//set complile module 
	//set proc unit 
	m_objectValue->m_compileModule = nullptr;
	m_objectValue->m_procUnit = nullptr;

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

	//incrRef - for control delete
	m_objectValue->IncrRef();

	const ibValueMetaObjectRecordData* commonObject = m_objectValue->GetMetaObject();
	wxASSERT(commonObject);
	ibValueMetaObjectFormBase* defFormObject = commonObject->GetDefaultFormByID
	(
		ibValueMetaObjectReport::eFormReport
	);

	if (defFormObject != nullptr) {
		ibBackendValueForm* result = nullptr;
		// Cache lives on the form's own metadata — see the symmetric DataProcessor
		// path above for rationale.
		ibCompileValueCache* cc = defFormObject->GetMetaData()->GetCompileCache();
		if (!cc || !cc->FindCompileModule(defFormObject, result)) {

			result = ibValueMetaObjectFormBase::CreateAndBuildForm(defFormObject, nullptr, m_objectValue);

			if (result != nullptr) {
				result->ShowForm();
			}
			else if (!appData->DesignerMode()) {
				//decrRef - for control delete
				m_objectValue->DecrRef();
				return false;
			}
		}
	}
	//else {
	//	ibBackendValueForm* valueForm = ibBackendValueForm::CreateNewForm(nullptr, nullptr, m_objectValue, ibGuid::newGuid());
	//	valueForm->BuildForm(ibValueMetaObjectReport::eFormReport);
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
	// Copy the external object value's surface; the module exports are appended after
	// these (the helper's tail, autobound by the ibRuntimeModuleDataObject ctor).
	if (m_objectValue != nullptr) {
		ibMemberTable* methodHelper = m_objectValue->GetPMethods();
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
	if (m_objectValue &&
		m_objectValue->FindMethod(GetMethodName(lMethodNum)) != wxNOT_FOUND) {
		return m_objectValue->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	}

	return ibRuntimeModuleDataObject::ExecAsFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}

bool ibValueModuleRuntimeManagerExternalDataProcessor::SetPropVal(const long lPropNum, const ibValue& varPropVal)
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

bool ibValueModuleRuntimeManagerExternalDataProcessor::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
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

long ibValueModuleRuntimeManagerExternalDataProcessor::FindProp(const wxString& strName) const
{
	if (m_objectValue &&
		m_objectValue->FindProp(strName) != wxNOT_FOUND) {
		return m_objectValue->FindProp(strName);
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
	// Copy the external object value's surface; the module exports are appended after
	// these (the helper's tail, autobound by the ibRuntimeModuleDataObject ctor).
	if (m_objectValue != nullptr) {
		ibMemberTable* methodHelper = m_objectValue->GetPMethods();
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
	if (m_objectValue &&
		m_objectValue->FindMethod(GetMethodName(lMethodNum)) != wxNOT_FOUND) {
		return m_objectValue->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	}

	return ibRuntimeModuleDataObject::ExecAsFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}

bool ibValueModuleRuntimeManagerExternalReport::SetPropVal(const long lPropNum, const ibValue& varPropVal)        //setting attribute
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

bool ibValueModuleRuntimeManagerExternalReport::GetPropVal(const long lPropNum, ibValue& pvarPropVal)                   //attribute value
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

long ibValueModuleRuntimeManagerExternalReport::FindProp(const wxString& strName) const
{
	if (m_objectValue &&
		m_objectValue->FindProp(strName) != wxNOT_FOUND) {
		return m_objectValue->FindProp(strName);
	}

	if (m_procUnit != nullptr) {
		return m_procUnit->FindProp(strName);
	}

	return ibValue::FindProp(strName);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueModuleRuntimeManagerExternalDataProcessor, "ExternalDataProcessorModuleManager", string_to_clsid("SO_EDMM"));
SYSTEM_TYPE_REGISTER(ibValueModuleRuntimeManagerExternalReport, "ExternalReportModuleManager", string_to_clsid("SO_ERMM"));
