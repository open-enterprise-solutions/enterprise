////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference - attributes
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "../baseObject.h"
#include "compiler/methods.h"
#include "metadata/metaObjects/objects/tabularSection/tabularSection.h"
#include "utils/stringUtils.h"

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************

void CReferenceDataObject::SetAttribute(attributeArg_t& aParams, CValue& value)
{
}

CValue CReferenceDataObject::GetAttribute(attributeArg_t& aParams)
{
	const meta_identifier_t &id = m_methods->GetAttributePosition(aParams.GetIndex());

	CReferenceDataObject::PrepareRef();

	if (!m_metaObject->IsDataReference(id))
		return GetValueByMetaID(id);

	if (!CReferenceDataObject::IsEmpty())
		return CReferenceDataObject::Create(m_metaObject, m_objGuid);

	return CReferenceDataObject::Create(m_metaObject);
}