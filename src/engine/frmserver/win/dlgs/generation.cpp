#include "generation.h"

#include "backend/backend_mainFrame.h"
#include "backend/backend_picture.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"

#include "protocol/protocol.h"   // ibProtocolRequestKind::Generation

bool ibDialogGeneration::ShowModal(ibMetaID& id)
{
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr)
		return false;

	// The list as the desktop's window fills it — a row per object generated, its synonym and its picture — and the
	// window's own picture.
	ibDataNode request;
	request.SetValue(wxT("Kind"), static_cast<s32>(ibProtocolRequestKind::Generation));
	request.SetValue(wxT("Picture"), wxString(ibBackendPicture::GetServerPicture(g_picGenerateCLSID).GetData()));
	for (unsigned int idx = 0; idx < m_metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObject* typeCtor = m_metaData->FindAnyObjectByFilter(m_metaDesc.GetByIdx(idx));
		if (typeCtor == nullptr)
			continue;
		ibDataNode& item = request.AddChild(0, 0);
		item.SetValue(wxT("Id"), static_cast<s32>(m_metaDesc.GetByIdx(idx)));
		item.SetValue(wxT("Caption"), typeCtor->GetSynonym());
		item.SetValue(wxT("Picture"), wxString(ibBackendPicture::GetServerPicture(typeCtor->GetClassType()).GetData()));
	}

	ibDataNode response;
	if (!frame->Request(request, response))
		return false;

	// Cancelled — no Id; and only what was offered can come back.
	if (response.FindField(wxT("Id")) == nullptr)
		return false;
	const ibMetaID chosen = response.GetValue<s32>(wxT("Id"));
	if (!m_metaDesc.ContainMetaType(chosen))
		return false;
	id = chosen;
	return true;
}

ibDialogGeneration::ibDialogGeneration(const ibMetaData* metaData, const ibMetaDescription& metaDesc) :
	m_metaData(metaData), m_metaDesc(metaDesc)
{
}
