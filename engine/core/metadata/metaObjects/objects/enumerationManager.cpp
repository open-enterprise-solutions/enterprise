////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enumeration manager
////////////////////////////////////////////////////////////////////////////

#include "enumerationManager.h"
#include "metadata/metadata.h"
#include "compiler/methods.h"
#include "baseObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CEnumerationManager, CValue);

CEnumerationManager::CEnumerationManager(CMetaObjectEnumeration *metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods()), m_metaObject(metaObject)
{
}

CEnumerationManager::~CEnumerationManager()
{
	wxDELETE(m_methods);
}

CMetaCommonModuleObject *CEnumerationManager::GetModuleManager() const { return m_metaObject->GetModuleManager(); }

#include "metadata/singleMetaTypes.h"

CLASS_ID CEnumerationManager::GetClassType() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CEnumerationManager::GetTypeString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CEnumerationManager::GetString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}