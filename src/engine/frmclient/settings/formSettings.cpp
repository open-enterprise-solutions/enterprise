#include "formSettings.h"

#include "frmclient/mainFrame/mainFrame.h"
#include "protocol/protocol.h"

namespace {

// AN ACT OF THE EDITOR'S — its answer: the server does it and asks again, saying how it went.
ibFormSettingsResult ActOnForm(ibProtocolFormEditorAct act)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return ibFormSettingsResult::NoStorage;

	ibProtocolNode response;
	response.SetValue(ibProtocolName::Act, static_cast<long long>(act));
	if (!frame->Respond(response))
		return ibFormSettingsResult::Refused;
	return static_cast<ibFormSettingsResult>(frame->GetRequest().GetInt(ibProtocolName::Result,
		static_cast<long long>(ibFormSettingsResult::Refused)));
}

} // namespace

ibFormSettingsResult ibSaveFormSettings(const ibValueForm* WXUNUSED(form))
{
	return ActOnForm(ibProtocolFormEditorAct::Save);
}

ibFormSettingsResult ibResetFormSettings(const ibValueForm* WXUNUSED(form))
{
	return ActOnForm(ibProtocolFormEditorAct::Reset);
}

bool ibHasFormSettings(const ibValueForm* WXUNUSED(form))
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	return frame != nullptr && frame->GetRequest().GetBool(ibProtocolName::HasSetting);
}
