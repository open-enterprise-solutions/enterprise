#ifndef _ACTIVE_USERS_WND_H__
#define _ACTIVE_USERS_WND_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
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

#include "backend/guid.h"
#include "frontend/frontend.h"

class FRONTEND_API ibDialogActiveUser : public wxDialog {
	wxNotebook* m_notebook;
	wxListCtrl* m_activeTable;
	wxListCtrl* m_locksTable;
	std::shared_ptr<wxTimer> m_activeTableScanner;
	ibGuid m_sessionArrayHash;
	size_t  m_lastLockRowCount = 0;
public:

	void RefreshActiveUserTable();
	void RefreshLocksTable();

	ibDialogActiveUser(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Active users"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(720, 320), long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	virtual ~ibDialogActiveUser();

protected:
	void OnIdleHandler(wxTimerEvent& event) {
		RefreshActiveUserTable();
		RefreshLocksTable();
	}
	// Right-click on a row → context menu with Kick / Reload. Both
	// write to sys_session.signal via ibSessionRegistry; the owning
	// process picks the directive up on its next JobCheckSignal tick.
	void OnContextMenu(wxListEvent& event);
	void OnKickSelected(wxCommandEvent& event);
	void OnReloadSelected(wxCommandEvent& event);

	// Locks tab: right-click → force-release. Designer-only admin path,
	// drops the named sys_lock row regardless of owner. Use when a
	// zombie session left a lock behind.
	void OnLocksContextMenu(wxListEvent& event);
	void OnForceReleaseSelected(wxCommandEvent& event);
};

#endif