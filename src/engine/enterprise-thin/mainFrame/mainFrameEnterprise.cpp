////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameEnterprise.h"

#include "win/dlg/functionAll.h"

ibFrontendMainFrameEnterprise::ibFrontendMainFrameEnterprise(std::unique_ptr<ibCommunicator> communicator,
	const wxString& user, const wxString& password,
	const wxString& title, const wxPoint& pos, const wxSize& size)
	: ibFrontendMainFrame(std::move(communicator), ibProtocol::ibLogin::ibMode::Runtime, user, password, title, pos, size)
{
	// Its templates are the server's — the external data processor and report among them (DrawTemplates).
	m_docManager = new ibFrontendDocManager;
}

void ibFrontendMainFrameEnterprise::CreateStartupPage()
{
	CreateSubSystem();
}

void ibFrontendMainFrameEnterprise::ShowSchema(ibProtocol::ibSchema::ibKind schema, const ibProtocolNode& shown)
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
