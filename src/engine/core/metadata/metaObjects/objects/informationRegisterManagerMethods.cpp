////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : information register manager manager - methods
////////////////////////////////////////////////////////////////////////////

#include "informationRegisterManager.h"
#include "compiler/methods.h"

enum
{
	eCreateRecordSet,
	eCreateRecordManager,
	eCreateRecordKey,

	eGet,

	eGetFirst,
	eGetLast,

	eSliceFirst,
	eSliceLast,

	eSelect,

	eGetForm,
	eGetRecordForm,
	eGetListForm,
};

#include "metadata/metadata.h"

void CInformationRegisterManager::PrepareNames() const
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
		{"createRecordManager", "createRecordManager()"},
		{"createRecordKey", "createRecordKey()"},

		{"get", "get(Filter...)"},

		{"getFirst", "sliceFirst(beginOfPeriod, filter...)"},
		{"getLast", "sliceLast(endOfPeriod, filter...)"},

		{"sliceFirst", "sliceFirst(beginOfPeriod, filter...)"},
		{"sliceLast", "sliceLast(endOfPeriod, filter...)"},

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

CValue CInformationRegisterManager::Method(methodArg_t& aParams)
{
	CValue ret;

	switch (aParams.GetIndex())
	{
	case eCreateRecordSet: return m_metaObject->CreateRecordSetObjectValue();
	case eCreateRecordManager: return m_metaObject->CreateRecordManagerObjectValue();
	case eCreateRecordKey: return new CRecordKeyObject(m_metaObject);

	case eGet:
		return aParams.GetParamCount() > 1 ?
			CInformationRegisterManager::Get(aParams[0], aParams[1])
			: aParams.GetParamCount() > 0 ?
			CInformationRegisterManager::Get(aParams[0]) :
			CInformationRegisterManager::Get();
	case eGetFirst:
		return aParams.GetParamCount() > 1 ?
			CInformationRegisterManager::GetFirst(aParams[0], aParams[1])
			: CInformationRegisterManager::GetFirst(aParams[0]);
	case eGetLast:
		return aParams.GetParamCount() > 1 ?
			CInformationRegisterManager::GetLast(aParams[0], aParams[1])
			: CInformationRegisterManager::GetLast(aParams[0]);
	case eSliceFirst:
		return aParams.GetParamCount() > 1 ?
			CInformationRegisterManager::SliceFirst(aParams[0], aParams[1])
			: CInformationRegisterManager::SliceFirst(aParams[0]);
	case eSliceLast:
		return aParams.GetParamCount() > 1 ?
			CInformationRegisterManager::SliceLast(aParams[0], aParams[1])
			: CInformationRegisterManager::SliceLast(aParams[0]);
	case eSelect:
	{
		class CSelectorInformationRegisterObject : public ISelectorRegisterObject {
		public:
			CSelectorInformationRegisterObject(CMetaObjectInformationRegister* metaObject) :
				ISelectorRegisterObject(metaObject)
			{
			}
		};

		return new CSelectorInformationRegisterObject(m_metaObject);
	}
	case eGetForm:
	{
		CValueGuid* guidVal = aParams.GetParamCount() > 2 ? aParams[2].ConvertToType<CValueGuid>() : NULL;
		return m_metaObject->GetGenericForm(aParams[0].GetString(),
			aParams.GetParamCount() > 1 ? aParams[1].ConvertToType<IValueFrame>() : NULL,
			guidVal ? ((Guid)*guidVal) : Guid());
	}
	case eGetRecordForm:
	{
		CRecordKeyObject* keyVal = aParams.GetParamCount() > 2 ? aParams[2].ConvertToType<CRecordKeyObject>() : NULL;
		return m_metaObject->GetRecordForm(aParams[0].GetString(),
			aParams.GetParamCount() > 1 ? aParams[1].ConvertToType<IValueFrame>() : NULL,
			keyVal ? (keyVal->GetUniqueKey()) : NULL);
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