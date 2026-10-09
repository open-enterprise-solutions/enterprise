#ifndef _FRMCLIENT_BACKEND_META_OBJECT_H__
#define _FRMCLIENT_BACKEND_META_OBJECT_H__

#include "frmclient/backend/propertyManager/propertyObject.h"   // a metaobject IS a property object, as the engine's

// A METAOBJECT OF THE CONFIGURATION — the engine's ibValueMetaObject (backend/metaCollection/metaObject.h): the
// server's. A client document has none; asked what it is, it is nothing — and none is ever listed (ibMetaData).
class ibValueMetaObject : public ibPropertyObject {
public:
	template <typename T>
	T* ConvertToType() const { return nullptr; }

	ibMetaID GetMetaID() const { return 0; }
	wxString GetName() const { return wxString(); }
	wxString GetSynonym() const { return wxString(); }
	wxBitmap GetIcon() const { return wxNullBitmap; }
};

#endif
