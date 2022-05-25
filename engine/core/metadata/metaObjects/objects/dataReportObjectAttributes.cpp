////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report - attributes
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"
#include "compiler/methods.h"
#include "reference/reference.h"
#include "utils/stringUtils.h"

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************
void CObjectReport::SetAttribute(attributeArg_t &aParams, CValue &vObject)        //установка атрибута
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
		SetValueByMetaID(id, vObject);
	}
}

CValue CObjectReport::GetAttribute(attributeArg_t &aParams)                   //значение атрибута
{
	wxString sName = GetAttributeName(aParams.GetIndex()), sSynonym = m_methods->GetAttributeSynonym(aParams.GetIndex());

	if (sSynonym == wxT("procUnit")) {
		if (m_procUnit != NULL) {
			return m_procUnit->GetAttribute(aParams.GetName());
		}
	}
	else if (sSynonym == wxT("attribute") || sSynonym == wxT("table")) {
		const meta_identifier_t &id = m_methods->GetAttributePosition(aParams.GetIndex());
		return GetValueByMetaID(id);
	}
	else if (sSynonym == wxT("system")) {
		return this;
	}

	return CValue();
}