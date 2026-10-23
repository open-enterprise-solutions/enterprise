#include "formSelector.h"

#include <algorithm>   // std::find_if — only a kind offered comes back

#include "backend/backend_mainFrame.h"
#include "backend/metaData.h"
#include "backend/metaCollection/metaFormObject.h"
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"
#include "core/stringUtils.h"

#include "protocol/protocol.h"   // ibProtocol::ibRequest::ibKind::FormType

ibDialogSelectTypeForm::ibDialogSelectTypeForm(ibValueMetaObject* metaValue, ibValueMetaObjectFormBase* metaObject) :
	m_metaValue(metaValue), m_metaObject(metaObject)
{
}

bool ibDialogSelectTypeForm::ShowModal(int& chosen)
{
	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr || m_listChoice.empty())
		return false;

	// …and the desktop's last button, a generic form: the kind no owner lists (CreateSelector put it after them).
	m_listChoice.emplace_back(formDefaultName, _("Generic form"), defaultFormType);

	// The window as the desktop's fills it — its title, the form's texts, and a radio button per kind; the name the
	// window wrote on picking one (OnFormTypeChanged) goes with each, the metadata being the server's.
	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::FormType));
	request.SetValue(ibProtocol::ibRequest::ibFormType::Caption, m_metaValue->GetSynonym() + _(" form wizard"));
	request.SetValue(ibProtocol::ibRequest::ibFormType::Name, m_metaObject->GetName());
	request.SetValue(ibProtocol::ibRequest::ibFormType::Synonym, m_metaObject->GetSynonym());
	request.SetValue(ibProtocol::ibRequest::ibFormType::Comment, m_metaObject->GetComment());
	// The names offered and the one put on the form are asked alike — only the stem changes.
	ibNameRequest nameRequest;
	nameRequest.m_clsid = m_metaObject->GetClassType();
	nameRequest.m_forConstructor = true;
	for (const choiceData_t& choice : m_listChoice) {
		ibDataNode& item = request.AddChild(0, 0);
		item.SetValue(ibProtocol::ibRequest::ibFormType::Id, static_cast<std::int32_t>(choice.m_value));
		item.SetValue(ibProtocol::ibRequest::ibFormType::Caption, choice.m_label);
		nameRequest.m_prefix = choice.m_name;
		item.SetValue(ibProtocol::ibRequest::ibFormType::Name,
			m_metaObject->GetMetaData()->GetNewName(m_metaObject->GetParent(), nameRequest));
	}

	// Cancelled — answered with no Id: a generic form's kind is wxNOT_FOUND, so no kind is the only word for it.
	ibDataNode response;
	if (!frame->Request(request, response) || response.FindField(ibProtocol::ibRequest::ibFormType::Id) == nullptr)
		return false;

	// Only what was offered can come back.
	chosen = response.GetValue<std::int32_t>(ibProtocol::ibRequest::ibFormType::Id);
	const auto offered = std::find_if(m_listChoice.begin(), m_listChoice.end(),
		[chosen](const choiceData_t& choice) { return choice.m_value == chosen; });
	if (offered == m_listChoice.end())
		return false;

	// …and the texts put on the form, as the desktop's OK put them — a name the window would not have let through
	// (OnTextEnter cut it) is the kind's.
	wxString name = response.GetValue<wxString>(ibProtocol::ibRequest::ibFormType::Name);
	if (name.IsEmpty() || stringUtils::CheckCorrectName(name) != wxNOT_FOUND) {
		nameRequest.m_prefix = offered->m_name;
		name = m_metaObject->GetMetaData()->GetNewName(m_metaObject->GetParent(), nameRequest);
	}
	m_metaObject->SetName(name);
	m_metaObject->SetSynonym(response.GetValue<wxString>(ibProtocol::ibRequest::ibFormType::Synonym));
	m_metaObject->SetComment(response.GetValue<wxString>(ibProtocol::ibRequest::ibFormType::Comment));
	return true;
}
