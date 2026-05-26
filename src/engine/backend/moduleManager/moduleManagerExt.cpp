
#include "moduleManagerExt.h"
#include "backend/metadataConfiguration.h"

#include "backend/appData.h"
#include "backend/session/session.h"

//////////////////////////////////////////////////////////////////////////////////
#define thisObject wxT("ThisObject")
//////////////////////////////////////////////////////////////////////////////////
//  ibValueModuleManagerExternalDataProcessor
//////////////////////////////////////////////////////////////////////////////////

ibCompileModule* ibValueModuleManagerExternalDataProcessor::GetCompileModule() const
{
	ibSession* session = ibSession::Current();
	ibValueModuleManager* moduleManager = session ? session->GetManagerModule() : nullptr;
	wxASSERT(moduleManager);
	return moduleManager->GetCompileModule();
}

std::shared_ptr<ibProcUnit> ibValueModuleManagerExternalDataProcessor::GetProcUnit() const
{
	ibSession* session = ibSession::Current();
	ibValueModuleManager* moduleManager = session ? session->GetManagerModule() : nullptr;
	wxASSERT(moduleManager);
	return moduleManager->GetProcUnit();
}

std::map<wxString, ibValue*>& ibValueModuleManagerExternalDataProcessor::GetContextVariables()
{
	ibSession* session = ibSession::Current();
	ibValueModuleManager* moduleManager = session ? session->GetManagerModule() : nullptr;
	wxASSERT(moduleManager);
	return moduleManager->GetContextVariables();
}

ibValueModuleManagerExternalDataProcessor::ibValueModuleManagerExternalDataProcessor(ibMetaData* metadata, ibValueMetaObjectDataProcessor* metaObject)
	: ibValueModuleManager(appEnv::ActiveMetaData(), metaObject ? metaObject->GetObjectModule() : nullptr)
{
	m_objectValue = new ibValueRecordDataObjectDataProcessor(metaObject);
	//set complile module 
	//set proc unit 
	m_objectValue->m_compileModule = m_compileModule;
	m_objectValue->m_procUnit = m_procUnit;

	if (appData->DesignerMode()) {
		//incrRef
		m_objectValue->IncrRef();
	}
}

ibValueModuleManagerExternalDataProcessor::~ibValueModuleManagerExternalDataProcessor()
{
	if (appData->DesignerMode()) {
		//decrRef
		m_objectValue->DecrRef();
	}
}

bool ibValueModuleManagerExternalDataProcessor::CreateMainModule()
{
	if (m_initialized)
		return true;

	ibSession* session = ibSession::Current();
	ibValueModuleManager* moduleManager = session ? session->GetManagerModule() : nullptr;
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

bool ibValueModuleManagerExternalDataProcessor::DestroyMainModule()
{
	if (!m_initialized)
		return true;

	m_compileModule->Reset();
	m_procUnit.reset();

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
bool ibValueModuleManagerExternalDataProcessor::StartMainModule(bool force)
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
bool ibValueModuleManagerExternalDataProcessor::ExitMainModule(bool force)
{
	if (force)
		return true;

	if (!m_initialized)
		return false;

	return true;
}

//////////////////////////////////////////////////////////////////////////////////
//  ibValueModuleManagerExternalReport
//////////////////////////////////////////////////////////////////////////////////

ibCompileModule* ibValueModuleManagerExternalReport::GetCompileModule() const
{
	ibSession* session = ibSession::Current();
	ibValueModuleManager* moduleManager = session ? session->GetManagerModule() : nullptr;
	wxASSERT(moduleManager);
	return moduleManager->GetCompileModule();
}

std::shared_ptr<ibProcUnit> ibValueModuleManagerExternalReport::GetProcUnit() const
{
	ibSession* session = ibSession::Current();
	ibValueModuleManager* moduleManager = session ? session->GetManagerModule() : nullptr;
	wxASSERT(moduleManager);
	return moduleManager->GetProcUnit();
}

std::map<wxString, ibValue*>& ibValueModuleManagerExternalReport::GetContextVariables()
{
	ibSession* session = ibSession::Current();
	ibValueModuleManager* moduleManager = session ? session->GetManagerModule() : nullptr;
	wxASSERT(moduleManager);
	return moduleManager->GetContextVariables();
}

ibValueModuleManagerExternalReport::ibValueModuleManagerExternalReport(ibMetaData* metadata, ibValueMetaObjectReport* metaObject)
	: ibValueModuleManager(appEnv::ActiveMetaData(), metaObject ? metaObject->GetObjectModule() : nullptr)
{
	m_objectValue = new ibValueRecordDataObjectReport(metaObject);
	//set complile module 
	//set proc unit 
	m_objectValue->m_compileModule = m_compileModule;
	m_objectValue->m_procUnit = m_procUnit;

	if (appData->DesignerMode()) {
		//incrRef
		m_objectValue->IncrRef();
	}
}

ibValueModuleManagerExternalReport::~ibValueModuleManagerExternalReport()
{
	if (appData->DesignerMode()) {
		//decrRef
		m_objectValue->DecrRef();
	}
}

bool ibValueModuleManagerExternalReport::CreateMainModule()
{
	if (m_initialized)
		return true;

	ibSession* session = ibSession::Current();
	ibValueModuleManager* moduleManager = session ? session->GetManagerModule() : nullptr;
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

bool ibValueModuleManagerExternalReport::DestroyMainModule()
{
	if (!m_initialized)
		return true;

	m_compileModule->Reset();
	m_procUnit.reset();

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
bool ibValueModuleManagerExternalReport::StartMainModule(bool force)
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
bool ibValueModuleManagerExternalReport::ExitMainModule(bool force)
{
	if (force)
		return true;

	if (!m_initialized)
		return false;

	return true;
}

//****************************************************************************
//*                      ibValueModuleManagerExternalDataProcessor                 *
//****************************************************************************

void ibValueModuleManagerExternalDataProcessor::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	if (m_objectValue != nullptr) {
		m_objectValue->PrepareNames();
		ibValueMethodHelper* methodHelper = m_objectValue->GetPMethods();
		wxASSERT(methodHelper);
		for (long idx = 0; idx < methodHelper->GetNMethods(); idx++) {
			m_methodHelper->CopyMethod(methodHelper, idx);
		}
		for (long idx = 0; idx < methodHelper->GetNProps(); idx++) {
			m_methodHelper->CopyProp(methodHelper, idx);
		}
	}

	ExportNamesToHelper(m_methodHelper, eProcUnit);
}

bool ibValueModuleManagerExternalDataProcessor::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	if (m_objectValue &&
		m_objectValue->FindMethod(GetMethodName(lMethodNum)) != wxNOT_FOUND) {
		return m_objectValue->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	}

	return ibRuntimeModuleDataObject::ExecAsFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}

bool ibValueModuleManagerExternalDataProcessor::SetPropVal(const long lPropNum, const ibValue& varPropVal)
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

bool ibValueModuleManagerExternalDataProcessor::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
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

long ibValueModuleManagerExternalDataProcessor::FindProp(const wxString& strName) const
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
//*                      ibValueModuleManagerExternalReport		                 *
//****************************************************************************

void ibValueModuleManagerExternalReport::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	if (m_objectValue != nullptr) {
		m_objectValue->PrepareNames();
		ibValueMethodHelper* methodHelper = m_objectValue->GetPMethods();
		wxASSERT(methodHelper);
		for (long idx = 0; idx < methodHelper->GetNMethods(); idx++) {
			m_methodHelper->CopyMethod(methodHelper, idx);
		}
		for (long idx = 0; idx < methodHelper->GetNProps(); idx++) {
			m_methodHelper->CopyProp(methodHelper, idx);
		}
	}
	ExportNamesToHelper(m_methodHelper, eProcUnit);
}

bool ibValueModuleManagerExternalReport::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	if (m_objectValue &&
		m_objectValue->FindMethod(GetMethodName(lMethodNum)) != wxNOT_FOUND) {
		return m_objectValue->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	}

	return ibRuntimeModuleDataObject::ExecAsFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}

bool ibValueModuleManagerExternalReport::SetPropVal(const long lPropNum, const ibValue& varPropVal)        //setting attribute
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

bool ibValueModuleManagerExternalReport::GetPropVal(const long lPropNum, ibValue& pvarPropVal)                   //attribute value
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

long ibValueModuleManagerExternalReport::FindProp(const wxString& strName) const
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

SYSTEM_TYPE_REGISTER(ibValueModuleManagerExternalDataProcessor, "ExternalDataProcessorModuleManager", string_to_clsid("SO_EDMM"));
SYSTEM_TYPE_REGISTER(ibValueModuleManagerExternalReport, "ExternalReportModuleManager", string_to_clsid("SO_ERMM"));
