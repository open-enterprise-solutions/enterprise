////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog manager
////////////////////////////////////////////////////////////////////////////

#include "catalogManager.h"
#include "core/metadata/metadata.h"

#include "object.h"

wxIMPLEMENT_DYNAMIC_CLASS(CCatalogManager, CValue);

CCatalogManager::CCatalogManager(CMetaObjectCatalog* metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methodHelper(new CMethodHelper()), m_metaObject(metaObject)
{
}

CCatalogManager::~CCatalogManager()
{
	wxDELETE(m_methodHelper);
}

CMetaCommonModuleObject* CCatalogManager::GetModuleManager() const { return m_metaObject->GetModuleManager(); }

#include "reference/reference.h"

CReferenceDataObject* CCatalogManager::EmptyRef()
{
	return CReferenceDataObject::Create(m_metaObject);
}

#include "core/metadata/singleClass.h"

CLASS_ID CCatalogManager::GetTypeClass() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
}

wxString CCatalogManager::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CCatalogManager::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

enum Func {
	eCreateElement = 0,
	eCreateGroup,
	eSelect,
	eFindByCode,
	eFindByName,
	eGetForm,
	eGetListForm,
	eGetSelectForm,
	eEmptyRef
};

#include "core/metadata/metadata.h"

void CCatalogManager::PrepareNames() const
{
	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	m_methodHelper->ClearHelper();
	m_methodHelper->AppendFunc("createElement", "createElement()");
	m_methodHelper->AppendFunc("createGroup", "createGroup()");
	m_methodHelper->AppendFunc("select", "select()");
	m_methodHelper->AppendFunc("findByCode", 1, "findByCode(string)");
	m_methodHelper->AppendFunc("findByName", 1, "findByName(string)");
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

bool CCatalogManager::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eCreateElement:
		pvarRetValue = m_metaObject->CreateObjectValue(eObjectMode::OBJECT_ITEM);
		return true;
	case eCreateGroup:
		pvarRetValue = m_metaObject->CreateObjectValue(eObjectMode::OBJECT_FOLDER);
		return true;
	case eSelect:
		pvarRetValue = new CSelectorDataObject(m_metaObject);
		return true;
	case eFindByCode:
		pvarRetValue = FindByCode(*paParams[0]);
		return true;
	case eFindByName:
		pvarRetValue = FindByName(*paParams[0]);
		return true;
	case eGetForm: {
		CValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<CValueGuid>() : NULL;
		pvarRetValue = m_metaObject->GetGenericForm(lSizeArray > 0 ? paParams[0]->GetString() : wxEmptyString,
			lSizeArray > 1 ? paParams[1]->ConvertToType<IValueFrame>() : NULL,
			guidVal ? ((Guid)*guidVal) : Guid());
		return true;
	}
	case eGetListForm: {
		CValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<CValueGuid>() : NULL;
		pvarRetValue = m_metaObject->GetListForm(lSizeArray > 0 ? paParams[0]->GetString() : wxEmptyString,
			lSizeArray > 1 ? paParams[1]->ConvertToType<IValueFrame>() : NULL,
			guidVal ? ((Guid)*guidVal) : Guid());
		return true;
	}
	case eGetSelectForm: {
		CValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<CValueGuid>() : NULL;
		pvarRetValue = m_metaObject->GetSelectForm(lSizeArray > 0 ? paParams[0]->GetString() : wxEmptyString,
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

	CValue* pRefData =
		moduleManager->FindCommonModule(m_metaObject->GetModuleManager());
	if (pRefData != NULL)
		return pRefData->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	return false;
}