#include "formSelector.h"

#include <algorithm>   // std::find_if — only a kind offered comes back

#include "backend/backend_mainFrame.h"
#include "backend/metaData.h"
#include "backend/metaCollection/metaFormObject.h"
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"
#include "core/stringUtils.h"

#include "protocol/protocol.h"   // ibProtocolRequestKind::FormType

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
	request.SetValue(ibProtocolName::Kind, static_cast<s32>(ibProtocolRequestKind::FormType));
	request.SetValue(ibProtocolName::Caption, m_metaValue->GetSynonym() + _(" form wizard"));
	request.SetValue(ibProtocolName::Name, m_metaObject->GetName());
	request.SetValue(ibProtocolName::Synonym, m_metaObject->GetSynonym());
	request.SetValue(ibProtocolName::Comment, m_metaObject->GetComment());
	for (const choiceData_t& choice : m_listChoice) {
		ibDataNode& item = request.AddChild(0, 0);
		item.SetValue(ibProtocolName::Id, static_cast<s32>(choice.m_value));
		item.SetValue(ibProtocolName::Caption, choice.m_label);
		item.SetValue(ibProtocolName::Name, m_metaObject->GetMetaData()->GetNewName(m_metaObject->GetClassType(),
			m_metaObject->GetParent(), choice.m_name, true));
	}

	// Cancelled — answered with no Id: a generic form's kind is wxNOT_FOUND, so no kind is the only word for it.
	ibDataNode response;
	if (!frame->Request(request, response) || response.FindField(ibProtocolName::Id) == nullptr)
		return false;

	// Only what was offered can come back.
	chosen = response.GetValue<s32>(ibProtocolName::Id);
	const auto offered = std::find_if(m_listChoice.begin(), m_listChoice.end(),
		[chosen](const choiceData_t& choice) { return choice.m_value == chosen; });
	if (offered == m_listChoice.end())
		return false;

	// …and the texts put on the form, as the desktop's OK put them — a name the window would not have let through
	// (OnTextEnter cut it) is the kind's.
	wxString name = response.GetValue<wxString>(wxT("Name"));
	if (name.IsEmpty() || stringUtils::CheckCorrectName(name) != wxNOT_FOUND)
		name = m_metaObject->GetMetaData()->GetNewName(m_metaObject->GetClassType(), m_metaObject->GetParent(),
			offered->m_name, true);
	m_metaObject->SetName(name);
	m_metaObject->SetSynonym(response.GetValue<wxString>(wxT("Synonym")));
	m_metaObject->SetComment(response.GetValue<wxString>(wxT("Comment")));
	return true;
}
