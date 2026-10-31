#ifndef __SCHEMA_ASSISTANT_ACCESS_H__
#define __SCHEMA_ASSISTANT_ACCESS_H__

// THE ASSISTANT ACCESS — whether the person may have it. Their client does the listening and keeps its settings on their
// machine (frmclient/backend/mcp/mcpServer.h), and their server answers it (ibClientHost::GetMcpServer).

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaMcpServer : public ibClientSchema {
public:

	// ⭐ WHO THE ASSISTANT IS THERE FOR: whoever is in the designer; in the application, whose roles give it
	// (AccessRight_Assistant). The right a menu item asks and the right a channel to it asks (ibClientHost::Mcp).
	virtual bool AccessRight(const ibSession& session) const override;

	// Allowed.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;
};

#endif
