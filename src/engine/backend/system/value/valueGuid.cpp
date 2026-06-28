////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value guid
////////////////////////////////////////////////////////////////////////////

#include "valueGuid.h"


ibValueGuid::ibValueGuid() : ibValue(ibValueTypes::TYPE_VALUE, true), m_guid() {}

ibValueGuid::ibValueGuid(const ibGuid& guid) : ibValue(ibValueTypes::TYPE_VALUE, true), m_guid(guid) {}

bool ibValueGuid::Init()
{
	m_guid = wxNewUniqueGuid;
	return true;
}

bool ibValueGuid::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;

	if (paParams[0]->GetType() == ibValueTypes::TYPE_STRING) {
		const ibGuid& newGuid = paParams[0]->GetString();
		if (newGuid.isValid())
			m_guid = newGuid;
		return newGuid.isValid();
	}
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueGuid, "Guid", value_to_clsid("VL_GUID"));
