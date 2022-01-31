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
void CObjectCatalogValue::SetAttribute(attributeArg_t &aParams, CValue &vObject)        //установка атрибута
{
	wxString sName = GetAttributeName(aParams.GetIndex()), sSynonym = m_methods->GetAttributeSynonym(aParams.GetIndex());

	if (sSynonym == wxT("procUnit")) {
		if (m_procUnit) {
			m_procUnit->SetAttribute(aParams.GetName(), vObject);
		}
	}
	else if (sSynonym == wxT("attribute")) {
		meta_identifier_t id =
			m_methods->GetAttributePosition(aParams.GetIndex());
		SetValueByMetaID(id, vObject);
	}
}

CValue CObjectCatalogValue::GetAttribute(attributeArg_t &aParams)                   //значение атрибута
{
	wxString sName = GetAttributeName(aParams.GetIndex()), sSynonym = m_methods->GetAttributeSynonym(aParams.GetIndex());

	if (sSynonym == wxT("procUnit")) { 
		if (m_procUnit) {
			return m_procUnit->GetAttribute(aParams.GetName());
		}
	}
	else if (sSynonym == wxT("attribute") || sSynonym == wxT("table")) {
		meta_identifier_t id = m_methods->GetAttributePosition(aParams.GetIndex());
		return GetValueByMetaID(id);
	}
	else if (sSynonym == wxT("system")) {
		if (sName == wxT("reference")) {
			if (m_bNewObject) {
				return new CValueReference(m_metaObject);
			}
			else {
				return new CValueReference(m_metaObject, m_objGuid);
			}
		}
		else {
			return this;
		}
	}

	return CValue();
}
