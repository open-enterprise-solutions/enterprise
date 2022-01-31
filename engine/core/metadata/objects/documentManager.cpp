////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document manager
////////////////////////////////////////////////////////////////////////////

#include "documentManager.h"
#include "metadata/metadata.h"
#include "compiler/methods.h"
#include "baseObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CManagerDocumentValue, CValue);

CManagerDocumentValue::CManagerDocumentValue(CMetaObjectDocumentValue *metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods()), m_metaObject(metaObject)
{
}

CManagerDocumentValue::~CManagerDocumentValue()
{
	wxDELETE(m_methods);
}

CMetaCommonModuleObject *CManagerDocumentValue::GetModuleManager() const { return m_metaObject->GetModuleManager(); }

#include "reference/reference.h"

CValue CManagerDocumentValue::EmptyRef()
{
	return new CValueReference(m_metaObject);
}

#include "metadata/singleMetaTypes.h"

CLASS_ID CManagerDocumentValue::GetTypeID() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeID();
}

wxString CManagerDocumentValue::GetTypeString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CManagerDocumentValue::GetString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}