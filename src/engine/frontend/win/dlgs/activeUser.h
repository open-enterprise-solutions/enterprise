#ifndef _ACTIVE_USERS_WND_H__
#define _ACTIVE_USERS_WND_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/listctrl.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/dialog.h>
#include <wx/timer.h>

#include "frontend/frontend.h"

class FRONTEND_API CDialogActiveUser : public wxDialog {
	wxListCtrl* m_activeTable;
	std::shared_ptr<wxTimer> m_activeTableScanner;
public:

	void RefreshTable();

	CDialogActiveUser(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Active users"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(600, 250), long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	virtual ~CDialogActiveUser();

protected:
	void OnIdleHandler(wxTimerEvent& event);
};

#endif 