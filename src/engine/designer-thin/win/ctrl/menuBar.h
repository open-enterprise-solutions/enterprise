#ifndef __DESIGNER_MENUBAR_H__
#define __DESIGNER_MENUBAR_H__

#include <wx/wx.h>
#include <vector>

#include "frmclient/backend/backend_core.h"

// THE DESKTOP'S (designer/win/ctrl/menuBar.h) WHOLE. The desktop makes it nowhere — nor does the thin.

class ibPanelMenuBar : public wxPanel
{
public:
	ibPanelMenuBar();
	ibPanelMenuBar(
		wxWindow* parent, int id, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
		long style = 0, const wxString& name = wxT("menubar"));
	
	virtual ~ibPanelMenuBar() override;

	void AppendMenu(wxMenu* menu, const wxString& name);

	wxMenu* GetMenu(int i) const {
		wxASSERT(i < int(m_menus.size()));
		return m_menus[i];
	}
	
	int GetMenuCount() const {
		return (unsigned int)m_menus.size();
	}
	
	wxMenu* Remove(int i);

private:
	std::vector<wxMenu*> m_menus;
	wxBoxSizer* m_sizer;
};

class ibMenuEvtHandler : public wxEvtHandler
{
public:
	ibMenuEvtHandler(wxStaticText* st, wxMenu* menu);
	void OnMouseEvent(wxMouseEvent& event);
	wxDECLARE_EVENT_TABLE();
private:
	wxStaticText* m_label;
	wxMenu* m_menu;
};

#endif  // __DESIGNER_MENUBAR_H__