////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrame.h" 
#include "common/reportManager.h"

//***********************************************************************************
//*                                 mainFrame                                       *
//***********************************************************************************

CMainFrame* CMainFrame::s_instance = NULL;

CMainFrame* CMainFrame::Get() {
	return s_instance;
}

#include "frontend/mainFrameDesigner.h"
#include "frontend/mainFrameEnterprise.h"

void CMainFrame::Initialize(eRunMode mode)
{
	if (!s_instance) {
		switch (mode)
		{
		case eRunMode::DESIGNER_MODE:
			s_instance = new CMainFrameDesigner;
			s_instance->CreateGUI();
			break;
		case eRunMode::ENTERPRISE_MODE:
			s_instance = new CMainFrameEnterprise;
			s_instance->CreateGUI();
			break;
		}
	}

	if (s_instance != NULL) {
		wxTheApp->SetTopWindow(s_instance);
	}
}

void CMainFrame::Destroy()
{
	wxDELETE(s_instance);
}