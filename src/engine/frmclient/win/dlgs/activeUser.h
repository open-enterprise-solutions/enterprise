#ifndef _FRMCLIENT_ACTIVE_USER_H__
#define _FRMCLIENT_ACTIVE_USER_H__

#include <wx/artprov.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/dialog.h>
#include <wx/timer.h>

#include <memory>
#include <string>

#include "frmclient/frmclient.h"
#include "protocol/protocolNode.h"

// ACTIVE USERS — the desktop's dialog (frontend/win/dlgs/activeUser) as it is: its sessions and its locks read from
// the server's schema (frmserver/schema/activeUser.cpp) where it read the registry and the lock manager itself, asked
// again every second as it read them.
class FRMCLIENT_API ibDialogActiveUser : public wxDialog {
	wxNotebook* m_notebook;
	wxListCtrl* m_activeTable;
	wxListCtrl* m_locksTable;
	std::shared_ptr<wxTimer> m_activeTableScanner;
	std::string m_sessionArray;   // the sessions shown, as they came — the desktop's hash of them
	size_t  m_lastLockRowCount = 0;
public:

	void RefreshActiveUserTable(const ibProtocolNode& shown);
	void RefreshLocksTable(const ibProtocolNode& shown);

	ibDialogActiveUser(wxWindow* parent, const ibProtocolNode& shown, wxWindowID id = wxID_ANY, const wxString& title = _("Active users"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(720, 320), long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	virtual ~ibDialogActiveUser();

protected:
	void OnIdleHandler(wxTimerEvent& event);
};

#endif
