#ifndef __about__
#define __about__

#include <wx/wx.h>
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/statline.h>

#include "frontend/frontend.h"

/**
 * Class CDialogAbout
 */
class FRONTEND_API CDialogAbout : public wxDialog {
	wxStaticText *m_staticText2;
	wxStaticText *m_staticText3;
	wxStaticText *m_staticText6;
	wxStaticLine *window1;
	wxPanel *m_panel1;
	wxStaticText *m_staticText8;
	wxStaticText *m_staticText9;
	wxStaticText *m_staticText10;
	wxStaticLine *window2;
	wxButton *m_button1;

	wxDECLARE_EVENT_TABLE();
public:
	CDialogAbout(wxWindow *parent, int id = wxID_ANY);
protected:
	void OnButtonEvent(wxCommandEvent &event);
};

#endif //__about__

