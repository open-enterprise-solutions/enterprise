#include "mcpServer.h"

#include "frmclient/mainFrame/mainFrame.h"

namespace {

// The schema's command, the settings its Args.
ibProtocolNode CommandOf(ibProtocol::ibSchema::ibCommand command, const ibMcpSettings& settings)
{
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::McpServer));
	params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(command));
	params.Child(ibProtocol::ibSchema::Args)
		.SetValue(ibProtocol::ibSchema::ibMcpServer::Enabled, settings.m_enabled)
		.SetValue(ibProtocol::ibSchema::ibMcpServer::Address, settings.m_address)
		.SetValue(ibProtocol::ibSchema::ibMcpServer::Port, static_cast<long long>(settings.m_port))
		.SetValue(ibProtocol::ibSchema::ibMcpServer::Token, settings.m_token);
	return params;
}

}

void ibMcpServer::Receive()
{
	ibProtocolNode params, shown;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::McpServer));
	m_shown = frame != nullptr && frame->Call(ibProtocol::ibMethod::Schema, params, shown) ? shown : ibProtocolNode();
}

bool ibMcpServer::IsAllowed() const
{
	return m_shown.GetBool(ibProtocol::ibSchema::ibMcpServer::Allowed);
}

ibMcpSettings ibMcpServer::GetSettings() const
{
	ibMcpSettings settings;
	settings.m_enabled = m_shown.GetBool(ibProtocol::ibSchema::ibMcpServer::Enabled);
	settings.m_address = m_shown.GetString(ibProtocol::ibSchema::ibMcpServer::Address, settings.m_address);
	settings.m_port = static_cast<unsigned short>(m_shown.GetInt(ibProtocol::ibSchema::ibMcpServer::Port, settings.m_port));
	settings.m_token = m_shown.GetString(ibProtocol::ibSchema::ibMcpServer::Token);
	return settings;
}

bool ibMcpServer::SaveSettings(ibSession* WXUNUSED(session)) const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	// What it shows is not received again: the page that saved closes with it, and the next one opens over the menu
	// item's answer.
	ibProtocolNode answer;
	return frame->Call(ibProtocol::ibMethod::Schema,
		CommandOf(ibProtocol::ibSchema::ibCommand::Save, m_settings.value_or(GetSettings())), answer);
}

bool ibMcpServer::Start(ibSession* WXUNUSED(session), wxString& refusal)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	// IN THE NAME OF THE SESSION THAT ASKS — the server's for this client. Its refusal handed back, said by the caller.
	ibProtocolNode answer;
	ibProtocol::ibRefusal refused = ibProtocol::ibRefusal::None;
	const bool started = frame->Call(ibProtocol::ibMethod::Schema,
		CommandOf(ibProtocol::ibSchema::ibCommand::Start, m_settings.value_or(GetSettings())), answer, refused, refusal);
	// What the server holds now — a token it may have minted, the endpoint — received once, for the page to read.
	if (started)
		Receive();
	return started;
}

void ibMcpServer::Stop()
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return;

	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::McpServer));
	params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(ibProtocol::ibSchema::ibCommand::Stop));
	frame->Call(ibProtocol::ibMethod::Schema, params, answer);
	Receive();
}

bool ibMcpServer::IsRunning() const
{
	return m_shown.GetBool(ibProtocol::ibSchema::ibMcpServer::Running);
}

wxString ibMcpServer::GetEndpoint() const
{
	return m_shown.GetString(ibProtocol::ibSchema::ibMcpServer::Endpoint);
}
