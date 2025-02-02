#ifndef __DESIGNER_MENUBAR_H__
#define __DESIGNER_MENUBAR_H__

#include <wx/wx.h>
#include <vector>

#include "backend/backend_core.h"

class CPanelMenuBar : public wxPanel
{
public:
	CPanelMenuBar();
	CPanelMenuBar(
		wxWindow* parent, int id, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
		long style = 0, const wxString& name = wxT("menubar"));
	
	virtual ~CPanelMenuBar();

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

class CMenuEvtHandler : public wxEvtHandler
{
public:
	CMenuEvtHandler(wxStaticText* st, wxMenu* menu);
	void OnMouseEvent(wxMouseEvent& event);
	wxDECLARE_EVENT_TABLE();
private:
	wxStaticText* m_label;
	wxMenu* m_menu;
};

#endif  // __DESIGNER_MENUBAR_H__