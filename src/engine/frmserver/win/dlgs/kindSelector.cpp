#include "kindSelector.h"

#include <algorithm>   // std::find_if — only a kind offered comes back

#include "backend/backend_mainFrame.h"
#include "backend/metaData.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"
#include "core/stringUtils.h"

#include "protocol/protocol.h"   // ibProtocol::ibRequest::ibKind::ObjectKind
#include "protocol/protocolDesigner.h"   // ibProtocol::ibRequest::ibObjectKind

ibDialogSelectKind::ibDialogSelectKind(ibValueMetaObject* metaObject, const ibString& caption) :
	m_metaObject(metaObject), m_caption(caption)
{
}

bool ibDialogSelectKind::ShowModal(int& chosen)
{
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr || m_listChoice.empty())
		return false;

	// The window as the desktop's fills it — its title, the object's texts, and a radio button per kind; the name the
	// window wrote on picking one (OnKindChanged) goes with each, the metadata being the server's.
	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::ObjectKind));
	request.SetValue(ibProtocol::ibRequest::ibObjectKind::Caption, m_caption);
	request.SetValue(ibProtocol::ibRequest::ibObjectKind::Name, m_metaObject->GetName());
	request.SetValue(ibProtocol::ibRequest::ibObjectKind::Synonym, m_metaObject->GetSynonym());
	request.SetValue(ibProtocol::ibRequest::ibObjectKind::Comment, m_metaObject->GetComment());
	// The names offered and the one put on the object are asked alike — only the stem changes; the object's own name is
	// no collision.
	ibNameRequest nameRequest;
	nameRequest.m_clsid = m_metaObject->GetClassType();
	nameRequest.m_forConstructor = true;
	nameRequest.m_metaId = m_metaObject->GetMetaID();
	for (const ibChoiceData& choice : m_listChoice) {
		ibDataNode& item = request.AddChild(0, 0);
		item.SetValue(ibProtocol::ibRequest::ibObjectKind::Id, static_cast<std::int32_t>(choice.m_value));
		item.SetValue(ibProtocol::ibRequest::ibObjectKind::Caption, choice.m_label);
		nameRequest.m_prefix = choice.m_name;
		item.SetValue(ibProtocol::ibRequest::ibObjectKind::Name,
			m_metaObject->GetMetaData()->GetNewName(m_metaObject->GetParent(), nameRequest));
	}

	// Cancelled — answered with no Id: a generic form's kind is wxNOT_FOUND, so no kind is the only word for it.
	ibDataNode response;
	if (!frame->Request(request, response) || !response.FindField(ibProtocol::ibRequest::ibObjectKind::Id))
		return false;

	// Only what was offered can come back.
	chosen = response.GetValue<std::int32_t>(ibProtocol::ibRequest::ibObjectKind::Id);
	const auto offered = std::find_if(m_listChoice.begin(), m_listChoice.end(),
		[chosen](const ibChoiceData& choice) { return choice.m_value == chosen; });
	if (offered == m_listChoice.end())
		return false;

	// …and the texts put on the object, as the desktop's OK put them — a name the window would not have let through
	// (OnTextEnter cut it) is the kind's.
	ibString name = response.GetValue<ibString>(ibProtocol::ibRequest::ibObjectKind::Name);
	if (name.IsEmpty() || stringUtils::CheckCorrectName(name) != wxNOT_FOUND) {
		nameRequest.m_prefix = offered->m_name;
		name = m_metaObject->GetMetaData()->GetNewName(m_metaObject->GetParent(), nameRequest);
	}
	m_metaObject->SetName(name);
	m_metaObject->SetSynonym(response.GetValue<ibString>(ibProtocol::ibRequest::ibObjectKind::Synonym));
	m_metaObject->SetComment(response.GetValue<ibString>(ibProtocol::ibRequest::ibObjectKind::Comment));
	return true;
}
