////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog manager
////////////////////////////////////////////////////////////////////////////

#include "catalogManager.h"
#include "metadata/metadata.h"
#include "compiler/methods.h"
#include "baseObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CManagerCatalogValue, CValue);

CManagerCatalogValue::CManagerCatalogValue(CMetaObjectCatalogValue *metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods()), m_metaObject(metaObject)
{
}

CManagerCatalogValue::~CManagerCatalogValue()
{
	wxDELETE(m_methods);
}

CMetaCommonModuleObject *CManagerCatalogValue::GetModuleManager() const { return m_metaObject->GetModuleManager(); }

#include "reference/reference.h"

CValue CManagerCatalogValue::EmptyRef()
{
	return new CValueReference(m_metaObject);
}

#include "metadata/singleMetaTypes.h"

CLASS_ID CManagerCatalogValue::GetTypeID() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeID();
}

wxString CManagerCatalogValue::GetTypeString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CManagerCatalogValue::GetString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}