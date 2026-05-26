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

	// Crash plumbing is wired by ibWxApp::OnInit before this runs.
	ibApplicationData::CreateAppDataEnv(ibRunMode::eENTERPRISE_MODE);
	m_codeRunner = new ibFrameCodeRunner(nullptr, wxID_ANY);

	return wxApp::OnInit() && m_codeRunner->Show();
}

int ibAppCodeRunner::OnExit()
{
	ibApplicationData::DestroyAppDataEnv();
	return wxApp::OnExit();
}

void ibAppCodeRunner::AppendOutput(const wxString& str)
{
	if (m_codeRunner)
		m_codeRunner->AppendOutput(str);
}

wxIMPLEMENT_APP(ibAppCodeRunner);