////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : module manager - common modules
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"

#include "backend/systemManager/systemManager.h"
#include "backend/appData.h"

wxIMPLEMENT_DYNAMIC_CLASS(IModuleManager::CModuleUnit, CValue);

IModuleManager::CModuleUnit::CModuleUnit(IModuleManager *moduleManager, CMetaObjectModule *moduleObject, bool managerModule) :
	CValue(eValueTypes::TYPE_VALUE, true), IModuleInfo(new CCompileModule(moduleObject, true)),
	m_methodHelper(new CMethodHelper()),
	m_moduleManager(moduleManager),
	m_moduleObject(moduleObject)
{
}

IModuleManager::CModuleUnit::~CModuleUnit()
{
	wxDELETE(m_methodHelper);
}

#define objectManager wxT("manager")

//common module 
bool IModuleManager::CModuleUnit::CreateCommonModule()
{
	wxASSERT(m_moduleManager != nullptr);

	m_compileModule->SetParent(m_moduleManager->GetCompileModule());
	//create singleton "manager"
	m_compileModule->AddContextVariable(objectManager, m_moduleManager->GetObjectManager());

	wxDELETE(m_procUnit);

	if (!appData->DesignerMode()) {
		try {
			m_compileModule->Compile();
			// у глобального модуля код исполняется в главном модуле! 
			if (!CModuleUnit::IsGlobalModule()) {
				m_procUnit = new CProcUnit();
				m_procUnit->SetParent(m_moduleManager->GetProcUnit());
				m_procUnit->Execute(m_compileModule->m_cByteCode, false);
			}
		}
		catch (const CBackendException *){
			return false;
		};
	}

	CModuleUnit::PrepareNames();
	return true;
}

bool IModuleManager::CModuleUnit::DestroyCommonModule()
{
	wxASSERT(m_moduleManager != nullptr);

	m_compileModule->RemoveVariable(objectManager);
	m_compileModule->Reset();

	wxDELETE(m_procUnit);
	return true;
}

void IModuleManager::CModuleUnit::PrepareNames() const
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
		}
	}
}

bool IModuleManager::CModuleUnit::CallAsProc(const long lMethodNum, CValue** paParams, const long lSizeArray)
{
	return IModuleInfo::ExecuteProc(
		GetMethodName(lMethodNum), paParams, lSizeArray
	);
}

bool IModuleManager::CModuleUnit::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	return IModuleInfo::ExecuteFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}