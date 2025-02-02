////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value guid
////////////////////////////////////////////////////////////////////////////

#include "valueGuid.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueGuid, CValue);

CValueGuid::CValueGuid() : CValue(eValueTypes::TYPE_VALUE, true), m_guid() {}

CValueGuid::CValueGuid(const Guid& guid) : CValue(eValueTypes::TYPE_VALUE, true), m_guid(guid) {}

bool CValueGuid::Init()
{
	m_guid = wxNewUniqueGuid;
	return true;
}

bool CValueGuid::Init(CValue** paParams, const long lSizeArray)
{
	if (paParams[0]->GetType() == eValueTypes::TYPE_STRING) {
		const Guid& newGuid = paParams[0]->GetString();
		if (newGuid.isValid())
			m_guid = newGuid;
		return newGuid.isValid();
	}
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(CValueGuid, "guid", string_to_clsid("VL_GUID"));