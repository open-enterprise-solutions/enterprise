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

const ibProtocolNode& ibMcpServer::Shown() const
{
	if (m_shown.has_value())
		return *m_shown;

	static const ibProtocolNode refused;
	ibProtocolNode params, shown;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::McpServer));
	if (frame == nullptr || !frame->Call(ibProtocolMethod::Schema, params, shown))
		return refused;
	m_shown = shown;
	return *m_shown;
}

ibMcpSettings ibMcpServer::GetSettings() const
{
	const ibProtocolNode shown = Shown();

	ibMcpSettings settings;
	settings.m_enabled = shown.GetBool(ibProtocolName::Enabled);
	settings.m_address = shown.GetString(ibProtocolName::Address, settings.m_address);
	settings.m_port = static_cast<unsigned short>(shown.GetInt(ibProtocolName::Port, settings.m_port));
	settings.m_token = shown.GetString(ibProtocolName::Token);
	return settings;
}

bool ibMcpServer::SaveSettings(ibSession* WXUNUSED(session)) const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	ibProtocolNode answer;
	const bool saved = frame->Call(ibProtocolMethod::Schema,
		CommandOf(ibProtocolSchemaCommand::Save, m_settings.value_or(GetSettings())), answer);
	m_shown.reset();
	return saved;
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
	// What the server holds now — a token it may have minted, the endpoint — is asked for by the next read, once.
	m_shown.reset();
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
	m_shown.reset();
}

bool ibMcpServer::IsRunning() const
{
	return Shown().GetBool(ibProtocolName::Running);
}

wxString ibMcpServer::GetEndpoint() const
{
	return Shown().GetString(ibProtocolName::Endpoint);
}
