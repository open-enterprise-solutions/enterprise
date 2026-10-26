#include "generation.h"

#include "backend/backend_mainFrame.h"
#include "core/picturePredefined.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"

#include "backend/backend_picture.h"   // ibBackendPicture::SendPicture — the pictures, by their ids
#include "protocol/protocol.h"   // ibProtocol::ibRequest::ibKind::Generation

bool ibDialogGeneration::ShowModal(ibMetaID& id)
{
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr)
		return false;

	// The list as the desktop's window fills it — a row per object generated, its synonym and its picture — and the
	// window's own picture.
	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::Generation));
	request.SetValue(ibProtocol::ibRequest::ibGeneration::Picture, ibBackendPicture::SendPicture(g_picGenerateCLSID));
	for (unsigned int idx = 0; idx < m_metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObject* typeCtor = m_metaData->FindAnyObjectByFilter(m_metaDesc.GetByIdx(idx));
		if (typeCtor == nullptr)
			continue;
		ibDataNode& item = request.AddChild(0, 0);
		item.SetValue(ibProtocol::ibRequest::ibGeneration::Id, static_cast<std::int32_t>(m_metaDesc.GetByIdx(idx)));
		item.SetValue(ibProtocol::ibRequest::ibGeneration::Caption, typeCtor->GetSynonym());
		item.SetValue(ibProtocol::ibRequest::ibGeneration::Picture, ibBackendPicture::SendPicture(typeCtor->GetClassType()));
	}

	ibDataNode response;
	if (!frame->Request(request, response))
		return false;

	// Cancelled — no Id; and only what was offered can come back.
	if (!response.FindField(ibProtocol::ibRequest::ibGeneration::Id))
		return false;
	const ibMetaID chosen = response.GetValue<std::int32_t>(ibProtocol::ibRequest::ibGeneration::Id);
	if (!m_metaDesc.ContainMetaType(chosen))
		return false;
	id = chosen;
	return true;
}

ibDialogGeneration::ibDialogGeneration(const ibMetaData* metaData, const ibMetaDescription& metaDesc) :
	m_metaData(metaData), m_metaDesc(metaDesc)
{
}
