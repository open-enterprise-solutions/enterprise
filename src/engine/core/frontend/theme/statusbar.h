#ifndef _STATUSBAR_H__
#define _STATUSBAR_H__

#include <wx/statusbr.h>

#define DEFAULT_COLOUR wxColour(41, 57, 85) 
#define WHITE_COLOUR wxColour(255, 255, 255) 

class CBottomStatusBar : public wxStatusBar {
	wxStaticText *m_statusBarText;
public:

	CBottomStatusBar() : wxStatusBar() {};
	CBottomStatusBar(wxWindow *parent,
		wxWindowID id = wxID_ANY,
		long style = wxSTB_DEFAULT_STYLE,
		const wxString& name = wxStatusBarNameStr)
		: wxStatusBar(parent, id, style, name)
	{
		SetBackgroundColour(DEFAULT_COLOUR);
		SetForegroundColour(WHITE_COLOUR);

		m_statusBarText = new wxStaticText(this, wxID_ANY, wxEmptyString, wxPoint(5, 5), wxDefaultSize, 0);
		m_statusBarText->Show(true);
	}

	virtual void DoUpdateStatusText(int field) override {
		m_statusBarText->SetLabelText(
			GetStatusText(field)
		);
	}
};

#endif 