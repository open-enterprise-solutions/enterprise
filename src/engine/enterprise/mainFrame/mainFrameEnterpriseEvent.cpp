////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameEnterprise.h"
   
#include "win/dlg/functionAll.h"
#include "win/dlg/enterpriseOption.h"

#include "frontend/win/dlgs/activeUser.h"
#include "frontend/win/dlgs/about.h"

void CDocEnterpriseMDIFrame::OnClickAllOperation(wxCommandEvent& event)
{
	CDialogFunctionAll* dlg = new CDialogFunctionAll(this, wxID_ANY);
	dlg->Show();

	event.Skip();
}

void CDocEnterpriseMDIFrame::OnToolsSettings(wxCommandEvent& event)
{
	CDialogEnterpriseOption* dlg = new CDialogEnterpriseOption(this, wxID_ANY);
	dlg->Show();

	event.Skip();
}

void CDocEnterpriseMDIFrame::OnActiveUsers(wxCommandEvent& event)
{
	CDialogActiveUser* dlg = new CDialogActiveUser(this, wxID_ANY);
	dlg->Show();

	event.Skip();
}

void CDocEnterpriseMDIFrame::OnAbout(wxCommandEvent& event)
{
	CDialogAbout* dlg = new CDialogAbout(this, wxID_ANY);
	dlg->Show();

	event.Skip();
}