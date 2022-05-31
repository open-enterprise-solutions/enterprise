////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : informationRegister object - attributes
////////////////////////////////////////////////////////////////////////////

#include "informationRegister.h"
#include "compiler/methods.h"
#include "reference/reference.h"
#include "utils/stringUtils.h"

enum {
	enThisObject,
	enFilter
};

void CRecordManagerInformationRegister::SetAttribute(attributeArg_t &aParams, CValue &vObject)        //установка атрибута
{
	wxString sName = GetAttributeName(aParams.GetIndex()), sSynonym = m_methods->GetAttributeSynonym(aParams.GetIndex());

	if (sSynonym == wxT("attribute")) {
		const meta_identifier_t &id = m_methods->GetAttributePosition(aParams.GetIndex());
		SetValueByMetaID(id, vObject);
	}
}

CValue CRecordManagerInformationRegister::GetAttribute(attributeArg_t &aParams)                   //значение атрибута
{
	wxString sName = GetAttributeName(aParams.GetIndex()), sSynonym = m_methods->GetAttributeSynonym(aParams.GetIndex());

	if (sSynonym == wxT("attribute")) {
		const meta_identifier_t &id = m_methods->GetAttributePosition(aParams.GetIndex());
		return GetValueByMetaID(id);
	}

	return CValue();
}

//////////////////////////////////////////////////////////////////////////

void CRecordSetInformationRegister::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
}

CValue CRecordSetInformationRegister::GetAttribute(attributeArg_t& aParams)
{
	switch (aParams.GetIndex())
	{
	case enThisObject:
		return this;
	case enFilter:
		return m_recordSetKeyValue;
	}

	return CValue();
}
