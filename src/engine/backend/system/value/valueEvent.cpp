////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value event 
////////////////////////////////////////////////////////////////////////////

#include "valueEvent.h"

//////////////////////////////////////////////////////////////////////

ibValueEvent::ibValueEvent() :
	ibValue(ibValueTypes::TYPE_VALUE), m_eventName(wxEmptyString)
{
}

ibValueEvent::ibValueEvent(const wxString& eventName) :
	ibValue(ibValueTypes::TYPE_VALUE), m_eventName(eventName)
{
}

bool ibValueEvent::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;
	m_eventName = paParams[0]->GetString();
	return true;
}

ibValueActionEvent::ibValueActionEvent()
	: ibValueEvent()
{
}

ibValueActionEvent::ibValueActionEvent(const wxString& eventName, ibActionID eventId)
	: ibValueEvent(eventName), m_eventId(eventId)
{
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueEvent, "Event", value_to_clsid("SY_EVENT"));
SYSTEM_TYPE_REGISTER(ibValueActionEvent, "ActionEvent", system_to_clsid("SY_ATEVT"));
