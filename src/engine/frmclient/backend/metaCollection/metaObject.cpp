#include "metaObject.h"

#include <map>

#include "core/serialize/dataBuilder.h"
#include "frmclient/backend/serialize/dataProtocol.h"   // ibReadProtocolNode, ibWriteProtocolNode — the rows, the edit
#include "frmclient/backend/propertyManager/property/propertySerializable.h"   // its properties — a row's each
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // an edit's value — its node
#include "frmclient/mainFrame/mainFrame.h"              // the door to the server

wxString ibValueMetaObject::GetName() const
{
	const ibProperty* const property = GetProperty(wxT("Name"));
	return property != nullptr ? property->GetValue().GetString() : wxString();
}

wxString ibValueMetaObject::GetSynonym() const
{
	const ibProperty* const property = GetProperty(wxT("Synonym"));
	return property != nullptr ? property->GetValue().GetString() : wxString();
}

namespace {

// The object's texts' params — its id, in its tree's tab — `args` filled.
ibProtocolNode ObjectTextParams(ibMetaID id, const ibMetaData& metaData, ibProtocolNode args)
{
	args.SetValue(ibProtocol::ibSchema::ibObjectText::Id, static_cast<long long>(id));
	args.SetValue(ibProtocol::ibSchema::ibObjectText::Tab, metaData.GetTabId());

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::ObjectText));
	params.SetValue(ibProtocol::ibSchema::Args, args);
	return params;
}

// …the one named, as the server has it now.
wxString ReadObjectText(ibMetaID id, const ibMetaData& metaData, const char* name)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	ibProtocolNode answer;
	if (frame == nullptr || !frame->Call(ibProtocol::ibMethod::Schema, ObjectTextParams(id, metaData, ibProtocolNode()), answer))
		return wxString();
	return answer.GetString(name);
}

// …given. Refused: the person told why.
void SaveObjectText(ibMetaID id, const ibMetaData& metaData, const char* name, const wxString& text)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return;

	ibProtocolNode args;
	args.SetValue(name, text);
	ibProtocolNode params = ObjectTextParams(id, metaData, args);
	params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(ibProtocol::ibSchema::ibCommand::Save));
	ibProtocolNode answer;
	frame->Call(ibProtocol::ibMethod::Schema, params, answer);
}

} // namespace

wxString ibValueMetaObject::GetHelpContent() const
{
	return ReadObjectText(m_metaId, m_metaData, ibProtocol::ibSchema::ibObjectText::Help);
}

void ibValueMetaObject::SetHelpContent(const wxString& strHelpContent)
{
	SaveObjectText(m_metaId, m_metaData, ibProtocol::ibSchema::ibObjectText::Help, strHelpContent);
}

wxString ibValueMetaObject::GetNoteContent() const
{
	return ReadObjectText(m_metaId, m_metaData, ibProtocol::ibSchema::ibObjectText::Note);
}

void ibValueMetaObject::SetNoteContent(const wxString& strNoteContent)
{
	SaveObjectText(m_metaId, m_metaData, ibProtocol::ibSchema::ibObjectText::Note, strNoteContent);
}

bool ibValueMetaObject::Send(const wxString& name, const ibDataValue& value) const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	ibDataNode args;
	args.SetValue(ibProtocol::ibSchema::ibProperties::Id, static_cast<std::int32_t>(m_metaId));
	args.SetValue(ibProtocol::ibSchema::ibProperties::Name, name);
	args.SetValue(ibProtocol::ibSchema::ibProperties::Tab, static_cast<std::int32_t>(m_metaData.GetTabId()));
	if (value.Kind() == ibDataKind::Child)
		args.SetProperty(ibProtocol::ibSchema::ibProperties::Value, value);
	else
		args.AddField(ibProtocol::ibSchema::ibProperties::Value, value);

	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::Properties));
	params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(ibProtocol::ibSchema::ibCommand::Save));
	ibWriteProtocolNode(args, params.Child(ibProtocol::ibSchema::Args));
	return frame->Call(ibProtocol::ibMethod::Schema, params, answer);
}

bool ibValueMetaObject::Send(const wxString& name, const wxVariant& value) const
{
	return Send(name, ibVariantDataSerializable::GetNodeValue(value));
}

namespace {

	// The rows received, and the frame they are true at: another frame drops them all.
	struct ibReceivedRows {
		long long frame = 0;
		std::map<std::pair<long long, ibMetaID>, ibProtocolNode> rows;
	};

	ibReceivedRows& ReceivedRows()
	{
		static ibReceivedRows received;
		return received;
	}

	// The rows of the object received at this frame, or none.
	const ibProtocolNode* FindReceivedRows(long long tabId, ibMetaID metaId, long long frame)
	{
		ibReceivedRows& received = ReceivedRows();
		if (received.frame != frame)
			return nullptr;
		const auto found = received.rows.find({ tabId, metaId });
		return found != received.rows.end() ? &found->second : nullptr;
	}
}

void ibValueMetaObject::Receive(long long tabId, ibMetaID metaId, const ibProtocolNode& rows)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr || !rows.IsNode())
		return;
	ibReceivedRows& received = ReceivedRows();
	if (received.frame != frame->GetFrameNumber()) {
		received.rows.clear();
		received.frame = frame->GetFrameNumber();
	}
	received.rows[{ tabId, metaId }] = rows;
}

bool ibValueMetaObject::Fetch()
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	// The rows read before or shown beside an answer — those, while the frame stays.
	if (const ibProtocolNode* const found = FindReceivedRows(m_metaData.GetTabId(), m_metaId, frame->GetFrameNumber())) {
		const ibProtocolNode rows = *found;   // taking them in may receive others
		return ibPropertyObject::Receive(rows);
	}

	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::Properties));
	ibProtocolNode args = params.Child(ibProtocol::ibSchema::Args);
	args.SetValue(ibProtocol::ibSchema::ibProperties::Id, static_cast<long long>(m_metaId));
	args.SetValue(ibProtocol::ibSchema::ibProperties::Tab, m_metaData.GetTabId());
	if (!frame->Call(ibProtocol::ibMethod::Schema, params, answer))
		return false;
	const ibProtocolNode rows = answer.FindChild(ibProtocol::ibSchema::ibProperties::Properties);
	Receive(m_metaData.GetTabId(), m_metaId, rows);
	return ibPropertyObject::Receive(rows);
}

ibProperty* ibValueMetaObject::CreateProperty(ibPropertyCategory* cat, ibDataNode&& row)
{
	return ibPropertyObject::CreateProperty<ibPropertySerializable>(cat, std::move(row));
}

bool ibValueMetaObject::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	return Send(property->GetName(), newValue);
}

void ibValueMetaObject::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	// …its rows came with the answer (Receive, by the tab and the id), taken in as it is read anew.
	Cleared();
	ibPropertyObject::OnPropertyChanged(property, oldValue, newValue);
}
