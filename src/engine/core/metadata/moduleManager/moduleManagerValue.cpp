////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : module manager - common modules
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"
#include "compiler/methods.h"
#include "compiler/systemObjects.h"
#include "appData.h"

wxIMPLEMENT_DYNAMIC_CLASS(IModuleManager::CModuleValue, CValue);

IModuleManager::CModuleValue::CModuleValue(IModuleManager *moduleManager, CMetaModuleObject *moduleObject, bool managerModule) :
	CValue(eValueTypes::TYPE_MODULE, true), IModuleInfo(new CCompileModule(moduleObject, true)),
	m_methods(new CMethods()),
	m_moduleManager(moduleManager),
	m_moduleObject(moduleObject)
{
}

IModuleManager::CModuleValue::~CModuleValue()
{
	wxDELETE(m_methods);
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
	std::vector<SEng> aMethods, aAttributes;

	if (m_procUnit != NULL) {
		CByteCode *m_byteCode = m_procUnit->GetByteCode();
		for (auto exportFunction : m_byteCode->m_aExportFuncList) {
			SEng methods;
			methods.sName = exportFunction.first;
			methods.sSynonym = wxT("procUnit");
			methods.iName = (int)exportFunction.second;
			aMethods.push_back(methods);
		}
	}

	m_methods->PrepareMethods(aMethods.data(), aMethods.size());
	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

CValue IModuleManager::CModuleValue::Method(methodArg_t &aParams)
{
	return IModuleInfo::ExecuteMethod(aParams);
}