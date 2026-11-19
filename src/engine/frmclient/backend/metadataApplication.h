#ifndef _FRMCLIENT_BACKEND_METADATA_APPLICATION_H__
#define _FRMCLIENT_BACKEND_METADATA_APPLICATION_H__

// THE APPLICATION — the engine's (backend/metadataApplication.h): the server's. The client holds none of it, and
// what asks for it is answered with none.
class ibMetaData;

namespace appEnv {
	inline ibMetaData* ActiveMetaData() { return nullptr; }
}

#endif
