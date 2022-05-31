#ifndef __TITLE__
#define __TITLE__

#include <wx/wx.h>

class CTitle : public wxPanel
{
private:
	//  DECLARE_EVENT_TABLE()
public:
	CTitle(wxWindow *parent, const wxString &title = wxT("No title"));

	static wxWindow* CreateTitle(wxWindow *inner, const wxString &title);
};

#endif //__TITLE__