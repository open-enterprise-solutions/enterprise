////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : launcher app
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"
#include "classCheckerWnd.h"

bool CMainApp::OnInit()
{
	wxDateTime::SetCountry(wxDateTime::Country::USA);

	m_locale.AddCatalogLookupPathPrefix(_T("lang"));
	m_locale.AddCatalog(m_locale.GetCanonicalName());

	CFrameClassChecker *launcherWnd = 
		new CFrameClassChecker(nullptr, wxID_ANY);
	launcherWnd->Show();

	return m_locale.Init(wxLANGUAGE_ENGLISH);
}


wxIMPLEMENT_APP(CMainApp);   