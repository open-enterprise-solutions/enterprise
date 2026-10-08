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

	// The command line first, then the base — see codeRunner/mainApp.cpp: OnExit follows only an
	// OnInit that succeeded.
	if (!wxApp::OnInit())
		return false;

	// ibWxApp::OnInit already armed ibCrashGuard. wxApp.h is header-only,
	// so no frontend.dll dependency was added — launcher still links
	// only backend.lib + wxlibs.
	ibApplicationInstance::CreateAppDataEnv(ibRunMode::eLAUNCHER_MODE);
	m_launcher = new ibFrameLauncher(nullptr, wxID_ANY);

	m_launcher->Show();
	return true;
}

int ibAppLauncher::OnExit()
{
	ibApplicationInstance::DestroyAppDataEnv();
	return wxApp::OnExit();
}

#include "core/diagnostics/leakTracker.h"

IB_LEAK_TRACKER_ARM();

wxIMPLEMENT_APP(ibAppLauncher);