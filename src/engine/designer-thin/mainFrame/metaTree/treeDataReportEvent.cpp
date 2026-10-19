////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report events
////////////////////////////////////////////////////////////////////////////

#include "treeDataReport.h"

#include "core/serialize/dataBuilder.h"   // ibDataValue — what an edit gives
#include "core/stringUtils.h"

void ibDataReportTree::OnEditCaptionName(wxCommandEvent& WXUNUSED(event))
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
	ibValueMetaObject(static_cast<ibMetaID>(m_root.GetId()), ibMetaData(GetTab()))
		.Send(wxT("Name"), ibDataValue::String(systemName));
}

void ibDataReportTree::OnEditCaptionSynonym(wxCommandEvent& WXUNUSED(event))
{
	if (!m_synonymValue->IsModified())
		return;

	m_synonymValue->SetModified(false);
	ibValueMetaObject(static_cast<ibMetaID>(m_root.GetId()), ibMetaData(GetTab()))
		.Send(wxT("Synonym"), ibDataValue::String(m_synonymValue->GetValue()));
}

void ibDataReportTree::OnEditCaptionComment(wxCommandEvent& WXUNUSED(event))
{
	if (!m_commentValue->IsModified())
		return;

	m_commentValue->SetModified(false);
	ibValueMetaObject(static_cast<ibMetaID>(m_root.GetId()), ibMetaData(GetTab()))
		.Send(wxT("Comment"), ibDataValue::String(m_commentValue->GetValue()));
}

void ibDataReportTree::OnChoiceDefForm(wxCommandEvent& event)
{
	// The offer's id — none: <not selected>.
	const long long id = static_cast<long long>(reinterpret_cast<intptr_t>(event.GetClientData()));

	ibValueMetaObject(static_cast<ibMetaID>(m_root.GetId()), ibMetaData(GetTab()))
		.Send(wxT("DefaultFormObject"), ibDataValue::Int(id > 0 ? id : wxNOT_FOUND));
}

void ibDataReportTree::OnChoiceDefComposer(wxCommandEvent& event)
{
	// The offer's id — none: <not selected>.
	const long long id = static_cast<long long>(reinterpret_cast<intptr_t>(event.GetClientData()));

	ibValueMetaObject(static_cast<ibMetaID>(m_root.GetId()), ibMetaData(GetTab()))
		.Send(wxT("DefaultComposer"), ibDataValue::Int(id > 0 ? id : wxNOT_FOUND));
}

void ibDataReportTree::OnButtonModuleClicked(wxCommandEvent& WXUNUSED(event))
{
	// ⭐ TAKEN FROM THE STRUCTURE THE OBJECT ALREADY OFFERS — its menu's entry that names the object module, opened by
	// its id (the desktop's).
	for (const ibProtocolNode& entry : m_root.FindChild(ibProtocol::ibSchema::ibMetaData::Menu).Children()) {
		if (entry.GetString(ibProtocol::ibSchema::ibMetaData::Name) == wxT("ObjectModule") && entry.GetInt(ibProtocol::ibSchema::ibMetaData::Object) != 0) {
			ibProtocolNode args;
			args.SetValue(ibProtocol::ibSchema::ibMetaData::Id, entry.GetInt(ibProtocol::ibSchema::ibMetaData::Object));
			Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Open, args);
			break;
		}
	}
}
