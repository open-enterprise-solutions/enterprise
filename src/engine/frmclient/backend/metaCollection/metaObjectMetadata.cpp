#include "metaObjectMetadata.h"

#include "frmclient/backend/serialize/dataProtocol.h"   // ibReadProtocolNode, ibWriteProtocolNode — the answer, the edit
#include "frmclient/mainFrame/mainFrame.h"              // the door to the server
#include "protocol/protocol.h"                          // ibProtocolSchema::HomePage — the editor the server holds
#include "protocol/protocolNode.h"

// The workspace editor's — done by the server's (ibProtocolSchema::HomePage), the root of this id in its tree's tab.

namespace {

ibProtocolNode HomePageParams(const ibValueMetaObject* object)
{
	ibProtocolNode params;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::HomePage));
	ibProtocolNode args = params.Child(ibProtocolName::Args);
	args.SetValue(ibProtocolName::Id, static_cast<long long>(object->GetMetaID()));
	args.SetValue(ibProtocolName::Tab, object->GetMetaData()->GetTabId());
	return params;
}

// The description as a node carries it — a value of its own, or a child node, as the wire left it.
ibDataValue HomePageValue(const ibDataNode& node)
{
	const ibDataValue value = node.GetProperty(wxT("HomePage"));
	if (value.Kind() == ibDataKind::Child)
		return value;
	if (const ibDataNode* const child = node.FindChild(wxT("HomePage")))
		return ibDataValue::Child(std::make_shared<ibDataNode>(*child));
	return ibDataValue();
}

} // namespace

void ibValueMetaObjectConfiguration::ReadHomePage()
{
	m_homePage = ibHomePageDescription();
	m_formArray = ibDataNode();
	m_readOnly = false;

	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	ibProtocolNode answer;
	if (frame == nullptr || !frame->Call(ibProtocolMethod::Schema, HomePageParams(this), answer))
		return;

	ibDataNode shown;
	ibReadProtocolNode(answer, shown);
	m_homePage.ReadNode(HomePageValue(shown));
	m_readOnly = shown.GetValue<bool>(wxT("ReadOnly"));
	if (const ibDataNode* const forms = shown.FindChild(wxT("Forms")))
		m_formArray = *forms;
}

void ibValueMetaObjectConfiguration::SetHomePage(const ibHomePageDescription& homePage)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	ibDataValue value;
	if (frame == nullptr || !homePage.WriteNode(value))
		return;

	ibDataNode args;
	args.SetProperty(wxT("HomePage"), value);

	ibProtocolNode params = HomePageParams(this);
	params.SetValue(ibProtocolName::Command, static_cast<long long>(ibProtocolSchemaCommand::Save));
	ibWriteProtocolNode(args, params.Child(ibProtocolName::Args));
	ibProtocolNode answer;
	if (frame->Call(ibProtocolMethod::Schema, params, answer))
		m_homePage = homePage;
}
