////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : information register manager
////////////////////////////////////////////////////////////////////////////

#include "informationRegisterManager.h"
#include "backend/metaData.h"

#include "object.h"

wxIMPLEMENT_DYNAMIC_CLASS(CInformationRegisterManager, CValue);

CInformationRegisterManager::CInformationRegisterManager(CMetaObjectInformationRegister* metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methodHelper(new CMethodHelper()), m_metaObject(metaObject)
{
}

CInformationRegisterManager::~CInformationRegisterManager()
{
	wxDELETE(m_methodHelper);
}

CMetaObjectCommonModule* CInformationRegisterManager::GetModuleManager() const {
	return m_metaObject->GetModuleManager();
}

#include "backend/objCtor.h"

class_identifier_t CInformationRegisterManager::GetClassType() const
{
	IMetaValueTypeCtor* clsFactory =
		m_metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CInformationRegisterManager::GetClassName() const
{
	IMetaValueTypeCtor* clsFactory =
		m_metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CInformationRegisterManager::GetString() const
{
	IMetaValueTypeCtor* clsFactory =
		m_metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

enum {
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

void CInformationRegisterManager::PrepareNames() const
{
	IMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	m_methodHelper->ClearHelper();

	//fill custom attributes 
	for (auto& obj : m_metaObject->GetObjects(g_metaEnumCLSID)) {
		if (obj->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			obj->GetName(),
			obj->GetMetaID()
		);
	}

	m_methodHelper->AppendFunc("createRecordSet", "createRecordSet()");
	m_methodHelper->AppendFunc("createRecordManager", "createRecordManager()");
	m_methodHelper->AppendFunc("createRecordKey", "createRecordKey()");
	m_methodHelper->AppendFunc("get", 2, "get(Filter...)");
	m_methodHelper->AppendFunc("getFirst", 3, "sliceFirst(beginOfPeriod, filter...)");
	m_methodHelper->AppendFunc("getLast", 2, "sliceLast(endOfPeriod, filter...)");
	m_methodHelper->AppendFunc("sliceFirst", 2, "sliceFirst(beginOfPeriod, filter...)");
	m_methodHelper->AppendFunc("sliceLast", 2, "sliceLast(endOfPeriod, filter...)");
	m_methodHelper->AppendFunc("select", "select()");
	m_methodHelper->AppendFunc("getForm", 3, "getForm(string, owner, guid)");
	m_methodHelper->AppendFunc("getRecordForm", 3, "getRecordForm(string, owner, guid)");
	m_methodHelper->AppendFunc("getListForm", 3, "getListForm(string, owner, guid)");

	CValue* pRefData = moduleManager->FindCommonModule(m_metaObject->GetModuleManager());
	if (pRefData != nullptr) {
		//добавляем методы из контекста
		for (long idx = 0; idx < pRefData->GetNMethods(); idx++) {
			m_methodHelper->CopyMethod(pRefData->GetPMethods(), idx);
		}
	}
}

#include "selector/objectSelector.h"

bool CInformationRegisterManager::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	IMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);

	switch (lMethodNum)
	{
	case eCreateRecordSet:
		pvarRetValue = m_metaObject->CreateRecordSetObjectValue();
		return true;
	case eCreateRecordManager:
		pvarRetValue = m_metaObject->CreateRecordManagerObjectValue();
		return true;
	case eCreateRecordKey:
		pvarRetValue = metaData->CreateAndConvertObjectValueRef<CRecordKeyObject>(m_metaObject);
		return true;
	case eGet:
		pvarRetValue = lSizeArray > 1 ?
			CInformationRegisterManager::Get(*paParams[0], *paParams[1])
			: lSizeArray > 0 ?
			CInformationRegisterManager::Get(*paParams[0]) :
			CInformationRegisterManager::Get();
		return true;
	case eGetFirst:
		pvarRetValue = lSizeArray > 1 ?
			CInformationRegisterManager::GetFirst(*paParams[0], *paParams[1])
			: CInformationRegisterManager::GetFirst(*paParams[0]);
		return true;
	case eGetLast:
		pvarRetValue = lSizeArray > 1 ?
			CInformationRegisterManager::GetLast(*paParams[0], *paParams[1])
			: CInformationRegisterManager::GetLast(*paParams[0]);
		return true;
	case eSliceFirst:
		pvarRetValue = lSizeArray > 1 ?
			CInformationRegisterManager::SliceFirst(*paParams[0], *paParams[1])
			: CInformationRegisterManager::SliceFirst(*paParams[0]);
	case eSliceLast:
		pvarRetValue = lSizeArray > 1 ?
			CInformationRegisterManager::SliceLast(*paParams[0], *paParams[1])
			: CInformationRegisterManager::SliceLast(*paParams[0]);
		return true;
	case eSelect:
		pvarRetValue = metaData->CreateAndConvertObjectValueRef<CSelectorRegisterObject>(m_metaObject);
		return true;
	case eGetForm:
	{
		CValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<CValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetGenericForm(paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<IBackendControlFrame>() : nullptr,
			guidVal ? ((Guid)*guidVal) : Guid());
		return true;
	}
	case eGetRecordForm:
	{
		CRecordKeyObject* keyVal = lSizeArray > 2 ? paParams[2]->ConvertToType<CRecordKeyObject>() : nullptr;
		pvarRetValue = m_metaObject->GetRecordForm(paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<IBackendControlFrame>() : nullptr,
			keyVal ? (keyVal->GetUniqueKey()) : nullptr);
		return true;
	}
	case eGetListForm:
	{
		CValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<CValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetListForm(paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<IBackendControlFrame>() : nullptr,
			guidVal ? ((Guid)*guidVal) : Guid());
		return true;
	}
	}

	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	CValue* pRefData = moduleManager->FindCommonModule(m_metaObject->GetModuleManager());
	if (pRefData != nullptr)
		return pRefData->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	return false;
}