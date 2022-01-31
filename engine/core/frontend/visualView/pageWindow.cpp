////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : visual page window 
////////////////////////////////////////////////////////////////////////////

#include "pageWindow.h"

wxIMPLEMENT_DYNAMIC_CLASS(CPageWindow, wxPanel);

CPageWindow::CPageWindow() : wxPanel(), m_boxSizer(NULL) {}

CPageWindow::CPageWindow(wxWindow *parent,
	wxWindowID winid,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& name) : wxPanel(parent, winid, pos, size, style, name), m_boxSizer(new wxBoxSizer(wxHORIZONTAL)) {}

CPageWindow::~CPageWindow() { SetSizer(NULL); }