////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : launcher app
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"
#include "backend/appData.h"

bool ibAppLauncher::DoOnInit()
{
	if (m_launcher)
		return false;

	// ibWxApp::OnInit already armed ibCrashGuard. wxApp.h is header-only,
	// so no frontend.dll dependency was added — launcher still links
	// only backend.lib + wxlibs.
	ibApplicationData::CreateAppDataEnv(ibRunMode::eLAUNCHER_MODE);
	m_launcher = new ibFrameLauncher(nullptr, wxID_ANY);

	return wxApp::OnInit() && m_launcher->Show();
}

int ibAppLauncher::OnExit()
{
	ibApplicationData::DestroyAppDataEnv();
	return wxApp::OnExit();
}

wxIMPLEMENT_APP(ibAppLauncher);