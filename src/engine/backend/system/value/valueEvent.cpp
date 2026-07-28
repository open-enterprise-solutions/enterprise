////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value event 
////////////////////////////////////////////////////////////////////////////

#include "valueEvent.h"

#include "backend/compiler/procUnit.h"   // ibProcUnit::CallAsProc (the named dispatch)
#include <vector>

//////////////////////////////////////////////////////////////////////

// CLASSIC dispatch — run the named form-module procedure through the form's runtime. The cancel flag rides as the
// TRAILING parameter, by reference (the procedure may set it to stop the default action), exactly as the old
// CallAsEvent contract built CallAsProc(name, args..., cancel).
bool ibValueEvent::Dispatch(ibProcUnit* runtime, ibValue** args, long argc, ibValue& outCancel)
{
	if (runtime == nullptr || m_eventName.IsEmpty())
		return true;   // nothing bound -> no-op, the event just proceeds

	std::vector<ibValue*> params(args, args + argc);
	params.push_back(&outCancel);
	runtime->CallAsProc(m_eventName, params.data(), (long)params.size());
	return outCancel.GetBoolean();
}

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
