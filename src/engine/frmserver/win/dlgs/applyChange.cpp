#include "applyChange.h"

#include "backend/backend_mainFrame.h"
#include "core/picturePredefined.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"

#include "backend/backend_picture.h"   // ibBackendPicture::SendPicture — the picture, by its id
#include "protocol/protocol.h"   // ibProtocol::ibRequest::ibKind::ApplyChange

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
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::ApplyChange));
	request.SetValue(ibProtocol::ibRequest::ibApplyChange::Picture, ibBackendPicture::SendPicture(g_picStructureCLSID));
	for (const ibRestructureInfo::Entry& entry : info) {
		ibDataNode& item = request.AddChild(0, 0);
		item.SetValue(ibProtocol::ibRequest::ibApplyChange::Level, static_cast<std::int32_t>(entry.type));
		item.SetValue(ibProtocol::ibRequest::ibApplyChange::Text, entry.descr);
	}

	ibDataNode response;
	if (!frame->Request(request, response))
		return false;

	// Cancelled — no Done; and a ledger with an error is never applied, whatever came back.
	return response.GetValue<bool>(ibProtocol::ibRequest::ibApplyChange::Done) && !info.HasErrors();
}
