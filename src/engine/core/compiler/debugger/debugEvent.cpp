////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debugger - events 
////////////////////////////////////////////////////////////////////////////

#include "debugEvent.h"

DEFINE_EVENT_TYPE(wxEVT_DEBUG_EVENT)
DEFINE_EVENT_TYPE(wxEVT_DEBUG_TOOLTIP_EVENT)
DEFINE_EVENT_TYPE(wxEVT_DEBUG_AUTOCOMPLETE_EVENT)
DEFINE_EVENT_TYPE(wxEVT_DEBUGGER_FOUND)

wxDebugEvent::wxDebugEvent(EventId eventId, wxSocketBase *connection, wxEventType commandType)
    : wxEvent(0, commandType), m_socket(connection)
{
    m_eventId       = eventId;
    m_scriptIndex   = -1;
    m_line          = 0;
    m_enabled       = false;
    m_messageType   = MessageType_Normal;
}

EventId wxDebugEvent::GetEventId() const
{
    return m_eventId;
}

void wxDebugEvent::SetScriptIndex(unsigned int scriptIndex)
{
    m_scriptIndex = scriptIndex;
}

unsigned int wxDebugEvent::GetScriptIndex() const
{
    return m_scriptIndex;
}

unsigned int wxDebugEvent::GetLine() const
{
    return m_line;
}

void wxDebugEvent::SetLine(unsigned int line)
{
    m_line = line;
}

bool wxDebugEvent::GetEnabled() const
{
    return m_enabled;
}

void wxDebugEvent::SetEnabled(bool enabled)
{
    m_enabled = enabled;
}

const wxString& wxDebugEvent::GetMessage() const
{
    return m_message;
}

void wxDebugEvent::SetMessage(const wxString& message)
{
    m_message = message;
}

const wxString& wxDebugEvent::GetModuleName() const
{
	return m_moduleName;
}

void wxDebugEvent::SetModuleName(const wxString& moduleName)
{
	m_moduleName = moduleName;
}

const wxString& wxDebugEvent::GetFileName() const
{
	return m_fileName;
}

void wxDebugEvent::SetFileName(const wxString& fileName)
{
	m_fileName = fileName;
}

MessageType wxDebugEvent::GetMessageType() const
{
    return m_messageType;
}

void wxDebugEvent::SetMessageType(MessageType messageType)
{
    m_messageType = messageType;
}

wxEvent* wxDebugEvent::Clone() const
{
    return new wxDebugEvent(*this);
}

wxEvent * wxDebuggerEvent::Clone() const
{
	return new wxDebuggerEvent(*this);
}
