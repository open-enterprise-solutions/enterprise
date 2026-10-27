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

	// The image handlers are the window's: the backend keeps its pictures as data and puts none up (backend_picture.h).
	wxInitAllImageHandlers();

	// ibWxApp::OnInit already armed ibCrashGuard. oesApp.h (core) is header-only,
	// so no GUI library dependency was added — launcher still links
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