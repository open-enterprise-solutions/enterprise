#ifndef _FRMCLIENT_BACKEND_QUERY_QUERYABLE_H__
#define _FRMCLIENT_BACKEND_QUERY_QUERYABLE_H__

// THE DESKTOP'S SCOPE, UNDER ITS OWN NAME (backend/query/queryable.h) — there it sets the configuration a query's
// names resolve against for the duration of one question. On the client the configuration is the server's, and
// the configuration a question is for travels with the question (ibQueryConstructorModel's m_metaData). So the
// scope here holds nothing: the windows that open one, as the desktop's do, change nothing by it.

#include "frmclient/backend/backend_core.h"   // FRMCLIENT_API
#include "core/query/queryPeriodUnits.h"       // ibPeriodUnits — the desktop's header brings them, and the windows read them

class ibMetaData;

class FRMCLIENT_API ibSourceMetaDataScope
{
public:
	explicit ibSourceMetaDataScope(const ibMetaData* /*metaData*/) {}

	ibSourceMetaDataScope(const ibSourceMetaDataScope&) = delete;
	ibSourceMetaDataScope& operator=(const ibSourceMetaDataScope&) = delete;
};

#endif
