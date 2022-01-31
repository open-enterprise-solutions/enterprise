#ifndef _WATCHWINDOWEVENTS_H__
#define _WATCHWINDOWEVENTS_H__

#include <wx/event.h>
#include <wx/treectrl.h>

DECLARE_LOCAL_EVENT_TYPE(wxEVT_WATCHWINDOW_VARIABLE_EVENT, -1)
DECLARE_LOCAL_EVENT_TYPE(wxEVT_WATCHWINDOW_EXPAND_EVENT, -1)

/**
 * Window event class.
 */

class wxWatchWindowDataEvent : public wxEvent
{
	wxTreeItemId itemID;

	struct itemData_t
	{
		wxTreeItemId itemID;

		wxString name;
		wxString value;
		wxString type;

		bool hasAttributes;

		itemData_t(const wxString &n, const wxString &v, const wxString &t, bool a, wxTreeItemId &i) : name(n), value(v), type(t), hasAttributes(a), itemID(i) {}
	};

	std::vector<itemData_t> m_aExpressions;

public:

	wxWatchWindowDataEvent(wxEventType commandType, wxTreeItemId item = NULL) : wxEvent(0, commandType), itemID(item) {}

	virtual wxEvent *Clone() const { return new wxWatchWindowDataEvent(*this); }

	wxTreeItemId GetItem() { return itemID; }

	void AddWatch(const wxString &name, const wxString &value, const wxString &type, bool hasAttributes);
	void AddWatch(const wxString &name, const wxString &value, const wxString &type, bool hasAttributes, wxTreeItemId &item);

	wxTreeItemId GetItem(unsigned int idx) { return m_aExpressions[idx].itemID; }

	wxString GetName(unsigned int idx) { return m_aExpressions[idx].name; }
	wxString GetValue(unsigned int idx) { return m_aExpressions[idx].value; }
	wxString GetType(unsigned int idx) { return m_aExpressions[idx].type; }

	bool HasAttributes(unsigned int idx) { return m_aExpressions[idx].hasAttributes; }

	unsigned int GetWatchCount() { return m_aExpressions.size(); }
};

typedef void (wxEvtHandler::*wxWatchWindowDataEventFunction)(wxWatchWindowDataEvent&);

#define wxWatchWindowDataEventHandler(func) \
  wxEVENT_HANDLER_CAST(wxWatchWindowDataEventFunction, func)

#define EVT_WATCHWINDOW_VARIABLE_EVENT(fn) \
    DECLARE_EVENT_TABLE_ENTRY( wxEVT_WATCHWINDOW_VARIABLE_EVENT, 0, -1, \
    (wxObjectEventFunction) (wxEventFunction) wxStaticCastEvent( wxWatchWindowDataEventFunction, &fn ), (wxObject *) NULL ),

#define EVT_WATCHWINDOW_EXPAND_EVENT(fn) \
    DECLARE_EVENT_TABLE_ENTRY( wxEVT_WATCHWINDOW_EXPAND_EVENT, 0, -1, \
    (wxObjectEventFunction) (wxEventFunction) wxStaticCastEvent( wxWatchWindowDataEventFunction, &fn ), (wxObject *) NULL ),

#endif
