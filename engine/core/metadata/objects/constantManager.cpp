////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants manager
////////////////////////////////////////////////////////////////////////////

#include "constantManager.h"

wxIMPLEMENT_DYNAMIC_CLASS(CManagerConstantValue, CValue);

CManagerConstantValue::CManagerConstantValue(CMetaConstantObject *metaConst) :
	CValue(eValueTypes::TYPE_VALUE, true), m_metaConst(metaConst)
{
}

CManagerConstantValue::~CManagerConstantValue()
{
}

#include "metadata/metadata.h"
#include "metadata/singleMetaTypes.h"

CLASS_ID CManagerConstantValue::GetTypeID() const
{
	IMetadata *metaData = m_metaConst->GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle *clsFactory =
		metaData->GetTypeObject(m_metaConst, eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeID();
}

wxString CManagerConstantValue::GetTypeString() const
{
	IMetadata *metaData = m_metaConst->GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle *clsFactory =
		metaData->GetTypeObject(m_metaConst, eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CManagerConstantValue::GetString() const
{
	IMetadata *metaData = m_metaConst->GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle *clsFactory =
		metaData->GetTypeObject(m_metaConst, eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}