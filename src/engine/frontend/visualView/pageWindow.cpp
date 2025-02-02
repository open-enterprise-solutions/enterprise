////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : visual page window 
////////////////////////////////////////////////////////////////////////////

#include "pageWindow.h"

wxIMPLEMENT_DYNAMIC_CLASS(CPanelPage, wxPanel);

CPanelPage::CPanelPage() : wxPanel(), m_boxSizer(nullptr) {}

CPanelPage::CPanelPage(wxWindow *parent,
	wxWindowID winid,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& name) : wxPanel(parent, winid, pos, size, style, name), m_boxSizer(new wxBoxSizer(wxHORIZONTAL)) {}

CPanelPage::~CPanelPage() { SetSizer(nullptr); }