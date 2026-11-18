#ifndef __COMPUTED_PROVIDER_H__
#define __COMPUTED_PROVIDER_H__

// The computed provider — a source whose rows are computed in memory rather than scanned: a register's slice / balance
// / turnover, a temp table in memory, a subquery. It PRODUCES the rows (the source's ComputeRows) and nothing else:
// what a read does to them — the filter, the order, the page, the fold — is a statement's, run in a base in memory by the
// same translation as on a server (ibQueryComposer::OverMemorySpace, query-one-road.md §7). What only metadata answers
// (where a reference points, the references a list made) it leaves to the interface's defaults, which ask the database
// provider — the references in its rows are the base's.

#include "backend/query/queryProvider.h"            // ibBackendQueryProvider — the interface it answers

// Stateless. Read-only (ExecuteWrite stays the base's false). A computed queryable vends the shared instance
// (ibComputedProviderInstance); the class is declared here (not file-local) so a provider that reads the same way can
// derive from it — the tests' fakes do.
class BACKEND_API ibComputedProvider : public ibBackendQueryProvider
{
public:
	// A read and a fold over a computed source — its rows copied into a base in memory and read there as one statement
	// (the base default would hand back the raw rows, the fold silently dropped).
	virtual ibDataQueryResult ExecuteRead(const ibDataQuerySpec& spec, const ibReadPageRequest& page) override;
	virtual ibDataQueryResult ExecuteAggregate(const ibDataQuerySpec& spec) override;
	// Every source it reads builds its rows itself (ComputeRows) — no statement of the base is written over it.
	virtual bool IsComputedInRam() const override { return true; }
};

#endif
