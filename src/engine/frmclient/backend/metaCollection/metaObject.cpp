#include "metaObject.h"

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

bool ibValueMetaObject::ReadProperty()
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	ibProtocolNode params, answer;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::Properties));
	ibProtocolNode args = params.Child(ibProtocolName::Args);
	args.SetValue(ibProtocolName::Id, static_cast<long long>(m_metaId));
	args.SetValue(ibProtocolName::Tab, m_metaData.GetTabId());
	if (!frame->Call(ibProtocolMethod::Schema, params, answer))
		return false;

	ibDataNode rows;
	ibReadProtocolNode(answer.FindChild(ibProtocolName::Properties), rows);
	return ibPropertyObject::ReadProperty(rows);
}

bool ibValueMetaObject::OnPropertyChanging(ibProperty* property, const wxVariant& WXUNUSED(newValue))
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	// …the value as the property's node holds it now: the door (ibPropertyGate) gave it before asking.
	ibDataNode args;
	args.SetValue(wxT("Id"), static_cast<s32>(m_metaId));
	args.SetValue(wxT("Name"), property->GetName());
	args.SetValue(wxT("Tab"), static_cast<s32>(m_metaData.GetTabId()));
	const ibDataValue value = property->GetNodeValue();
	if (value.Kind() == ibDataKind::Child)
		args.SetProperty(wxT("Value"), value);
	else
		args.AddField(wxT("Value"), value);

	ibProtocolNode params, answer;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::Properties));
	params.SetValue(ibProtocolName::Command, static_cast<long long>(ibProtocolSchemaCommand::Save));
	ibWriteProtocolNode(args, params.Child(ibProtocolName::Args));
	return frame->Call(ibProtocolMethod::Schema, params, answer);
}
