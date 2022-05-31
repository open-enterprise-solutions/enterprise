////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog object - attributes
////////////////////////////////////////////////////////////////////////////

#include "catalog.h"
#include "compiler/methods.h"
#include "reference/reference.h"
#include "utils/stringUtils.h"

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************
void CObjectCatalog::SetAttribute(attributeArg_t& aParams, CValue& vObject) //установка атрибута
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

CValue CObjectCatalog::GetAttribute(attributeArg_t& aParams) //значение атрибута
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
		return this;
	}

	return CValue();
}