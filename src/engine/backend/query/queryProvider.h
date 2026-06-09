#ifndef __QUERY_PROVIDER_H__
#define __QUERY_PROVIDER_H__

// ibBackendQueryProvider — the L3 execution ENGINE for one source (a relation). The
// door (ibDataQueryBuilder) is the metadata SURFACE and is L2-BLIND; the provider is
// the engine that realizes a source and holds ALL the L2 + the metadata->physical
// lowering:
//   - ibDbTableProvider  — lowers to L2 over the real table (read / cached / aggregate
//                          / write); file-local in queryProvider.cpp;
//   - ibComputedProvider — computes a RAM virtual table (register slice / balance /
//                          turnover); declared here so a computed queryable's
//                          GetProvider() override can vend a static of it.
// The queryable VENDS the provider (ibBackendQueryable::GetProvider()); the door reads
// the whole query through THIS interface, passing the accumulated ibDataQuerySpec and
// never naming a concrete provider or an L2 type. The providers are STATELESS (the spec
// carries all state), so a queryable hands back a shared static instance.
// See docs/query-language-arc.md §22.

#include "dataQueryBuilder.h"   // ibDataQueryResult / ibReadPageRequest / ibDataQuerySpec / ibRenderedPageCache (all L2-free)

// (ibDbTableProvider — the BIG DB provider + its static GET/WRITE template — lives in its own
// L2-coupled dbTableProvider.h; this header stays deliberately L2-free.)

class BACKEND_API ibBackendQueryProvider
{
public:
	virtual ~ibBackendQueryProvider() = default;

	// Read a page of the source described by `spec`. The provider owns HOW (real
	// SELECT / computed RAM / temp) — the door never sees it.
	virtual ibDataQueryResult ExecuteRead(const ibDataQuerySpec& spec, const ibReadPageRequest& page) = 0;

	// Build-once cached read (a physical-scan optimisation): render the page SQL once
	// per signature, rebind the anchor per tick. Non-DB providers fall back to a plain
	// read (the default).
	virtual ibDataQueryResult ExecuteReadCached(const ibDataQuerySpec& spec, const ibReadPageRequest& page,
	                                            ibRenderedPageCache& /*cache*/, const wxString& /*signature*/)
	{
		return ExecuteRead(spec, page);
	}

	// Aggregated read (ИТОГИ / totals) — GROUP BY built from the spec's groupBy /
	// aggregates / having. NOT paged. Non-DB providers fall back to a plain read.
	virtual ibDataQueryResult ExecuteAggregate(const ibDataQuerySpec& spec)
	{
		return ExecuteRead(spec, ibReadPageRequest{});
	}

	// Write from the spec: INSERT/UPSERT the SetValue() assignments (UPSERT matches on the
	// queryable's uniqueness key — GetPrimaryKeyColumns); DELETE by the Where() conditions
	// (a null-col condition keys off the uuid identity column). Only the DB-table provider
	// implements it; computed/temp is read-only.
	virtual bool ExecuteWrite(const ibDataQuerySpec& /*spec*/, ibDataQueryBuilder::WriteKind /*kind*/)
	{
		return false;
	}
};

// Computed virtual table provider — register slice / balance / turnover. Stateless:
// ExecuteRead computes the RAM table from the spec (no physical scan, no L2). Read-only
// (ExecuteWrite stays the base's false). Declared here (not file-local) so a computed
// queryable's GetProvider() override can return a static instance of it.
class BACKEND_API ibComputedProvider : public ibBackendQueryProvider
{
public:
	ibDataQueryResult ExecuteRead(const ibDataQuerySpec& spec, const ibReadPageRequest& page) override;
};

// ==========================================================================
// ibQueryComposer — the L3 ENGINE over the relational TREE (ibQueryNode). The door is
// blind to single-vs-multi source: it hands the composer the spec and the composer
// realizes the tree —
//   * a single Source leaf  -> delegate to that source's GetProvider() (today's path);
//   * a Join / Union of leaves -> co-locate into ONE SQL where all leaves are SQL-able
//     on one connection, else push down per leaf + materialise the rest + stitch.
// The door calls THIS, never a concrete provider — so adding multi-source never
// reshapes the door. (docs/query-language-arc.md §22.1)
// ==========================================================================
class BACKEND_API ibQueryComposer
{
public:
	static ibDataQueryResult ExecuteRead(const ibDataQuerySpec& spec, const ibReadPageRequest& page);
	static ibDataQueryResult ExecuteReadCached(const ibDataQuerySpec& spec, const ibReadPageRequest& page,
	                                           ibRenderedPageCache& cache, const wxString& signature);
	static ibDataQueryResult ExecuteAggregate(const ibDataQuerySpec& spec);
	static ibQueryRamTable   ExecuteTotals(const ibDataQuerySpec& spec);   // ИТОГИ — raw hierarchical totals tree
	// The ИТОГИ fold in isolation: detail rows -> subtotal tree. Pure (no DB) — unit-testable.
	static ibQueryRamTable   BuildTotalsTree(const ibQueryRamTable& detail,
	                                         const std::vector<const ibBackendQueryColumn*>& groupFields,
	                                         const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates);
	// Inner-join two materialised tables on (onLeft == onRight), emitting outCols (each
	// tagged by side in fromLeft). The RAM JOIN core, pure (no DB) — unit-testable.
	static ibQueryRamTable   JoinRamTables(const ibQueryRamTable& left, const ibQueryRamTable& right,
	                                       const ibBackendQueryColumn* onLeft, const ibBackendQueryColumn* onRight,
	                                       const std::vector<const ibBackendQueryColumn*>& outCols,
	                                       const std::vector<bool>& fromLeft);
	// Append one UNION branch's rows to `out`: each output column is read from the branch's
	// same-named column (branchCols, by position; null = absent -> NULL cell). The RAM UNION
	// stacking core, pure (no DB) — unit-testable.
	static void              AppendUnionBranch(ibQueryRamTable& out, const ibQueryRamTable& branch,
	                                           const std::vector<const ibBackendQueryColumn*>& outCols,
	                                           const std::vector<const ibBackendQueryColumn*>& branchCols);
	static bool ExecuteWrite(const ibDataQuerySpec& spec, ibDataQueryBuilder::WriteKind kind);
};

#endif
