////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameEnterprise.h"
#include <wx/config.h>

wxAuiDocEnterpriseMDIFrame::wxAuiDocEnterpriseMDIFrame(const wxString& title,
	const wxPoint& pos,
	const wxSize& size) : 
	wxAuiDocMDIFrame(title, pos, size)
{
}

void wxAuiDocEnterpriseMDIFrame::CreateGUI()
{
	CreateWideGui();
}