#include "docManagerDesigner.h"

#include "frmserver/schema/designer/mcpServer.h"
#include "frmserver/schema/designer/userList.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibDocManagerDesigner, ibDocManager);

ibDocManagerDesigner::ibDocManagerDesigner()
	: ibDocManager()
{
	// The designer's Administration → Users — the base's users.
	RegisterSchema(ibProtocolSchema::UserList, std::make_unique<ibSchemaUserList>());
	// The designer's options — the person's assistant access (its MCP page).
	RegisterSchema(ibProtocolSchema::McpServer, std::make_unique<ibSchemaMcpServer>());
}
