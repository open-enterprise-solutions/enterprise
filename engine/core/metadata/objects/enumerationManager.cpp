////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enumeration manager
////////////////////////////////////////////////////////////////////////////

#include "enumerationManager.h"
#include "metadata/metadata.h"
#include "compiler/methods.h"
#include "baseObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CManagerEnumerationValue, CValue);

CManagerEnumerationValue::CManagerEnumerationValue(CMetaObjectEnumerationValue *metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods()), m_metaObject(metaObject)
{
}

CManagerEnumerationValue::~CManagerEnumerationValue()
{
	wxDELETE(m_methods);
}

CMetaCommonModuleObject *CManagerEnumerationValue::GetModuleManager() const { return m_metaObject->GetModuleManager(); }

#include "metadata/singleMetaTypes.h"

CLASS_ID CManagerEnumerationValue::GetTypeID() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeID();
}

wxString CManagerEnumerationValue::GetTypeString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CManagerEnumerationValue::GetString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}