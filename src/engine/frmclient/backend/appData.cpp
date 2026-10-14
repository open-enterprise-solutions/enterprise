#include "appData.h"

#include "frmclient/backend/mcp/mcpServer.h"
#include "frmclient/mainFrame/mainFrame.h"

bool ibApplicationData::DesignerMode() const
{
	const ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	return frame != nullptr && frame->GetMode() == ibProtocolMode::Designer;
}

ibMcpServer* ibApplicationInstance::GetMcpServer()
{
	// The schema says whether the person may switch it here (Allowed): not on the application server, nor where the
	// mode has no such schema (the runtime) — refused, and said to nobody.
	static ibMcpServer server;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return nullptr;

	ibProtocolNode params, shown;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::McpServer));
	ibProtocolRefusal refusal = ibProtocolRefusal::None;
	wxString error;
	return frame->Call(ibProtocolMethod::Schema, params, shown, refusal, error) && shown.GetBool(ibProtocolName::Allowed)
		? &server : nullptr;
}
