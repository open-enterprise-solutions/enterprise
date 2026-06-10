#ifndef __QUERYABLE_HOOKS_H__
#define __QUERYABLE_HOOKS_H__

// Light registration seam for the L4 query-source factory. A metaobject registers its
// OWN descriptor field (m_queryable) with the factory on run and unregisters it on close —
// the family checks the onlyLoadFlag BEFORE calling register. These two free functions do
// the appData / factory lookup + Register/Unregister inside queryableFactory.cpp, so the
// metadata side (commonObject.cpp / constantMetadata.cpp / the concrete registers) pulls
// only THIS light header — not appData.h or the full factory — at the registration site.

#include "backend/backend.h"   // BACKEND_API

class ibQueryableSourceDescriptor;

BACKEND_API void ibRegisterQueryableSource(ibQueryableSourceDescriptor* descriptor);
BACKEND_API void ibUnregisterQueryableSource(ibQueryableSourceDescriptor* descriptor);

#endif
