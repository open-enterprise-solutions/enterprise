#include "applyChange.h"

#include "backend/backend_mainFrame.h"
#include "core/picturePredefined.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"

#include "frmserver/client/clientFrame.h"   // ibClientFrame::SendPicture — the picture, by its id
#include "protocol/protocol.h"   // ibProtocolRequestKind::ApplyChange

bool ibDialogApplyChange::ShowApplyChange(const ibRestructureInfo& info)
{
	if (info.IsEmpty())
		return true;

	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr)
		return false;

	// The ledger as the desktop's window lists it — a row per entry, its level beside it (an error blocks the apply
	// there) — and the window's own picture.
	ibDataNode request;
	request.SetValue(wxT("Kind"), static_cast<s32>(ibProtocolRequestKind::ApplyChange));
	request.SetValue(wxT("Picture"), ibClientFrame::SendPicture(g_picStructureCLSID));
	for (const ibRestructureInfo::Entry& entry : info) {
		ibDataNode& item = request.AddChild(0, 0);
		item.SetValue(wxT("Level"), static_cast<s32>(entry.type));
		item.SetValue(wxT("Text"), entry.descr);
	}

	ibDataNode response;
	if (!frame->Request(request, response))
		return false;

	// Cancelled — no Done; and a ledger with an error is never applied, whatever came back.
	return response.GetValue<bool>(wxT("Done")) && !info.HasErrors();
}
