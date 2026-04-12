////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : module manager - common modules
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"

#include "backend/system/systemManager.h"
#include "backend/appData.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueModuleManager::ibValueModuleUnit, ibValue);

ibValueModuleManager::ibValueModuleUnit::ibValueModuleUnit(ibValueModuleManager *moduleManager, ibValueMetaObjectModuleBase *moduleObject, bool managerModule) :
	ibValue(ibValueTypes::TYPE_VALUE, true), ibModuleDataObject(new ibCompileCommonModule(moduleObject)),
	m_methodHelper(new ibValueMethodHelper()),
	m_moduleManager(moduleManager),
	m_moduleObject(moduleObject)
{
}

ibValueModuleManager::ibValueModuleUnit::~ibValueModuleUnit()
{
	wxDELETE(m_methodHelper);
}

#define objectManager wxT("Manager")

//common module 
bool ibValueModuleManager::ibValueModuleUnit::CreateCommonModule()
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
			if (!ibValueModuleUnit::IsGlobalModule()) {
				m_procUnit = new ibProcUnit();
				m_procUnit->SetParent(m_moduleManager->GetProcUnit());
				m_procUnit->Execute(m_compileModule->m_cByteCode, false);
			}
		}
		catch (const ibBackendException *){
			return false;
		};
	}

	ibValueModuleUnit::PrepareNames();
	return true;
}

bool ibValueModuleManager::ibValueModuleUnit::DestroyCommonModule()
{
	wxASSERT(m_moduleManager != nullptr);

	m_compileModule->RemoveVariable(objectManager);
	m_compileModule->Reset();

	wxDELETE(m_procUnit);
	return true;
}

void ibValueModuleManager::ibValueModuleUnit::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	if (m_procUnit != nullptr) {
		ibByteCode* byteCode = m_procUnit->GetByteCode();
		if (byteCode != nullptr) {
			for (auto exportFunction : byteCode->m_listExportFunc) {
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

bool ibValueModuleManager::ibValueModuleUnit::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	return ibModuleDataObject::ExecuteProc(
		GetMethodName(lMethodNum), paParams, lSizeArray
	);
}

bool ibValueModuleManager::ibValueModuleUnit::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	return ibModuleDataObject::ExecuteFunc(
		GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
	);
}