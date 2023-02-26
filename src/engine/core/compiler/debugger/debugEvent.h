#ifndef DEBUG_EVENT_H
#define DEBUG_EVENT_H

#include <wx/event.h>
#include <wx/socket.h>
#include <wx/buffer.h>

#include "debugDefs.h"

//
// Event definitions.
//

BEGIN_DECLARE_EVENT_TYPES()
DECLARE_LOCAL_EVENT_TYPE(wxEVT_DEBUG_EVENT, -1)
DECLARE_LOCAL_EVENT_TYPE(wxEVT_DEBUGGER_FOUND, -1)
END_DECLARE_EVENT_TYPES()

/**
 * Event class used to pass information from the debug server to the
 * wxWidget UI.
 */
	class wxDebugEvent : public wxEvent
{

public:

	/**
	 * Constructor.
	 */
	wxDebugEvent(EventId eventId, wxSocketBase *connection, wxEventType commandType = wxEVT_DEBUG_EVENT);

	/**
	 * Returns the event id of the event.
	 */
	EventId GetEventId() const;

	/**
	 * Returns the index of the script the event relates to.
	 */
	unsigned int GetScriptIndex() const;

	/**
	 * Sets the index of the script the event relates to.
	 */
	void SetScriptIndex(unsigned int scriptIndex);

	/**
	 * Returns the number of the line in the script the event relates to.
	 */
	unsigned int GetLine() const;

	/**
	 * Sets the number of the line in the script the event relates to.
	 */
	void SetLine(unsigned int scriptIndex);

	/**
	 * Gets the boolean value for the event. This is a generic value that's
	 * meaning depends on the event.
	 */
	bool GetEnabled() const;

	/**
	 * Sets the boolean value for the event. This is a generic value that's
	 * meaning depends on the event.
	 */
	void SetEnabled(bool enabled);

	/**
	 * Returns the message associated with the event. Not all events will have
	 * messages.
	 */
	const wxString& GetMessage() const;

	/**
	 * Sets the message associated with the event.
	 */
	void SetMessage(const wxString& message);

	/**
	* Returns the message associated with the event. Not all events will have
	* messages.
	*/
	const wxString& GetModuleName() const;

	/**
	 * Sets the message associated with the event.
	 */
	void SetModuleName(const wxString& moduleName);

	/**
	* Returns the message associated with the event. Not all events will have
	* messages.
	*/
	const wxString& GetFileName() const;

	/**
	 * Sets the message associated with the event.
	 */
	void SetFileName(const wxString& fileName);

	/**
	 * Returns the type of the string message (error, warning, etc.) This is only
	 * relevant when the event deals with a message.
	 */
	MessageType GetMessageType() const;

	/**
	 * Sets the type of the string message. This is only relevant when the event
	 * deals with a message.
	 */
	void SetMessageType(MessageType messageType);

	/**
	 * From wxEvent.
	 */
	virtual wxEvent* Clone() const;

private:

	wxSocketBase*   m_socket;
	EventId         m_eventId;

	unsigned int    m_scriptIndex;
	unsigned int    m_line;

	bool            m_enabled;

	wxString        m_message;
	MessageType     m_messageType;

	wxString        m_moduleName;
	wxString        m_fileName;
};

class wxDebuggerEvent : public wxEvent 
{
public:
	wxDebuggerEvent(wxEventType commandType) : wxEvent(0, commandType) {}

	void SetHostName(const wxString &hostName) { m_hostName = hostName; }
	wxString GetHostName() const { return m_hostName; }
	void SetPort(unsigned short port) { m_port = port; }
	unsigned short GetPort() const { return m_port; }
	void SetConfGuid(const wxString &confGuid) { m_confGuid = confGuid; }
	wxString GetConfGuid() const { return m_confGuid; }
	void SetMD5(const wxString &md5Hash) { m_md5Hash = md5Hash; }
	wxString GetMD5() const { return m_md5Hash; }
	void SetComputerName(const wxString &compName) { m_compName = compName; }
	wxString GetComputerName() const { return m_compName; }
	void SetUserName(const wxString &userName) { m_userName = userName; }
	wxString GetUserName() const { return m_userName; }

	/**
	* From wxEvent.
	*/
	virtual wxEvent* Clone() const;

private:
	wxString m_hostName;
	unsigned short m_port;

	wxString m_confGuid;
	wxString m_md5Hash;
	wxString m_userName;
	wxString m_compName;
};

typedef void (wxEvtHandler::*wxDebugEventFunction)(wxDebugEvent&);
typedef void (wxEvtHandler::*wxDebuggerEventFunction)(wxDebuggerEvent&);

#define wxDebugEventHandler(func) \
  wxEVENT_HANDLER_CAST(wxDebugEventFunction, func)

#define wxDebuggerEventHandler(func) \
  wxEVENT_HANDLER_CAST(wxDebuggerEventFunction, func)

#define EVT_DEBUG(fn) \
    DECLARE_EVENT_TABLE_ENTRY( wxEVT_DEBUG_EVENT, 0, -1, \
    (wxObjectEventFunction) (wxEventFunction) wxStaticCastEvent( wxDebugEventFunction, &fn ), (wxObject *) NULL ),

#define EVT_DEBUGGER_FOUND(fn) \
    DECLARE_EVENT_TABLE_ENTRY( wxEVT_DEBUGGER_FOUND, 0, -1, \
    (wxObjectEventFunction) (wxEventFunction) wxStaticCastEvent( wxDebuggerEventFunction, &fn ), (wxObject *) NULL ),

#endif