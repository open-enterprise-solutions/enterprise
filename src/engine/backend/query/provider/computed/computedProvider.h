#ifndef __COMPUTED_PROVIDER_H__
#define __COMPUTED_PROVIDER_H__

// The computed (RAM) provider — a source whose rows are computed in memory rather than scanned: a register's
// slice / balance / turnover, a temp table in memory, a subquery. The interface it answers is queryProvider.h's;
// what only metadata answers (where a reference points, the references a list made) it leaves to the interface's
// defaults, which ask the database provider — the references in its rows are the base's.

#include "backend/query/queryProvider.h"            // ibBackendQueryProvider — the interface it answers

// Computed virtual table provider — register slice / balance / turnover. Stateless:
// ExecuteRead computes the RAM table from the spec (no physical scan, no L2). Read-only
// (ExecuteWrite stays the base's false). A computed queryable vends the shared instance
// (ibComputedProviderInstance); the class is declared here (not file-local) so a provider that
// reads the same way can derive from it — the tests' fakes do.
class BACKEND_API ibComputedProvider : public ibBackendQueryProvider
{
public:
	virtual ibDataQueryResult ExecuteRead(const ibDataQuerySpec& spec, const ibReadPageRequest& page) override;
	// Aggregated read over a COMPUTED source — compute the rows, then the RAM GROUP-BY fold
	// (the base default would silently return the RAW rows). Enables SELECT SUM(x) FROM (subquery)
	// and aggregates over a register slice. HAVING is not folded on the RAM path (gated above).
	virtual ibDataQueryResult ExecuteAggregate(const ibDataQuerySpec& spec) override;
	// Every source it reads builds its rows itself (ComputeRows) — no statement is written.
	virtual bool IsComputedInRam() const override { return true; }
};

#endif
