////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject attributes
////////////////////////////////////////////////////////////////////////////

#include "metaObject.h"

//*******************************************************************
//*                          Attributes                            *
//*******************************************************************

void IMetaObject::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
	Property* property = GetPropertyByIndex(aParams.GetIndex());
	if (property != NULL)
		property->SetDataValue(cVal);
}

CValue IMetaObject::GetAttribute(attributeArg_t& aParams)
{
	Property* property = GetPropertyByIndex(aParams.GetIndex());
	if (property != NULL)
		return property->GetDataValue();
	return CValue();
}

int IMetaObject::FindAttribute(const wxString& nameAttribute) const
{
	return CValue::FindAttribute(nameAttribute);
}
