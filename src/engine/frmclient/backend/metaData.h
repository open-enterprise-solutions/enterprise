#ifndef _FRMCLIENT_BACKEND_META_DATA_H__
#define _FRMCLIENT_BACKEND_META_DATA_H__

#include <initializer_list>
#include <vector>

#include "frmclient/backend/metaCollection/metaObject.h"

// THE CONFIGURATION — the engine's ibMetaData: the server's. The client's objects have none (GetMetaData answers
// null), so nothing here is ever reached; what a configuration lists, it lists on the server.
class ibMetaData {
public:
	void GetOwner(ibMetaData*& owner) const { owner = nullptr; }

	std::vector<const ibValueMetaObject*> GetAnyArrayObject(const std::initializer_list<ibClassID> WXUNUSED(classes)) const { return {}; }
	template <class T>
	std::vector<const T*> GetAnyArrayObject(const ibClassID& WXUNUSED(clsid)) const { return {}; }
};

#endif
