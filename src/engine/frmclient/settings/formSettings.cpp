#include "formSettings.h"

#include "frmclient/mainFrame/mainFrame.h"
#include "protocol/protocol.h"

namespace {

// AN ACT OF THE EDITOR'S — its answer: the server does it and asks again, saying how it went.
ibFormSettingsResult ActOnForm(ibProtocol::ibRequest::ibFormEditor::ibAct act)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return ibFormSettingsResult::NoStorage;

	ibProtocolNode response;
	response.SetValue(ibProtocol::ibRequest::ibFormEditor::Act, static_cast<long long>(act));
	if (!frame->Respond(response))
		return ibFormSettingsResult::Refused;
	return static_cast<ibFormSettingsResult>(frame->GetRequest().GetInt(ibProtocol::ibRequest::ibFormEditor::Result,
		static_cast<long long>(ibFormSettingsResult::Refused)));
}

} // namespace

ibFormSettingsResult ibSaveFormSettings(const ibValueForm* WXUNUSED(form))
{
	return ActOnForm(ibProtocol::ibRequest::ibFormEditor::ibAct::Save);
}

ibFormSettingsResult ibResetFormSettings(const ibValueForm* WXUNUSED(form))
{
	return ActOnForm(ibProtocol::ibRequest::ibFormEditor::ibAct::Reset);
}

bool ibHasFormSettings(const ibValueForm* WXUNUSED(form))
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	return frame != nullptr && frame->GetRequest().GetBool(ibProtocol::ibRequest::ibFormEditor::HasSetting);
}
