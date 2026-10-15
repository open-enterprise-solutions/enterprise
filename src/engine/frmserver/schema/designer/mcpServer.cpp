#include "mcpServer.h"

#include "backend/appData.h"
#include "backend/mcp/mcpServer.h"
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"

namespace {

// The settings a page holds — Start's and Save's arguments, as Build gave them.
ibMcpSettings SettingsOf(const ibDataNode& args)
{
	ibMcpSettings settings;
	settings.m_enabled = args.GetValue<bool>(wxT("Enabled"));
	settings.m_address = args.GetValue<wxString>(wxT("Address"));
	settings.m_port = static_cast<unsigned short>(args.GetValue<s32>(wxT("Port")));
	settings.m_token = args.GetValue<wxString>(wxT("Token"));
	return settings;
}

}

void ibSchemaMcpServer::Build(const ibSession& session, const ibDataNode& WXUNUSED(args), ibDataNode& result) const
{
	const ibMcpServer* const server = SwitchableServer(session);
	result.SetValue(wxT("Allowed"), server != nullptr);
	const ibMcpSettings settings = server != nullptr ? server->GetSettings() : ibMcpSettings();
	result.SetValue(wxT("Enabled"), settings.m_enabled);
	result.SetValue(wxT("Address"), settings.m_address);
	result.SetValue(wxT("Port"), static_cast<s32>(settings.m_port));
	result.SetValue(wxT("Token"), settings.m_token);
	result.SetValue(wxT("Running"), server != nullptr && server->IsRunning());
	result.SetValue(wxT("Endpoint"), server != nullptr ? server->GetEndpoint() : wxString());
}

std::function<void()> ibSchemaMcpServer::Command(const ibSession& session, s32 command,
	const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const
{
	ibMcpServer* const server = SwitchableServer(session);
	if (server == nullptr) {
		refusal = ibProtocolRefusal::NotNow;
		error = wxT("the base has no assistant access");
		return nullptr;
	}

	switch (static_cast<ibProtocolSchemaCommand>(command)) {
	case ibProtocolSchemaCommand::Start:
		// IN THE NAME OF THIS SESSION, on what the page says — as the page's Start now did (ibPanelMcpSettings::OnRun).
		server->Configure(SettingsOf(args));
		if (server->Start(ibSession::Current(), error))
			return []() {};
		refusal = ibProtocolRefusal::NotNow;
		return nullptr;
	case ibProtocolSchemaCommand::Stop:
		return [server]() { server->Stop(); };
	case ibProtocolSchemaCommand::Save: {
		// Configure first, save second, both from the one value the page holds — as the dialog's OK did.
		const ibMcpSettings settings = SettingsOf(args);
		return [server, settings]() {
			server->Configure(settings);
			server->SaveSettings(ibSession::Current());
		};
	}
	default:
		break;
	}

	refusal = ibProtocolRefusal::NotFound;
	error = wxString::Format(wxT("assistant access has no command %d"), command);
	return nullptr;
}

ibMcpServer* ibSchemaMcpServer::SwitchableServer(const ibSession& session)
{
	const ibApplicationInstance* const applicationInstance = session.GetApplicationInstance();
	if (applicationInstance == nullptr || applicationInstance->ServiceMode())
		return nullptr;

	return ibApplicationInstance::GetMcpServer(applicationInstance);
}
