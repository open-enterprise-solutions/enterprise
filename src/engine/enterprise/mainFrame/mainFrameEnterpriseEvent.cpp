////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameEnterprise.h"
   
#include "win/dlg/functionAll.h"
#include "win/dlg/enterpriseOption.h"

#include "frontend/win/dlgs/activeUser.h"
#include "frontend/win/dlgs/about.h"

#include "frontend/docView/docView.h"
#include "frontend/docView/templates/docViewAuditLog.h"
#include "backend/picturePredefined.h"

void ibFrontendMainFrameEnterprise::OnClickAllOperation(wxCommandEvent& event)
{
	ibDialogFunctionAll* dlg = new ibDialogFunctionAll(this, wxID_ANY);
	dlg->Show();

	event.Skip();
}

void ibFrontendMainFrameEnterprise::OnToolsSettings(wxCommandEvent& event)
{
	ibDialogEnterpriseOption* dlg = new ibDialogEnterpriseOption(this, wxID_ANY);
	dlg->Show();

	event.Skip();
}

void ibFrontendMainFrameEnterprise::OnActiveUsers(wxCommandEvent& event)
{
	ibDialogActiveUser* dlg = new ibDialogActiveUser(this, wxID_ANY);
	dlg->Show();

	event.Skip();
}

void ibFrontendMainFrameEnterprise::OnAuditLog(wxCommandEvent& event)
{
	// Open the journal as a tab via the docview system (replaces
	// the historical modal ibDialogAuditLog). The template is registered
	// in ibDocManagerEnterprise's ctor (via the base AddDocTemplate that
	// builds an ibMetaDocTemplate with g_toolAuditLogCLSID) — invisible
	// to File → New, reachable only through CreateDocument<T>() here.
	if (docManager != nullptr) {
		ibAuditLogDocument* doc = docManager->CreateDocument<ibAuditLogDocument>();
		if (doc != nullptr) {
			doc->SetTitle(_("Registration journal"));
			doc->SetIcon(ibBackendPicture::GetPictureAsIcon(g_picUserActiveCLSID));
			docManager->AddDocument(doc);
			if (!doc->OnCreate(wxEmptyString, 0))
				doc->DeleteAllViews();
		}
	}

	event.Skip();
}

void ibFrontendMainFrameEnterprise::OnAbout(wxCommandEvent& event)
{
	ibDialogAbout* dlg = new ibDialogAbout(this, wxID_ANY);
	dlg->Show();

	event.Skip();
}