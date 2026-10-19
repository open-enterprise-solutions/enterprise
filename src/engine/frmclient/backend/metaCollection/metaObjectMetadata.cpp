#include "metaObjectMetadata.h"

#include "frmclient/backend/serialize/dataProtocol.h"   // ibReadProtocolNode, ibWriteProtocolNode — the answer, the edit
#include "frmclient/mainFrame/mainFrame.h"              // the door to the server
#include "protocol/protocol.h"                          // ibProtocol::ibSchema::ibKind::HomePage — the editor the server holds
#include "protocol/protocolNode.h"

// The workspace editor's — done by the server's (ibProtocol::ibSchema::ibKind::HomePage), the root of this id in its tree's tab.

namespace {

ibProtocolNode HomePageParams(const ibValueMetaObject* object)
{
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::HomePage));
	ibProtocolNode args = params.Child(ibProtocol::ibSchema::Args);
	args.SetValue(ibProtocol::ibSchema::ibHomePage::Id, static_cast<long long>(object->GetMetaID()));
	args.SetValue(ibProtocol::ibSchema::ibHomePage::Tab, object->GetMetaData()->GetTabId());
	return params;
}

// The description as a node carries it — a value of its own, or a child node, as the wire left it.
ibDataValue HomePageValue(const ibDataNode& node)
{
	const ibDataValue value = node.GetProperty(ibProtocol::ibSchema::ibHomePage::HomePage);
	if (value.Kind() == ibDataKind::Child)
		return value;
	if (const ibDataNode* const child = node.FindChild(ibProtocol::ibSchema::ibHomePage::HomePage))
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
	if (frame == nullptr || !frame->Call(ibProtocol::ibMethod::Schema, HomePageParams(this), answer))
		return;

	ibDataNode shown;
	ibReadProtocolNode(answer, shown);
	m_homePage.ReadNode(HomePageValue(shown));
	m_readOnly = shown.GetValue<bool>(ibProtocol::ibSchema::ibHomePage::ReadOnly);
	if (const ibDataNode* const forms = shown.FindChild(ibProtocol::ibSchema::ibHomePage::Forms))
		m_formArray = *forms;
}

void ibValueMetaObjectConfiguration::SetHomePage(const ibHomePageDescription& homePage)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	ibDataValue value;
	if (frame == nullptr || !homePage.WriteNode(value))
		return;

	ibDataNode args;
	args.SetProperty(ibProtocol::ibSchema::ibHomePage::HomePage, value);

	ibProtocolNode params = HomePageParams(this);
	params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(ibProtocol::ibSchema::ibCommand::Save));
	ibWriteProtocolNode(args, params.Child(ibProtocol::ibSchema::Args));
	ibProtocolNode answer;
	if (frame->Call(ibProtocol::ibMethod::Schema, params, answer))
		m_homePage = homePage;
}
