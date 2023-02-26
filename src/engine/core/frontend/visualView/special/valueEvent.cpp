////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value event 
////////////////////////////////////////////////////////////////////////////

#include "valueEvent.h"


#include "frontend/mainFrame.h"
#include <3rdparty/databaseLayer/databaseLayer.h>

#include "utils/stringUtils.h"
#include "utils/typeconv.h"

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

wxString CValueEvent::GetTypeString()const
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

VALUE_REGISTER(CValueEvent, "event", TEXT2CLSID("VL_EVEN"));