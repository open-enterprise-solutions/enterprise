////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameEnterprise.h"
#include <wx/config.h>

CMainFrameEnterprise::CMainFrameEnterprise(const wxString& title,
	const wxPoint& pos,
	const wxSize& size) : 
	CMainFrame(title, pos, size)
{
}

void CMainFrameEnterprise::CreateGUI()
{
	InitializeDefaultMenu();
	CreateWideGui();
}