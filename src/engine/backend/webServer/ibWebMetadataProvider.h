////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : Serialise OES metadata tree to JSON for the REST API
////////////////////////////////////////////////////////////////////////////

#ifndef _IB_WEB_METADATA_PROVIDER_H__
#define _IB_WEB_METADATA_PROVIDER_H__

#include "backend/backend.h"

// Forward-declare nlohmann::json so callers do not need the full header
namespace nlohmann { class json; }

class BACKEND_API ibWebMetadataProvider {
public:

	// Full metadata tree: all catalogs, documents, enumerations, registers…
	// Returns a json object — caller must include <json.hpp> to use the result.
	static nlohmann::json SerializeMetadataTree();

private:

	// Per-section helpers — each returns a json array of serialised objects
	static nlohmann::json SerializeCatalogs();
	static nlohmann::json SerializeDocuments();
	static nlohmann::json SerializeEnumerations();
	static nlohmann::json SerializeConstants();
	static nlohmann::json SerializeInformationRegisters();
	static nlohmann::json SerializeAccumulationRegisters();
	static nlohmann::json SerializeDataProcessors();
	static nlohmann::json SerializeReports();
};

#endif // _IB_WEB_METADATA_PROVIDER_H__
