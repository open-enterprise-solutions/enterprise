////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections - attributes
////////////////////////////////////////////////////////////////////////////

#include "baseObject.h"
#include "compiler/methods.h"

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************

void CRecordKeyObject::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
}

CValue CRecordKeyObject::GetAttribute(attributeArg_t& aParams)
{
	return CValue();
}

////////////////////////////////////////////////////////////////////////////

void IRecordSetObject::CRecordSetRegisterReturnLine::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
	const meta_identifier_t& id = m_methods->GetAttributePosition(aParams.GetIndex());

	if (id != wxNOT_FOUND) {
		SetValueByMetaID(id, cVal);
	}
}

CValue IRecordSetObject::CRecordSetRegisterReturnLine::GetAttribute(attributeArg_t& aParams)
{
	const meta_identifier_t& id = m_methods->GetAttributePosition(aParams.GetIndex());

	if (id != wxNOT_FOUND) {
		return GetValueByMetaID(id);
	}

	return CValue();
}

////////////////////////////////////////////////////////////////////////////

void IRecordSetObject::CRecordSetRegisterKeyValue::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
}

CValue IRecordSetObject::CRecordSetRegisterKeyValue::GetAttribute(attributeArg_t& aParams)
{
	const meta_identifier_t& id = m_methods->GetAttributePosition(aParams.GetIndex());

	if (id != wxNOT_FOUND) {
		return new CRecordSetRegisterKeyDescriptionValue(m_recordSet, id);
	}

	return CValue();
}

////////////////////////////////////////////////////////////////////////////

enum {
	eValue,
	eUse
};

void IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
	IMetaObjectRegisterData* metaObject = m_recordSet->GetMetaObject();
	wxASSERT(metaObject);
	IMetaAttributeObject* metaAttribute = metaObject->FindGenericAttribute(m_metaId);
	wxASSERT(metaAttribute);

	switch (aParams.GetIndex()) {
	case eValue:
		m_recordSet->SetKeyValue(m_metaId, cVal);
		break;
	case eUse:
		if (cVal.GetBoolean())
			m_recordSet->SetKeyValue(m_metaId, metaAttribute->CreateValue());
		else
			m_recordSet->EraseKeyValue(m_metaId);
		break;
	}
}

CValue IRecordSetObject::CRecordSetRegisterKeyValue::CRecordSetRegisterKeyDescriptionValue::GetAttribute(attributeArg_t& aParams)
{
	IMetaObjectRegisterData* metaObject = m_recordSet->GetMetaObject();
	wxASSERT(metaObject);
	IMetaAttributeObject* metaAttribute = metaObject->FindGenericAttribute(m_metaId);
	wxASSERT(metaAttribute);

	switch (aParams.GetIndex()) {
	case eValue:
		if (m_recordSet->FindKeyValue(m_metaId))
			return m_recordSet->GetKeyValue(m_metaId);
		return metaAttribute->CreateValue();
	case eUse:
		return m_recordSet->FindKeyValue(m_metaId);
	}

	return CValue();
}