#include "visualEvent.h"

DEFINE_EVENT_TYPE(wxEVT_PROJECT_LOADED)
DEFINE_EVENT_TYPE(wxEVT_PROJECT_SAVED)
DEFINE_EVENT_TYPE(wxEVT_OBJECT_EXPANDED)
DEFINE_EVENT_TYPE(wxEVT_OBJECT_SELECTED)
DEFINE_EVENT_TYPE(wxEVT_OBJECT_CREATED)
DEFINE_EVENT_TYPE(wxEVT_OBJECT_REMOVED)
DEFINE_EVENT_TYPE(wxEVT_PROPERTY_MODIFIED)
DEFINE_EVENT_TYPE(wxEVT_PROJECT_REFRESH)
DEFINE_EVENT_TYPE(wxEVT_CODE_GENERATION)
DEFINE_EVENT_TYPE(wxEVT_EVENT_HANDLER_MODIFIED)

wxFrameEvent::wxFrameEvent(wxEventType commandType)
	:
	wxEvent(0, commandType)
{
	//ctor
}

// required for sending with wxPostEvent()
wxEvent* wxFrameEvent::Clone() const
{
	return new wxFrameEvent(*this);
}

wxFrameEvent::wxFrameEvent(const wxFrameEvent& event)
	:
	wxEvent(event),
	m_string(event.m_string)
{
}

wxFrameEvent::~wxFrameEvent()
{
	//dtor
}

#define CASE( EVENT )									\
	if ( EVENT == m_eventType )							\
	{													\
		return wxT( #EVENT );							\
	}

wxString wxFrameEvent::GetEventName()
{
	CASE(wxEVT_PROJECT_LOADED)
		CASE(wxEVT_PROJECT_SAVED)
		CASE(wxEVT_OBJECT_EXPANDED)
		CASE(wxEVT_OBJECT_SELECTED)
		CASE(wxEVT_OBJECT_CREATED)
		CASE(wxEVT_OBJECT_REMOVED)
		CASE(wxEVT_PROPERTY_MODIFIED)
		CASE(wxEVT_PROJECT_REFRESH)
		CASE(wxEVT_CODE_GENERATION)

		return wxT("Unknown Type");
}

void wxFrameEvent::SetString(const wxString& newString)
{
	m_string = newString;
}

wxString wxFrameEvent::GetString()
{
	return m_string;
}

wxFramePropertyEvent::wxFramePropertyEvent(wxEventType commandType, Property* property, const wxVariant& oldValue)
	: wxFrameEvent(commandType), m_strValue(oldValue), m_property(property)
{
}

wxFramePropertyEvent::wxFramePropertyEvent(const wxFramePropertyEvent& event)
	: wxFrameEvent(event), m_strValue(event.m_strValue), m_property(event.m_property)
{
}

wxEvent* wxFramePropertyEvent::Clone() const
{
	return new wxFramePropertyEvent(*this);
}

wxFrameObjectEvent::wxFrameObjectEvent(wxEventType commandType, IValueFrame* object)
	: wxFrameEvent(commandType), m_object(object)
{
}

wxFrameObjectEvent::wxFrameObjectEvent(const wxFrameObjectEvent& event)
	:
	wxFrameEvent(event),
	m_object(event.m_object)
{
}

wxEvent* wxFrameObjectEvent::Clone() const
{
	return new wxFrameObjectEvent(*this);
}

wxFrameEventHandlerEvent::wxFrameEventHandlerEvent(wxEventType commandType, const wxString& newValue, const wxString& prevValue, const std::vector<wxString>& args)
	: wxFrameEvent(commandType), m_args(args), m_value(newValue), m_prevValue(prevValue)
{
}

wxFrameEventHandlerEvent::wxFrameEventHandlerEvent(const wxFrameEventHandlerEvent& event)
	: wxFrameEvent(event)
{
}

wxEvent* wxFrameEventHandlerEvent::Clone() const
{
	return new wxFrameEventHandlerEvent(*this);
}
