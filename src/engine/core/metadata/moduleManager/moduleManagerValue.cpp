////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : module manager - common modules
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"

#include "core/compiler/systemObjects.h"
#include "appData.h"

wxIMPLEMENT_DYNAMIC_CLASS(IModuleManager::CModuleValue, CValue);

IModuleManager::CModuleValue::CModuleValue(IModuleManager *moduleManager, CMetaModuleObject *moduleObject, bool managerModule) :
	CValue(eValueTypes::TYPE_MODULE, true), IModuleInfo(new CCompileModule(moduleObject, true)),
	m_methodHelper(new CMethodHelper()),
	m_moduleManager(moduleManager),
	m_moduleObject(moduleObject)
{
}

IModuleManager::CModuleValue::~CModuleValue()
{
	wxDELETE(m_methodHelper);
}

#define objectManager wxT("manager")

//common module 
bool IModuleManager::CModuleValue::CreateCommonModule()
{
	wxASSERT(m_moduleManager != NULL);

	m_compileModule->SetParent(m_moduleManager->GetCompileModule());
	//create singleton "manager"
	m_compileModule->AddContextVariable(objectManager, m_moduleManager->GetObjectManager());

	wxDELETE(m_procUnit);

	if (appData->EnterpriseMode()) {
		try {
			m_compileModule->Compile();
		}
		catch (const CTranslateError *){
			return false;
		};

		// у глобального модуля код исполняется в главном модуле! 
		if (!CModuleValue::IsGlobalModule()) {
			m_procUnit = new CProcUnit();
			m_procUnit->SetParent(m_moduleManager->GetProcUnit());
			m_procUnit->Execute(m_compileModule->m_cByteCode, false);
		}
	}

	return true;
}

bool IModuleManager::CModuleValue::DestroyCommonModule()
{
	wxASSERT(m_moduleManager != NULL);

	m_compileModule->RemoveVariable(objectManager);
	m_compileModule->Reset();

	wxDELETE(m_procUnit);

	return true;
}

void IModuleManager::CModuleValue::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	if (m_procUnit != NULL) {
		byteCode_t* byteCode = m_procUnit->GetByteCode();
		for (auto exportFunction : byteCode->m_aExportFuncList) {
			m_methodHelper->AppendMethod(
				exportFunction.first,
				byteCode->GetNParams(exportFunction.second),
				byteCode->HasRetVal(exportFunction.second),
				exportFunction.second,
				eProcUnit
			);
		}
	}
}

bool IModuleManager::CModuleValue::CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray)
{
	return IModuleInfo::ExecuteProc(
		GetMethodName(lMethodNum), paParams, lSizeArray
	);
}

bool IModuleManager::CModuleValue::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	return IModuleInfo::ExecuteFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}