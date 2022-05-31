////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants manager
////////////////////////////////////////////////////////////////////////////

#include "constantManager.h"

wxIMPLEMENT_DYNAMIC_CLASS(CConstantManager, CValue);

CConstantManager::CConstantManager(CMetaConstantObject *metaConst) :
	CValue(eValueTypes::TYPE_VALUE, true), m_metaConst(metaConst)
{
}

CConstantManager::~CConstantManager()
{
}

#include "metadata/metadata.h"
#include "metadata/singleMetaTypes.h"

CLASS_ID CConstantManager::GetClassType() const
{
	IMetadata *metaData = m_metaConst->GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle *clsFactory =
		metaData->GetTypeObject(m_metaConst, eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CConstantManager::GetTypeString() const
{
	IMetadata *metaData = m_metaConst->GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle *clsFactory =
		metaData->GetTypeObject(m_metaConst, eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CConstantManager::GetString() const
{
	IMetadata *metaData = m_metaConst->GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle *clsFactory =
		metaData->GetTypeObject(m_metaConst, eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}