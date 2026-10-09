#ifndef _FRMCLIENT_BACKEND_METADATA_CONFIGURATION_H__
#define _FRMCLIENT_BACKEND_METADATA_CONFIGURATION_H__

// THE CONFIGURATION — the engine's (backend/metadataConfiguration.h): the server's. The client holds none of it, and
// what asks for it is answered with none.
class ibMetaData;

namespace appEnv {
	inline ibMetaData* ActiveMetaData() { return nullptr; }
}

#endif
