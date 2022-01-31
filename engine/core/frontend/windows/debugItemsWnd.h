#ifndef _DEBUG_ITEMS_H_
#define _DEBUG_ITEMS_H_

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/listctrl.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/dialog.h>

#include <wx/timer.h>

#include <map>

#include "compiler/debugger/debugEvent.h"

class CDebugItemsWnd : public wxDialog
{
	wxListCtrl* m_availableList;
	wxListCtrl* m_attachedList;

	std::vector<std::pair<unsigned short, wxString>> m_aAvailable;
	std::vector<std::pair<unsigned short, wxString>> m_aAttached;

	wxTimer *m_connectionScanner;

public:

	void RefreshDebugList();

	CDebugItemsWnd(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Debug items"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(500, 400), long style = wxDEFAULT_DIALOG_STYLE);
	virtual ~CDebugItemsWnd();

protected:

	void OnAttachedItemSelected(wxListEvent &event);
	void OnAvailableItemSelected(wxListEvent &event);

	void OnIdleHandler(wxTimerEvent& event);
};

#endif 
