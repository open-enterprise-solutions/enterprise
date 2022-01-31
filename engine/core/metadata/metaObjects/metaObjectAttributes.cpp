////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject attributes
////////////////////////////////////////////////////////////////////////////

#include "metaObject.h"

//*******************************************************************
//*                          Attributes                            *
//*******************************************************************

void IMetaObject::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
	Property *m_prop = GetPropertyByIndex(aParams.GetIndex());
	if (m_prop) SetPropertyData(m_prop, cVal);

	SaveProperty();
}

CValue IMetaObject::GetAttribute(attributeArg_t &aParams)
{
	Property *m_prop = GetPropertyByIndex(aParams.GetIndex());
	if (m_prop) return GetPropertyData(m_prop);

	return CValue();
}

int IMetaObject::FindAttribute(const wxString &sName) const
{
	//return GetPropertyIndex(sName) + 1;
	return CValue::FindAttribute(sName);
}
