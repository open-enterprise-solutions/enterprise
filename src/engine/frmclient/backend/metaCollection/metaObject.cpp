#include "metaObject.h"

#include <map>

#include "core/serialize/dataBuilder.h"
#include "frmclient/backend/serialize/dataProtocol.h"   // ibReadProtocolNode, ibWriteProtocolNode — the rows, the edit
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
	args.SetValue(ibProtocolName::Id, static_cast<long long>(id));
	args.SetValue(ibProtocolName::Tab, metaData.GetTabId());

	ibProtocolNode params;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::ObjectText));
	params.SetValue(ibProtocolName::Args, args);
	return params;
}

// …the one named, as the server has it now.
wxString ReadObjectText(ibMetaID id, const ibMetaData& metaData, const char* name)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	ibProtocolNode answer;
	if (frame == nullptr || !frame->Call(ibProtocolMethod::Schema, ObjectTextParams(id, metaData, ibProtocolNode()), answer))
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
	params.SetValue(ibProtocolName::Command, static_cast<long long>(ibProtocolSchemaCommand::Save));
	ibProtocolNode answer;
	frame->Call(ibProtocolMethod::Schema, params, answer);
}

} // namespace

wxString ibValueMetaObject::GetHelpContent() const
{
	return ReadObjectText(m_metaId, m_metaData, ibProtocolName::Help);
}

void ibValueMetaObject::SetHelpContent(const wxString& strHelpContent)
{
	SaveObjectText(m_metaId, m_metaData, ibProtocolName::Help, strHelpContent);
}

wxString ibValueMetaObject::GetNoteContent() const
{
	return ReadObjectText(m_metaId, m_metaData, ibProtocolName::Note);
}

void ibValueMetaObject::SetNoteContent(const wxString& strNoteContent)
{
	SaveObjectText(m_metaId, m_metaData, ibProtocolName::Note, strNoteContent);
}

bool ibValueMetaObject::Send(const wxString& name, const ibDataValue& value) const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	ibDataNode args;
	args.SetValue(ibProtocolName::Id, static_cast<s32>(m_metaId));
	args.SetValue(ibProtocolName::Name, name);
	args.SetValue(ibProtocolName::Tab, static_cast<s32>(m_metaData.GetTabId()));
	if (value.Kind() == ibDataKind::Child)
		args.SetProperty(ibProtocolName::Value, value);
	else
		args.AddField(ibProtocolName::Value, value);

	ibProtocolNode params, answer;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::Properties));
	params.SetValue(ibProtocolName::Command, static_cast<long long>(ibProtocolSchemaCommand::Save));
	ibWriteProtocolNode(args, params.Child(ibProtocolName::Args));
	return frame->Call(ibProtocolMethod::Schema, params, answer);
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

bool ibValueMetaObject::Receive()
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
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::Properties));
	ibProtocolNode args = params.Child(ibProtocolName::Args);
	args.SetValue(ibProtocolName::Id, static_cast<long long>(m_metaId));
	args.SetValue(ibProtocolName::Tab, m_metaData.GetTabId());
	if (!frame->Call(ibProtocolMethod::Schema, params, answer))
		return false;
	const ibProtocolNode rows = answer.FindChild(ibProtocolName::Properties);
	Receive(m_metaData.GetTabId(), m_metaId, rows);
	return ibPropertyObject::Receive(rows);
}

bool ibValueMetaObject::OnPropertyChanging(ibProperty* property, const wxVariant& WXUNUSED(newValue))
{
	// …the value as the property's node holds it now: the door (ibPropertyGate) gave it before asking.
	return Send(property->GetName(), property->GetNodeValue());
}
