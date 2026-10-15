#ifndef __TITLE__
#define __TITLE__

#include <wx/wx.h>

class ibPanelTitle : public wxPanel {
public:
	ibPanelTitle(wxWindow *parent, const wxString &title = wxT("No title"));
	static wxWindow* CreateTitle(wxWindow *inner, const wxString &title);
};

#endif //__TITLE__
