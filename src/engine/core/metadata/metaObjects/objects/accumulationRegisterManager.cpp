////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : acc register manager
////////////////////////////////////////////////////////////////////////////

#include "accumulationRegisterManager.h"
#include "core/metadata/metadata.h"

#include "object.h"

wxIMPLEMENT_DYNAMIC_CLASS(CAccumulationRegisterManager, CValue);

CAccumulationRegisterManager::CAccumulationRegisterManager(CMetaObjectAccumulationRegister* metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methodHelper(new CMethodHelper()), m_metaObject(metaObject)
{
}

CAccumulationRegisterManager::~CAccumulationRegisterManager()
{
	wxDELETE(m_methodHelper);
}

CMetaCommonModuleObject* CAccumulationRegisterManager::GetModuleManager() const { 
	return m_metaObject->GetModuleManager(); 
}

#include "core/metadata/singleClass.h"

CLASS_ID CAccumulationRegisterManager::GetTypeClass() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
}

wxString CAccumulationRegisterManager::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CAccumulationRegisterManager::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

enum Func {
	eCreateRecordSet,
	eCreateRecordKey,
	eBalance,
	eTurnover,
	eSelect,
	eGetForm,
	eGetListForm,
};

#include "core/metadata/metadata.h"

void CAccumulationRegisterManager::PrepareNames() const
{
	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	m_methodHelper->ClearHelper();

	//fill custom attributes 
	for (auto attributes : m_metaObject->GetObjects(g_metaEnumCLSID)) {
		if (attributes->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			attributes->GetName(),
			attributes->GetMetaID()
		);
	}

	m_methodHelper->AppendFunc("createRecordSet", "createRecordSet()");
	m_methodHelper->AppendFunc("createRecordKey", "createRecordKey()");
	m_methodHelper->AppendFunc("balance", 2, "balance(period, filter...)");
	m_methodHelper->AppendFunc("turnovers", 4, "turnovers(beginOfPeriod, endOfPeriod, filter...)");
	m_methodHelper->AppendFunc("select", "select()");
	m_methodHelper->AppendFunc("getForm", 3, "getForm(string, owner, guid)");
	m_methodHelper->AppendFunc("getRecordForm", 3, "getRecordForm(string, owner, guid)");
	m_methodHelper->AppendFunc("getListForm", 3, "getListForm(string, owner, guid)");

	CValue* pRefData = moduleManager->FindCommonModule(m_metaObject->GetModuleManager());
	if (pRefData != NULL) {
		//добавляем методы из контекста
		for (long idx = 0; idx < pRefData->GetNMethods(); idx++) {
			m_methodHelper->CopyMethod(pRefData->GetPMethods(), idx);
		}
	}
}

#include "selector/objectSelector.h"
#include "frontend/visualView/controls/form.h"

bool CAccumulationRegisterManager::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eCreateRecordSet:
		pvarRetValue = m_metaObject->CreateRecordSetObjectValue();
		return true;
	case eCreateRecordKey:
		pvarRetValue = new CRecordKeyObject(m_metaObject);
		return true;
	case eBalance: pvarRetValue = lSizeArray > 1 ?
		CAccumulationRegisterManager::Balance(*paParams[0], *paParams[1]) : CAccumulationRegisterManager::Balance(*paParams[0]);
		return true;
	case eTurnover: pvarRetValue = lSizeArray > 2 ?
		CAccumulationRegisterManager::Turnovers(*paParams[0], paParams[1], paParams[2]) : CAccumulationRegisterManager::Turnovers(*paParams[0], *paParams[1]);
		return true;
	case eSelect:
		pvarRetValue = new CSelectorRegisterObject(m_metaObject);
		return true;
	case eGetForm:
	{
		CValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<CValueGuid>() : NULL;
		pvarRetValue = m_metaObject->GetGenericForm(paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<IValueFrame>() : NULL,
			guidVal ? ((Guid)*guidVal) : Guid());
		return true;
	}
	case eGetListForm:
	{
		CValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<CValueGuid>() : NULL;
		pvarRetValue = m_metaObject->GetListForm(paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<IValueFrame>() : NULL,
			guidVal ? ((Guid)*guidVal) : Guid());
		return true;
	}
	}

	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	CValue* pRefData = moduleManager->FindCommonModule(m_metaObject->GetModuleManager());
	if (pRefData != NULL) 
		return pRefData->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	return false;
}