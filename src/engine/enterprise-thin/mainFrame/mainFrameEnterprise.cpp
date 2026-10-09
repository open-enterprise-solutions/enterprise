////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameEnterprise.h"

#include "win/dlg/functionAll.h"

ibFrontendMainFrameEnterprise::ibFrontendMainFrameEnterprise(std::unique_ptr<ibCommunicator> communicator,
	const wxString& user, const wxString& password,
	const wxString& title, const wxPoint& pos, const wxSize& size)
	: ibFrontendMainFrame(std::move(communicator), ibProtocolMode::Runtime, user, password, title, pos, size)
{
	// Its templates are the server's — the external data processor and report among them (DrawTemplates).
	m_docManager = new ibFrontendDocManager;
}

void ibFrontendMainFrameEnterprise::CreateStartupPage()
{
	CreateSubSystem();
}

void ibFrontendMainFrameEnterprise::ShowSchema(ibProtocolSchema schema, const ibProtocolNode& shown)
{
	if (schema != ibProtocolSchema::AllFunctions) {
		ibFrontendMainFrame::ShowSchema(schema, shown);
		return;
	}

	// An item opened is the schema's own command — Open {Item}, as the navigation opens it; the default command type.
	ibDialogFunctionAll* dlg = new ibDialogFunctionAll(this, shown, [this](long long item) {
		ibProtocolNode params, answer;
		params.SetValue(ibProtocolName::Schema, static_cast<int>(ibProtocolSchema::AllFunctions))
			.SetValue(ibProtocolName::Command, static_cast<int>(ibProtocolSchemaCommand::Open));
		params.Child(ibProtocolName::Args).SetValue(ibProtocolName::Item, item);
		return Call(ibProtocolMethod::Schema, params, answer);
	});
	dlg->Show();
}
