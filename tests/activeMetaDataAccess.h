// A test's configuration, made in memory, run as the base's active one — what a tool that asks the base for its
// configuration (an MCP tool by a metadata id) then finds. The base itself only ever runs what its database
// publishes (ibApplicationInstance::ReplaceActiveMetaData()); this is the step after the read, reached by a friend.

#ifndef _IB_TEST_ACTIVE_METADATA_ACCESS_H_
#define _IB_TEST_ACTIVE_METADATA_ACCESS_H_

#include "backend/appData.h"

struct ActiveMetaDataAccess {
	static void Replace(std::shared_ptr<class ibMetaDataConfigurationBase> metaData)
	{
		ibApplicationInstance::Get()->ReplaceActiveMetaData(std::move(metaData));
	}
};

#endif
