////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accumulation register manager manager - methods
////////////////////////////////////////////////////////////////////////////

#include "accumulationRegisterManager.h"
#include "compiler/methods.h"

enum
{
	eCreateRecordSet,
	eCreateRecordKey,

	eBalance,
	eTurnover,

	eSelect,

	eGetForm,
	eGetListForm,
};

#include "metadata/metadata.h"

void CAccumulationRegisterManager::PrepareNames() const
{
	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	std::vector<SEng> aAttributes;

	//fill custom attributes 
	for (auto attributes : m_metaObject->GetObjects(g_metaEnumCLSID)) {
		if (attributes->IsDeleted())
			continue;
		SEng attribute;
		attribute.sName = attributes->GetName();
		attribute.iName = attributes->GetMetaID();
		attribute.sSynonym = wxT("attribute");
		aAttributes.push_back(attribute);
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());

	SEng aMethods[] =
	{
		{"createRecordSet", "createRecordSet()"},
		{"createRecordKey", "createRecordKey()"},

		{"balance", "balance(period, filter...)"},
		{"turnovers", "turnovers(beginOfPeriod, endOfPeriod, filter...)"},

		{"select", "select()"},

		{"getForm", "getForm(string, owner, guid)"},
		{"getRecordForm", "getRecordForm(string, owner, guid)"},
		{"getListForm", "getListForm(string, owner, guid)"},
	};

	int nCountM = sizeof(aMethods) / sizeof(aMethods[0]);
	m_methods->PrepareMethods(aMethods, nCountM);

	CValue* pRefData = moduleManager->FindCommonModule(m_metaObject->GetModuleManager());

	if (pRefData) {
		//добавляем методы из контекста
		for (unsigned int idx = 0; idx < pRefData->GetNMethods(); idx++) {
			m_methods->AppendMethod(pRefData->GetMethodName(idx),
				pRefData->GetMethodDescription(idx),
				wxT("commonModule"));
		}
	}
}

#include "selector/objectSelector.h"
#include "frontend/visualView/controls/form.h"

CValue CAccumulationRegisterManager::Method(methodArg_t& aParams)
{
	CValue ret;

	switch (aParams.GetIndex())
	{
	case eCreateRecordSet: return m_metaObject->CreateRecordSetObjectValue();
	case eCreateRecordKey: return new CRecordKeyObject(m_metaObject);

	case eBalance: return aParams.GetParamCount() > 1 ?
		CAccumulationRegisterManager::Balance(aParams[0], aParams[1]) : CAccumulationRegisterManager::Balance(aParams[0]);
	case eTurnover: return aParams.GetParamCount() > 2 ?
		CAccumulationRegisterManager::Turnovers(aParams[0], aParams[1], aParams[2]) : CAccumulationRegisterManager::Turnovers(aParams[0], aParams[1]);
	case eSelect:
	{
		class CSelectorAccumulationRegisterObject : public ISelectorRegisterObject {
		public:
			CSelectorAccumulationRegisterObject(CMetaObjectAccumulationRegister* metaObject) :
				ISelectorRegisterObject(metaObject)
			{
			}
		};

		return new CSelectorAccumulationRegisterObject(m_metaObject);
	}
	case eGetForm:
	{
		CValueGuid* guidVal = aParams.GetParamCount() > 2 ? aParams[2].ConvertToType<CValueGuid>() : NULL;
		return m_metaObject->GetGenericForm(aParams[0].GetString(),
			aParams.GetParamCount() > 1 ? aParams[1].ConvertToType<IValueFrame>() : NULL,
			guidVal ? ((Guid)*guidVal) : Guid());
	}
	case eGetListForm:
	{
		CValueGuid* guidVal = aParams.GetParamCount() > 2 ? aParams[2].ConvertToType<CValueGuid>() : NULL;
		return m_metaObject->GetListForm(aParams[0].GetString(),
			aParams.GetParamCount() > 1 ? aParams[1].ConvertToType<IValueFrame>() : NULL,
			guidVal ? ((Guid)*guidVal) : Guid());
	}
	}

	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	CValue* pRefData = moduleManager->FindCommonModule(m_metaObject->GetModuleManager());

	if (pRefData) {
		return pRefData->Method(aParams);
	}

	return ret;
}