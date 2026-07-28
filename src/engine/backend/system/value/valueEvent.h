#ifndef __VALUE_EVENT_H__
#define __VALUE_EVENT_H__

#include "backend/compiler/value.h"
#include "backend/eventDispatcher.h"   // ibValueEvent IS-A dispatcher — it dispatches by calling its named procedure

//event support — a runtime event value that is SIMULTANEOUSLY its own dispatcher (the classic "по старинке" call).
class BACKEND_API ibValueEvent : public ibValue, public ibEventDispatcher {
	public:

	ibValueEvent();
	ibValueEvent(const wxString &eventName);

	virtual bool IsEmpty() const override { return m_eventName.IsEmpty(); }   // serves both ibValue and ibEventDispatcher

	virtual bool Init(ibValue **paParams, const long lSizeArray);
	virtual wxString GetString() const{ return m_eventName; }

	// ibEventDispatcher — run the named form procedure through the form's runtime (trailing cancel by ref).
	virtual bool Dispatch(ibProcUnit* runtime, ibValue** args, long argc, ibValue& outCancel) override;

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