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

class ibDialogDebugItem : public wxDialog {
public:

	ibDialogDebugItem(wxWindow* parent, wxWindowID id = wxID_ANY,
		const wxString& title = _("Debug items"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(500, 400), long style = wxDEFAULT_DIALOG_STYLE);
	virtual ~ibDialogDebugItem();

protected:

	void RefreshDebugList();

	void OnAttachedItemSelected(wxListEvent& event);
	void OnAvailableItemSelected(wxListEvent& event);

	void OnIdleHandler(wxTimerEvent& event) { 
		RefreshDebugList(); 
		event.Skip(); 
	}
private:

	wxTimer* m_connectionScanner;
	wxListCtrl* m_availableList, * m_attachedList;

	std::vector<std::pair<unsigned short, wxString>> m_listAvailable;
	std::vector<std::pair<unsigned short, wxString>> m_listAttached;
};

#endif 
