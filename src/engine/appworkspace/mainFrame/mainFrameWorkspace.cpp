////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameWorkspace.h"

#include "win/dlg/functionAll.h"

ibFrontendMainFrameWorkspace::ibFrontendMainFrameWorkspace(std::unique_ptr<ibCommunicator> communicator,
	const wxString& user, const wxString& password,
	const wxString& title, const wxPoint& pos, const wxSize& size)
	: ibFrontendMainFrame(std::move(communicator), ibProtocol::ibLogin::ibMode::Workspace, user, password, title, pos, size)
{
}

void ibFrontendMainFrameWorkspace::CreateStartupPage()
{
	CreateSubSystem();
}

void ibFrontendMainFrameWorkspace::ShowSchema(ibProtocol::ibSchema::ibKind schema, const ibProtocolNode& shown)
{
	if (schema != ibProtocol::ibSchema::ibKind::AllFunctions) {
		ibFrontendMainFrame::ShowSchema(schema, shown);
		return;
	}

	// An item opened is the schema's own command — Open {Item}, as the navigation opens it; the default command type.
	ibDialogFunctionAll* dlg = new ibDialogFunctionAll(this, shown, [this](long long item) {
		ibProtocolNode params, answer;
		params.SetValue(ibProtocol::ibSchema::Schema, static_cast<int>(ibProtocol::ibSchema::ibKind::AllFunctions))
			.SetValue(ibProtocol::ibSchema::Command, static_cast<int>(ibProtocol::ibSchema::ibCommand::Open));
		params.Child(ibProtocol::ibSchema::Args).SetValue(ibProtocol::ibSchema::ibAllFunctions::Item, item);
		return Call(ibProtocol::ibMethod::Schema, params, answer);
	});
	dlg->Show();
}
