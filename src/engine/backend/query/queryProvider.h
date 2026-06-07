#ifndef __QUERY_PROVIDER_H__
#define __QUERY_PROVIDER_H__

// ibBackendQueryProvider — the L3 execution engine for ONE source (a relation).
// The querybuilder (ibDataQueryBuilder) is the metadata SURFACE; the provider is
// the ENGINE that realizes a source and yields the L3 selection:
//   - DB provider      — lowers to L2 over the real table (today's path);
//   - virtual provider — computes (register balances / turnovers / slices) — later;
//   - temp/RAM provider — reads a materialised set — later.
// L2 stays UNDER the providers. The querybuilder composes provider-relations by
// column; see docs/query-language-arc.md §22.
//
// A provider is created per query from the querybuilder's state, holds its holder,
// and dies with the ibDataQueryResult it opened (RAII). This header includes
// dataQueryBuilder.h for ibDataQueryResult / ibReadPageRequest — there is no cycle,
// the querybuilder header does not include this one.

#include "dataQueryBuilder.h"

class BACKEND_API ibBackendQueryProvider
{
public:
	virtual ~ibBackendQueryProvider() = default;

	// Run the accumulated read against this source and open the L3 selection. The
	// provider owns HOW (real SELECT / virtual compute / temp read) — the surface
	// never sees it.
	virtual ibDataQueryResult ExecuteRead(const ibReadPageRequest& page) = 0;

	// Write the row keyed by `rowKey` (Upsert from the accumulated SetValue()s, or
	// Delete). Only a writable source implements it — the DB provider; virtual / temp
	// providers leave the default (a virtual table is read-only). The provider owns
	// the field decomposition + the dialect UPSERT/DELETE.
	virtual bool ExecuteWrite(ibDataQueryBuilder::WriteKind kind, const ibGuid& rowKey) { return false; }
};

#endif
