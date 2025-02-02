#ifndef __TITLE__
#define __TITLE__

#include <wx/wx.h>

class CPanelTitle : public wxPanel {
public:
	CPanelTitle(wxWindow *parent, const wxString &title = wxT("No title"));
	static wxWindow* CreateTitle(wxWindow *inner, const wxString &title);
};

#endif //__TITLE__