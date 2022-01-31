#pragma once 

#include <wx/statusbr.h>

#define DEFAULT_COLOUR wxColour(41, 57, 85) 
#define WHITE_COLOUR wxColour(255, 255, 255) 

class CStatusBar : public wxStatusBar
{
	wxStaticText *m_statusBarText;
public:

	CStatusBar() : wxStatusBar() {};
	CStatusBar(wxWindow *parent,
		wxWindowID id = wxID_ANY,
		long style = wxSTB_DEFAULT_STYLE,
		const wxString& name = wxStatusBarNameStr)
		: wxStatusBar(parent, id, style, name)
	{
		SetBackgroundColour(DEFAULT_COLOUR);
		SetForegroundColour(WHITE_COLOUR);

		m_statusBarText = new wxStaticText(this, wxID_ANY, wxEmptyString, wxPoint(5, 5), wxDefaultSize, 0);
		wxFont m_font = m_statusBarText->GetFont();

		auto siz = m_font.GetPixelSize();

		auto fam = m_font.GetFamily();
		auto styl = m_font.GetStyle();
		auto wei = m_font.GetWeight();

		m_statusBarText->Show(true);
	}

	virtual void DoUpdateStatusText(int nField) override
	{
		m_statusBarText->SetLabelText(GetStatusText(nField));
	}
};