////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document manager
////////////////////////////////////////////////////////////////////////////

#include "documentManager.h"
#include "metadata/metadata.h"
#include "compiler/methods.h"
#include "object.h"

wxIMPLEMENT_DYNAMIC_CLASS(CDocumentManager, CValue);

CDocumentManager::CDocumentManager(CMetaObjectDocument* metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods()), m_metaObject(metaObject)
{
}

CDocumentManager::~CDocumentManager()
{
	wxDELETE(m_methods);
}

CMetaCommonModuleObject* CDocumentManager::GetModuleManager() const { return m_metaObject->GetModuleManager(); }

#include "reference/reference.h"

CReferenceDataObject* CDocumentManager::EmptyRef()
{
	return CReferenceDataObject::Create(m_metaObject);
}

#include "metadata/singleMetaTypes.h"

CLASS_ID CDocumentManager::GetClassType() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
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