////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value guid
////////////////////////////////////////////////////////////////////////////

#include "valueGuid.h"
#include "compiler/methods.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueGuid, CValue);

CValueGuid::CValueGuid() : CValue(eValueTypes::TYPE_VALUE, true), m_guid() {}

CValueGuid::CValueGuid(const Guid &guid) : CValue(eValueTypes::TYPE_VALUE, true), m_guid(guid) {}

bool CValueGuid::Init()
{
	m_guid = wxNewGuid;
	return true;
}

bool CValueGuid::Init(CValue **aParams)
{
	if (aParams[0]->GetType() == eValueTypes::TYPE_STRING)
	{
		Guid m_newGuid = aParams[0]->ToString();
		if (m_newGuid.isValid()) m_guid = m_newGuid;	
		return m_newGuid.isValid();
	}

	return false;
}

CValueGuid::~CValueGuid() {}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueGuid, "guid", TEXT2CLSID("VL_GUID"));