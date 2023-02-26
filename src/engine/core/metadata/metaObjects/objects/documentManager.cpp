////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document manager
////////////////////////////////////////////////////////////////////////////

#include "documentManager.h"
#include "core/metadata/metadata.h"

#include "object.h"

wxIMPLEMENT_DYNAMIC_CLASS(CDocumentManager, CValue);

CDocumentManager::CDocumentManager(CMetaObjectDocument* metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methodHelper(new CMethodHelper()), m_metaObject(metaObject) {
}

CDocumentManager::~CDocumentManager() {
	wxDELETE(m_methodHelper);
}

CMetaCommonModuleObject* CDocumentManager::GetModuleManager() const {
	return m_metaObject->GetModuleManager();
}

#include "reference/reference.h"

CReferenceDataObject* CDocumentManager::EmptyRef() {
	return CReferenceDataObject::Create(m_metaObject);
}

#include "core/metadata/singleClass.h"

CLASS_ID CDocumentManager::GetTypeClass() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
}

wxString CDocumentManager::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CDocumentManager::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

enum Func {
	eCreateElement = 0,
	eSelect,
	eFindByNumber,
	eGetForm,
	eGetListForm,
	eGetSelectForm,
	eEmptyRef
};

#include "core/metadata/metadata.h"

void CDocumentManager::PrepareNames() const
{
	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	m_methodHelper->ClearHelper();
	m_methodHelper->AppendFunc("createElement", "createElement()");
	m_methodHelper->AppendFunc("select", "select()");
	m_methodHelper->AppendFunc("findByNumber", 2, "findByNumber(string, date)");
	m_methodHelper->AppendFunc("getForm", 3, "getForm(string, owner, guid)");
	m_methodHelper->AppendFunc("getListForm", 3, "getListForm(string, owner, guid)");
	m_methodHelper->AppendFunc("getSelectForm", 3, "getSelectForm(string, owner, guid)");
	m_methodHelper->AppendFunc("emptyRef", "emptyRef()");

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

bool CDocumentManager::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eCreateElement:
		pvarRetValue = m_metaObject->CreateObjectValue();
		return true;
	case eSelect:
		pvarRetValue = new CSelectorDataObject(m_metaObject);
		return true;
	case eFindByNumber:
	{
		pvarRetValue = FindByNumber(*paParams[0],
			lSizeArray > 1 ? *paParams[1] : CValue()
		);
		return true;
	}
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
	case eEmptyRef:
		pvarRetValue = EmptyRef();
		return true;
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