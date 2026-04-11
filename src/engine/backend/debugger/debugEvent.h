#ifndef _DEBUG_EVENT_H__
#define _DEBUG_EVENT_H__

#include <wx/event.h>
#include "debugDefs.h"

class wxDebugEvent : public wxEvent
{
	EventId m_eventId;
public:
	wxDebugEvent(EventId eventId = EventId_SessionStart, int winid = 0)
		: wxEvent(winid), m_eventId(eventId) {}

	EventId GetEventId() const { return m_eventId; }
	void SetEventId(EventId eventId) { m_eventId = eventId; }

	virtual wxEvent* Clone() const override { return new wxDebugEvent(*this); }
};

#endif
