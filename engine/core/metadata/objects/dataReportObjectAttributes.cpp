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
void CObjectReportValue::SetAttribute(attributeArg_t &aParams, CValue &vObject)        //установка атрибута
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

CValue CObjectReportValue::GetAttribute(attributeArg_t &aParams)                   //значение атрибута
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
		return this;
	}

	return CValue();
}