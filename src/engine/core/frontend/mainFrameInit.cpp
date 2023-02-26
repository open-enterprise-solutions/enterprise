////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrame.h" 
#include "core/frontend/docView/docManager.h"

//***********************************************************************************
//*                                 mainFrame                                       *
//***********************************************************************************

wxAuiDocMDIFrame* wxAuiDocMDIFrame::s_instance = NULL;

#include "frontend/mainFrameDesigner.h"
#include "frontend/mainFrameEnterprise.h"

void wxAuiDocMDIFrame::InitializeFrame(eRunMode mode)
{
	if (s_instance == NULL && mode == eRunMode::eDESIGNER_MODE) {
		s_instance = new wxAuiDocDesignerMDIFrame;
		s_instance->CreateGUI();
		wxTheApp->SetTopWindow(s_instance);
	}
	else if (s_instance == NULL && mode == eRunMode::eENTERPRISE_MODE) {
		s_instance = new wxAuiDocEnterpriseMDIFrame;
		s_instance->CreateGUI();
		wxTheApp->SetTopWindow(s_instance);
	}
}

void wxAuiDocMDIFrame::DestroyFrame()
{
	if (s_instance != NULL)
		s_instance->Destroy();
}