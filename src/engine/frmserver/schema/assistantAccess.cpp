#include "assistantAccess.h"

#include "backend/metadataConfiguration.h"   // AccessRight_Assistant
#include "frmserver/client/clientHost.h"
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
	result.SetValue(ibProtocol::ibSchema::ibMcpServer::Allowed, ibClientHost::GetMcpServer(&session) != nullptr);
}
