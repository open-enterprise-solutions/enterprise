////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, unknownworlds team
//	Description : watch window
////////////////////////////////////////////////////////////////////////////

#include "watchWindowEvents.h"

//
// Events declarations
//

DEFINE_EVENT_TYPE(wxEVT_WATCHWINDOW_VARIABLE_EVENT)
DEFINE_EVENT_TYPE(wxEVT_WATCHWINDOW_EXPAND_EVENT)

void wxWatchWindowDataEvent::AddWatch(const wxString &name, const wxString &value, const wxString &type, bool hasAttributes)
{
	m_aExpressions.emplace_back(name, value, type, hasAttributes, itemID); 
}

void wxWatchWindowDataEvent::AddWatch(const wxString &name, const wxString &value, const wxString &type, bool hasAttributes, wxTreeItemId &item)
{
	m_aExpressions.emplace_back(name, value, type, hasAttributes, item);
}