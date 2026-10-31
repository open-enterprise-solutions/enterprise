#ifndef _FRMCLIENT_BACKEND_META_RESOURCE_OBJECT_H__
#define _FRMCLIENT_BACKEND_META_RESOURCE_OBJECT_H__

#include <wx/icon.h>

// A RESOURCE OF THE CONFIGURATION — the engine's ibValueMetaObjectResource (backend/metaCollection/resource/
// metaResourceObject.h): on the client, only its class picture, which a settings window puts on a field declared a
// resource.
class ibValueMetaObjectResource {
public:
	static wxIcon GetIconGroup();
};

#endif
