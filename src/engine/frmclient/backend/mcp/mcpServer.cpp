#include "mcpServer.h"

#include "frmclient/mainFrame/mainFrame.h"

namespace {

// The schema's command, the settings its Args.
ibProtocolNode CommandOf(ibProtocolSchemaCommand command, const ibMcpSettings& settings)
{
	ibProtocolNode params;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::McpServer));
	params.SetValue(ibProtocolName::Command, static_cast<long long>(command));
	params.Child(ibProtocolName::Args)
		.SetValue(ibProtocolName::Enabled, settings.m_enabled)
		.SetValue(ibProtocolName::Address, settings.m_address)
		.SetValue(ibProtocolName::Port, static_cast<long long>(settings.m_port))
		.SetValue(ibProtocolName::Token, settings.m_token);
	return params;
}

}

void ibMcpServer::Receive()
{
	ibProtocolNode params, shown;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::McpServer));
	m_shown = frame != nullptr && frame->Call(ibProtocolMethod::Schema, params, shown) ? shown : ibProtocolNode();
}

bool ibMcpServer::IsAllowed() const
{
	return m_shown.GetBool(ibProtocolName::Allowed);
}

ibMcpSettings ibMcpServer::GetSettings() const
{
	ibMcpSettings settings;
	settings.m_enabled = m_shown.GetBool(ibProtocolName::Enabled);
	settings.m_address = m_shown.GetString(ibProtocolName::Address, settings.m_address);
	settings.m_port = static_cast<unsigned short>(m_shown.GetInt(ibProtocolName::Port, settings.m_port));
	settings.m_token = m_shown.GetString(ibProtocolName::Token);
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
	return frame->Call(ibProtocolMethod::Schema,
		CommandOf(ibProtocolSchemaCommand::Save, m_settings.value_or(GetSettings())), answer);
}

bool ibMcpServer::Start(ibSession* WXUNUSED(session), wxString& refusal)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	// IN THE NAME OF THE SESSION THAT ASKS — the server's for this client. Its refusal handed back, said by the caller.
	ibProtocolNode answer;
	ibProtocolRefusal refused = ibProtocolRefusal::None;
	const bool started = frame->Call(ibProtocolMethod::Schema,
		CommandOf(ibProtocolSchemaCommand::Start, m_settings.value_or(GetSettings())), answer, refused, refusal);
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
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::McpServer));
	params.SetValue(ibProtocolName::Command, static_cast<long long>(ibProtocolSchemaCommand::Stop));
	frame->Call(ibProtocolMethod::Schema, params, answer);
	Receive();
}

bool ibMcpServer::IsRunning() const
{
	return m_shown.GetBool(ibProtocolName::Running);
}

wxString ibMcpServer::GetEndpoint() const
{
	return m_shown.GetString(ibProtocolName::Endpoint);
}
