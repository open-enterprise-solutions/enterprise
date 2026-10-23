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
	settings.m_enabled = args.GetValue<bool>(ibProtocol::ibSchema::ibMcpServer::Enabled);
	settings.m_address = args.GetValue<wxString>(ibProtocol::ibSchema::ibMcpServer::Address);
	settings.m_port = static_cast<unsigned short>(args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibMcpServer::Port));
	settings.m_token = args.GetValue<wxString>(ibProtocol::ibSchema::ibMcpServer::Token);
	return settings;
}

}

void ibSchemaMcpServer::Build(const ibSession& session, const ibDataNode& /*args*/, ibDataNode& result) const
{
	const ibMcpServer* const server = SwitchableServer(session);
	result.SetValue(ibProtocol::ibSchema::ibMcpServer::Allowed, server != nullptr);
	const ibMcpSettings settings = server != nullptr ? server->GetSettings() : ibMcpSettings();
	result.SetValue(ibProtocol::ibSchema::ibMcpServer::Enabled, settings.m_enabled);
	result.SetValue(ibProtocol::ibSchema::ibMcpServer::Address, settings.m_address);
	result.SetValue(ibProtocol::ibSchema::ibMcpServer::Port, static_cast<std::int32_t>(settings.m_port));
	result.SetValue(ibProtocol::ibSchema::ibMcpServer::Token, settings.m_token);
	result.SetValue(ibProtocol::ibSchema::ibMcpServer::Running, server != nullptr && server->IsRunning());
	result.SetValue(ibProtocol::ibSchema::ibMcpServer::Endpoint, server != nullptr ? server->GetEndpoint() : wxString());
}

std::function<void()> ibSchemaMcpServer::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, wxString& error) const
{
	ibMcpServer* const server = SwitchableServer(session);
	if (server == nullptr) {
		refusal = ibProtocol::ibRefusal::NotNow;
		error = wxT("the base has no assistant access");
		return nullptr;
	}

	switch (static_cast<ibProtocol::ibSchema::ibCommand>(command)) {
	case ibProtocol::ibSchema::ibCommand::Start:
		// IN THE NAME OF THIS SESSION, on what the page says — as the page's Start now did (ibPanelMcpSettings::OnRun).
		server->Configure(SettingsOf(args));
		if (server->Start(ibSession::Current(), error))
			return []() {};
		refusal = ibProtocol::ibRefusal::NotNow;
		return nullptr;
	case ibProtocol::ibSchema::ibCommand::Stop:
		return [server]() { server->Stop(); };
	case ibProtocol::ibSchema::ibCommand::Save: {
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

	refusal = ibProtocol::ibRefusal::NotFound;
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
