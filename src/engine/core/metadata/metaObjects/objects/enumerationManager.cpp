////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enumeration manager
////////////////////////////////////////////////////////////////////////////

#include "enumerationManager.h"
#include "core/metadata/metadata.h"

#include "object.h"

wxIMPLEMENT_DYNAMIC_CLASS(CEnumerationManager, CValue);

CEnumerationManager::CEnumerationManager(CMetaObjectEnumeration* metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methodHelper(new CMethodHelper()), m_metaObject(metaObject)
{
}

CEnumerationManager::~CEnumerationManager()
{
	wxDELETE(m_methodHelper);
}

CMetaCommonModuleObject* CEnumerationManager::GetModuleManager() const { return m_metaObject->GetModuleManager(); }

#include "core/metadata/singleClass.h"

CLASS_ID CEnumerationManager::GetTypeClass() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
}

wxString CEnumerationManager::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CEnumerationManager::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

enum Func {
	eGetForm,
	eGetListForm,
	eGetSelectForm,
};

#include "core/metadata/metadata.h"

void CEnumerationManager::PrepareNames() const
{
	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	m_methodHelper->ClearHelper();
	m_methodHelper->AppendFunc("getForm", 3, "getForm(string, owner, guid)");
	m_methodHelper->AppendFunc("getListForm", 3, "getListForm(string, owner, guid)");
	m_methodHelper->AppendFunc("getSelectForm", 3, "getSelectForm(string, owner, guid)");

	//fill custom attributes 
	for (auto attribute : m_metaObject->GetObjects(g_metaEnumCLSID)) {
		if (attribute->IsDeleted())
			continue;
		m_methodHelper->AppendProp(
			attribute->GetName(),
			true, false,
			attribute->GetMetaID(),
			s_def_alias
		);
	}

	CValue* pRefData = moduleManager->FindCommonModule(m_metaObject->GetModuleManager());
	if (pRefData != NULL) {
		//добавляем методы из контекста
		for (long idx = 0; idx < pRefData->GetNMethods(); idx++) {
			m_methodHelper->CopyMethod(pRefData->GetPMethods(), idx);
		}
	}
}

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************
bool CEnumerationManager::SetPropVal(const long lPropNum, CValue& cValue) {
	return false;
}

bool CEnumerationManager::GetPropVal(const long lPropNum, CValue& pvarPropVal) {
	pvarPropVal = CReferenceDataObject::Create(m_metaObject,
		m_metaObject->GetObjectEnums()[lPropNum]->GetGuid()
	);
	return true;
}

#include "frontend/visualView/controls/frameInterface.h"

bool CEnumerationManager::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
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
	case eGetSelectForm:
	{
		CValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<CValueGuid>() : NULL;
		pvarRetValue = m_metaObject->GetSelectForm(paParams[0]->GetString(),
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