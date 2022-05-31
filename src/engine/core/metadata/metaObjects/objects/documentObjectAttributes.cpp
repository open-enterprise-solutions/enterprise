////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document object - attributes
////////////////////////////////////////////////////////////////////////////

#include "document.h"
#include "compiler/methods.h"
#include "reference/reference.h"
#include "utils/stringUtils.h"

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************
void CObjectDocument::SetAttribute(attributeArg_t& aParams, CValue& vObject)        //установка атрибута
{
	wxString sName = GetAttributeName(aParams.GetIndex()), sSynonym = m_methods->GetAttributeSynonym(aParams.GetIndex());

	if (sSynonym == wxT("procUnit")) {
		if (m_procUnit != NULL) {
			m_procUnit->SetAttribute(aParams.GetName(), vObject);
		}
	}
	else if (sSynonym == wxT("attribute")) {
		const meta_identifier_t &id =
			m_methods->GetAttributePosition(aParams.GetIndex());
		if (!m_metaObject->IsDataReference(id)) {
			SetValueByMetaID(id, vObject);
		}
	}
}

CValue CObjectDocument::GetAttribute(attributeArg_t& aParams)                   //значение атрибута
{
	wxString sName = GetAttributeName(aParams.GetIndex()), sSynonym = m_methods->GetAttributeSynonym(aParams.GetIndex());

	if (sSynonym == wxT("procUnit")) {
		if (m_procUnit != NULL) {
			return m_procUnit->GetAttribute(aParams.GetName());
		}
	}
	else if (sSynonym == wxT("attribute") || sSynonym == wxT("table")) {
		const meta_identifier_t &id = m_methods->GetAttributePosition(aParams.GetIndex());
		if (m_metaObject->IsDataReference(id)) {
			if (m_newObject) {
				return CReferenceDataObject::Create(m_metaObject);
			}
			return CReferenceDataObject::Create(m_metaObject, m_objGuid);
		}
		return GetValueByMetaID(id);
	}
	else if (sSynonym == wxT("system")) {
		if (StringUtils::CompareString(sName, registerRecords)) {
			return m_registerRecords;
		}
		return this;
	}

	return CValue();
}

/////////////////////////////////////////////////////////////////////////////

void CObjectDocument::CRecordRegister::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
}

CValue CObjectDocument::CRecordRegister::GetAttribute(attributeArg_t& aParams)
{
	const meta_identifier_t &id = m_methods->GetAttributePosition(aParams.GetIndex());

	auto foundedIt = m_records.find(id);
	if (foundedIt != m_records.end())
		return foundedIt->second;

	return CValue();
}
