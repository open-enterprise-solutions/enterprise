////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameEnterprise.h"
   
#include "win/dlg/functionAll.h"
#include "win/dlg/enterpriseOption.h"

#include "frontend/win/dlgs/activeUser.h"
#include "frontend/win/dlgs/auditLog.h"
#include "frontend/win/dlgs/about.h"

void ibFrontendDocMDIFrameEnterprise::OnClickAllOperation(wxCommandEvent& event)
{
	ibDialogFunctionAll* dlg = new ibDialogFunctionAll(this, wxID_ANY);
	dlg->Show();

	event.Skip();
}

void ibFrontendDocMDIFrameEnterprise::OnToolsSettings(wxCommandEvent& event)
{
	ibDialogEnterpriseOption* dlg = new ibDialogEnterpriseOption(this, wxID_ANY);
	dlg->Show();

	event.Skip();
}

void ibFrontendDocMDIFrameEnterprise::OnActiveUsers(wxCommandEvent& event)
{
	ibDialogActiveUser* dlg = new ibDialogActiveUser(this, wxID_ANY);
	dlg->Show();

	event.Skip();
}

void ibFrontendDocMDIFrameEnterprise::OnAuditLog(wxCommandEvent& event)
{
	ibDialogAuditLog* dlg = new ibDialogAuditLog(this, wxID_ANY);
	dlg->Show();

	event.Skip();
}

void ibFrontendDocMDIFrameEnterprise::OnAbout(wxCommandEvent& event)
{
	ibDialogAbout* dlg = new ibDialogAbout(this, wxID_ANY);
	dlg->Show();

	event.Skip();
}