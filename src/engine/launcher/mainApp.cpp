////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : launcher app
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"
#include "launcherWnd.h"

bool CMainApp::OnInit()
{
	wxDateTime::SetCountry(wxDateTime::Country::USA);

	m_locale.AddCatalogLookupPathPrefix(_T("lang"));
	m_locale.AddCatalog(m_locale.GetCanonicalName());

	CFrameLauncher *launcherWnd = 
		new CFrameLauncher(nullptr, wxID_ANY);
	launcherWnd->Show();

	return m_locale.Init(wxLANGUAGE_ENGLISH);
}


wxIMPLEMENT_APP(CMainApp);   