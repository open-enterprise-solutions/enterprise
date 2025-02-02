////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value event 
////////////////////////////////////////////////////////////////////////////

#include "valueEvent.h"


//////////////////////////////////////////////////////////////////////
wxIMPLEMENT_DYNAMIC_CLASS(CValueEvent, CValue);

CValueEvent::CValueEvent() :
	CValue(eValueTypes::TYPE_VALUE), m_eventName(wxEmptyString)
{
}

CValueEvent::CValueEvent(const wxString& eventName) :
	CValue(eValueTypes::TYPE_VALUE), m_eventName(eventName)
{
}

bool CValueEvent::Init(CValue** paParams, const long lSizeArray)
{
	m_eventName = paParams[0]->GetString();
	return true;
}

CValueEvent::~CValueEvent()
{
}

wxString CValueEvent::GetClassName()const
{
	return wxT("event");
}

wxString CValueEvent::GetString() const
{
	return m_eventName;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(CValueEvent, "event", string_to_clsid("VL_EVEN"));