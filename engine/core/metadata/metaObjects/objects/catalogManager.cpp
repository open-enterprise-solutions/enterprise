////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog manager
////////////////////////////////////////////////////////////////////////////

#include "catalogManager.h"
#include "metadata/metadata.h"
#include "compiler/methods.h"
#include "baseObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CCatalogManager, CValue);

CCatalogManager::CCatalogManager(CMetaObjectCatalog *metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods()), m_metaObject(metaObject)
{
}

CCatalogManager::~CCatalogManager()
{
	wxDELETE(m_methods);
}

CMetaCommonModuleObject *CCatalogManager::GetModuleManager() const { return m_metaObject->GetModuleManager(); }

#include "reference/reference.h"

CReferenceDataObject * CCatalogManager::EmptyRef()
{
	return CReferenceDataObject::Create(m_metaObject);
}

#include "metadata/singleMetaTypes.h"

CLASS_ID CCatalogManager::GetClassType() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CCatalogManager::GetTypeString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CCatalogManager::GetString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}