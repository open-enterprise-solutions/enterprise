#ifndef __VALUE_EVENT_H__
#define __VALUE_EVENT_H__

#include "backend/compiler/value.h"

//event support
class BACKEND_API ibValueEvent : public ibValue {
	public:

	ibValueEvent();
	ibValueEvent(const wxString &eventName);

	virtual bool IsEmpty() const { return m_eventName.IsEmpty(); }

	virtual bool Init(ibValue **paParams, const long lSizeArray);
	virtual wxString GetString() const{ return m_eventName; }

protected:
	wxString m_eventName;
};

class BACKEND_API ibValueActionEvent : public ibValueEvent {
	public:
	ibValueActionEvent();
	ibValueActionEvent(const wxString& eventName, ibActionID eventId);
private:
	ibActionID m_eventId;
};
#endif