////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference - attributes
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "../baseObject.h"
#include "compiler/methods.h"
#include "metadata/objects/tabularSection/tabularSection.h"
#include "utils/stringUtils.h"

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************

void CValueReference::SetAttribute(attributeArg_t &aParams, CValue &value) 
{
}

CValue CValueReference::GetAttribute(attributeArg_t &aParams)
{
	wxString sSynonym = m_methods->GetAttributeSynonym(aParams.GetIndex());
	meta_identifier_t id = m_methods->GetAttributePosition(aParams.GetIndex());

	if (IsEmpty()) {
		if (sSynonym != wxT("reference")) {
			return GetValueByMetaID(id);
		}
		else {
			return new CValueReference(m_metaObject);
		}
	}

	if (sSynonym == wxT("reference")) {
		return new CValueReference(m_metaObject, m_objGuid);
	}
	else {
		return m_aObjectValues[id];
	}
}