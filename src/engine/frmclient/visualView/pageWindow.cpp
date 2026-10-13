////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : visual page window
////////////////////////////////////////////////////////////////////////////

#include "pageWindow.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibPanelPage, wxPanel);

ibPanelPage::ibPanelPage() :
	wxPanel(), m_boxSizer(nullptr)
{
}

ibPanelPage::ibPanelPage(wxWindow* parent, wxWindowID winid, const wxPoint& pos, const wxSize& size, long style, const wxString& name) :
	wxPanel(parent, winid, pos, size, style, name), m_boxSizer(nullptr)
{
	m_boxSizer = new wxBoxSizer(wxHORIZONTAL);
	SetSizer(m_boxSizer);
}

ibPanelPage::~ibPanelPage()
{
	SetSizer(nullptr);
}
