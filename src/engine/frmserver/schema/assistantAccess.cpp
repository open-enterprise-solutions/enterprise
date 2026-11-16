#include "assistantAccess.h"

#include "backend/metadataConfiguration.h"   // AccessRight_Assistant
#include "frmserver/client/clientHost.h"
#include "frmserver/mcp/mcpServer.h"   // IsRunning — Running
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"

bool ibSchemaMcpServer::AccessRight(const ibSession& session) const
{
	if (IsDesignerSessionKind(session.GetKind()))
		return true;

	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_Assistant();
}

void ibSchemaMcpServer::Build(const ibSession& session, const ibDataNode& /*args*/, ibDataNode& result) const
{
	const ibMcpServer* const server = ibClientHost::GetMcpServer(&session);
	result.SetValue(ibProtocol::ibSchema::ibMcpServer::Allowed, server != nullptr);
	result.SetValue(ibProtocol::ibSchema::ibMcpServer::Running, server != nullptr && server->IsRunning());
}
