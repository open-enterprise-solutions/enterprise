////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataprocessor events
////////////////////////////////////////////////////////////////////////////

#include "treeDataProcessor.h"

#include "core/stringUtils.h"

void ibDataProcessorTree::OnEditCaptionName(wxCommandEvent& WXUNUSED(event))
{
	if (!m_nameValue->IsModified())
		return;

	wxString systemName = m_nameValue->GetValue();

	int pos = stringUtils::CheckCorrectName(systemName);
	if (pos > 0) {
		systemName = systemName.Left(pos);
		m_nameValue->ChangeValue(systemName);
	}

	// …its synonym made from it by the server: a renamed object's (ibValueMetaObject::OnPropertyChanged).
	m_nameValue->SetModified(false);
	ibProtocolNode args;
	args.SetValue(ibProtocolName::Value, systemName);
	SaveProperty(m_root.GetId(), wxT("Name"), args);
}

void ibDataProcessorTree::OnEditCaptionSynonym(wxCommandEvent& WXUNUSED(event))
{
	if (!m_synonymValue->IsModified())
		return;

	m_synonymValue->SetModified(false);
	ibProtocolNode args;
	args.SetValue(ibProtocolName::Value, m_synonymValue->GetValue());
	SaveProperty(m_root.GetId(), wxT("Synonym"), args);
}

void ibDataProcessorTree::OnEditCaptionComment(wxCommandEvent& WXUNUSED(event))
{
	if (!m_commentValue->IsModified())
		return;

	m_commentValue->SetModified(false);
	ibProtocolNode args;
	args.SetValue(ibProtocolName::Value, m_commentValue->GetValue());
	SaveProperty(m_root.GetId(), wxT("Comment"), args);
}

void ibDataProcessorTree::OnChoiceDefForm(wxCommandEvent& event)
{
	// The offer's id — none: <not selected>.
	const long long id = static_cast<long long>(reinterpret_cast<intptr_t>(event.GetClientData()));

	ibProtocolNode args;
	args.SetValue(ibProtocolName::Value, id > 0 ? id : static_cast<long long>(wxNOT_FOUND));
	SaveProperty(m_root.GetId(), wxT("DefaultFormObject"), args);
}

void ibDataProcessorTree::OnButtonModuleClicked(wxCommandEvent& WXUNUSED(event))
{
	// ⭐ TAKEN FROM THE STRUCTURE THE OBJECT ALREADY OFFERS — its menu's entry that names the object module, opened by
	// its id (the desktop's).
	for (const ibProtocolNode& entry : m_root.FindChild(ibProtocolName::Menu).Children()) {
		if (entry.GetString(ibProtocolName::Name) == wxT("ObjectModule") && entry.GetInt(ibProtocolName::Object) != 0) {
			ibProtocolNode args;
			args.SetValue(ibProtocolName::Id, entry.GetInt(ibProtocolName::Object));
			Command(ibProtocolMetaDataCommand::Open, args);
			break;
		}
	}
}
