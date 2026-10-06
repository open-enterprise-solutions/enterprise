////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : code runner app
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"

#include "backend/appData.h"

bool ibAppCodeRunner::DoOnInit()
{
	if (m_codeRunner)
		return false;

	// ⚠ THE COMMAND LINE FIRST, then the base. wx calls OnExit only after an OnInit that succeeded, so
	// a base opened before a refusal (`--help`, an unknown switch) was never closed by it: the process
	// went down in the CRT's atexit, after the statics its plugins unsubscribe from (2026-10-07).
	if (!wxApp::OnInit())
		return false;

	// Crash plumbing is wired by ibWxApp::OnInit before this runs.
	ibApplicationInstance::CreateAppDataEnv(ibRunMode::eSANDBOX_MODE);
	m_codeRunner = new ibFrameCodeRunner(nullptr, wxID_ANY);

	// Show's false means "nothing changed", not a failure — and a false here would skip OnExit again.
	m_codeRunner->Show();
	return true;
}

int ibAppCodeRunner::OnExit()
{
	ibApplicationInstance::DestroyAppDataEnv();
	return wxApp::OnExit();
}

void ibAppCodeRunner::AppendOutput(const wxString& str)
{
	if (m_codeRunner)
		m_codeRunner->AppendOutput(str);
}

#include "backend/diagnostics/leakTracker.h"

IB_LEAK_TRACKER_ARM();

wxIMPLEMENT_APP(ibAppCodeRunner);