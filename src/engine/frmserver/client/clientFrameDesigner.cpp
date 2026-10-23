#include "clientFrameDesigner.h"

#include "backend/appData.h"
#include "backend/debugger/debugClient.h"
#include "backend/mcp/mcpServer.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"

#include "core/picturePredefined.h"   // the assistant's item's picture

#include "frmserver/docView/docManagerDesigner.h"
#include "frmserver/schema/designer/mcpServer.h"   // SwitchableServer — the assistant the person may switch here

#include "clientDebugger.h"

ibClientFrameDesigner::ibClientFrameDesigner(ibSessionHolder&& holder, ibClientInstance* clientInstance)
	: ibClientFrame(std::move(holder), clientInstance), m_metaTree(this)
{
	m_docManager = new ibDocManagerDesigner;
	InitializeDefaultMenu();

	// The designer's debugger — the engine's, which the login made with the configuration — heard over the wire.
	ibSession* const session = GetSession();
	if (debugClient != nullptr && session != nullptr) {
		m_debugger = new ibClientDebugger(session->GetApplicationInstance(), clientInstance);
		debugClient->AddBridge(m_debugger);
	}
}

ibClientFrameDesigner::~ibClientFrameDesigner()
{
	// RemoveBridge destroys it — the adapter owns what it holds.
	if (m_debugger != nullptr && debugClient != nullptr)
		debugClient->RemoveBridge(m_debugger);
}

bool ibClientFrameDesigner::AllowRun()
{
	ibMetaDataConfigurationBase* const metaData = GetSession()->GetMetaData();
	if (metaData == nullptr || !metaData->AccessRight_Administration())
		return false;
	return m_metaTree.Load(std::static_pointer_cast<ibMetaDataConfigurationBase>(metaData->shared_from_this()));
}

void ibClientFrameDesigner::CreateStartupPage()
{
	// Nothing starts that the settings did not ask for, nor one running already: Start refuses a switched-off access on
	// its own, and that refusal is not worth saying at every start.
	ibMcpServer* const server = ibSchemaMcpServer::SwitchableServer(*GetSession());
	if (server == nullptr || !server->GetSettings().m_enabled || server->IsRunning())
		return;

	wxString refusal;
	if (!server->Start(GetSession(), refusal) && !refusal.IsEmpty())
		Message(refusal, ibStatusMessage::ibStatusMessage_Warning);
}

void ibClientFrameDesigner::OnDraw(ibDataNode& state)
{
	m_metaTree.OnDraw(state.Child(ibProtocol::ibFrame::MetaData));
}

void ibClientFrameDesigner::BackendError(const wxString& /*strFileName*/,
	const wxString& /*strDocPath*/, const long /*line*/,
	const wxString& strErrorMessage)
{
	// ⚠ Without its place yet: the desktop's line opened the module at the error (ibOutputWindow), a message here is its
	// text alone.
	Message(strErrorMessage, ibStatusMessage::ibStatusMessage_Error);
}

void ibClientFrameDesigner::InitializeDefaultMenu()
{
	// File and Edit are the client's doc manager's. The designer's items stand whatever the rights; the schema's own
	// right switches its item off (as drawn).
	ibClientMenu administration;
	administration.Append(ibProtocol::ibSchema::ibKind::UserList, _("Users"));
	administration.Append(ibProtocol::ibSchema::ibKind::ActiveUser, _("Active users"));
	administration.AppendSeparator();
	administration.Append(ibProtocol::ibSchema::ibKind::AuditLog, _("Registration journal"));
	administration.AppendSeparator();
	administration.Append(ibProtocol::ibSchema::ibKind::Database, ibProtocol::ibSchema::ibCommand::Load, _("Restore database"));
	administration.Append(ibProtocol::ibSchema::ibKind::Database, ibProtocol::ibSchema::ibCommand::Save, _("Dump database"));
	administration.AppendSeparator();
	administration.Append(ibProtocol::ibSchema::ibKind::Database, ibProtocol::ibSchema::ibCommand::Clear, _("Clear database"));
	administration.AppendSeparator();
	ibClientMenu::Item& assistant = administration.Append(ibProtocol::ibSchema::ibKind::Assistant, ibProtocol::ibSchema::ibCommand::Open,
		_("Assistant window"));
	assistant.picture = g_picAssistantCLSID;
	m_menu.AppendSubMenu(administration, _("Administration"));

	ibClientMenu tools;
	tools.Append(ibProtocol::ibSchema::ibKind::McpServer, _("Options..."));
	m_menu.AppendSubMenu(tools, _("Tools"));
}
