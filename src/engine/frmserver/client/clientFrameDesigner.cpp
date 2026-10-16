#include "clientFrameDesigner.h"

#include "backend/appData.h"
#include "backend/mcp/mcpServer.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"

#include "core/picturePredefined.h"   // the assistant's item's picture

#include "frmserver/docView/docManagerDesigner.h"
#include "frmserver/schema/designer/mcpServer.h"   // SwitchableServer — the assistant the person may switch here

ibClientFrameDesigner::ibClientFrameDesigner(ibSessionHolder&& holder, ibClientInstance* clientInstance)
	: ibClientFrame(std::move(holder), clientInstance), m_metaTree(this)
{
	m_docManager = new ibDocManagerDesigner;
	InitializeDefaultMenu();
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
	m_metaTree.OnDraw(state.Child(wxT("MetaData")));
}

void ibClientFrameDesigner::BackendError(const wxString& WXUNUSED(strFileName),
	const wxString& WXUNUSED(strDocPath), const long WXUNUSED(line),
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
	administration.Append(ibProtocolSchema::UserList, _("Users"));
	administration.Append(ibProtocolSchema::ActiveUser, _("Active users"));
	administration.AppendSeparator();
	administration.Append(ibProtocolSchema::AuditLog, _("Registration journal"));
	administration.AppendSeparator();
	administration.Append(ibProtocolSchema::Database, ibProtocolSchemaCommand::Load, _("Restore database"));
	administration.Append(ibProtocolSchema::Database, ibProtocolSchemaCommand::Save, _("Dump database"));
	administration.AppendSeparator();
	administration.Append(ibProtocolSchema::Database, ibProtocolSchemaCommand::Clear, _("Clear database"));
	administration.AppendSeparator();
	ibClientMenu::Item& assistant = administration.Append(ibProtocolSchema::Assistant, ibProtocolSchemaCommand::Open,
		_("Assistant window"));
	assistant.picture = g_picAssistantCLSID;
	m_menu.AppendSubMenu(administration, _("Administration"));

	ibClientMenu tools;
	tools.Append(ibProtocolSchema::McpServer, _("Options..."));
	m_menu.AppendSubMenu(tools, _("Tools"));
}
