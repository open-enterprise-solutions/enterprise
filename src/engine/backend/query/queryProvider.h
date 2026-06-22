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
#include "querySelectorTree.h"  // ibSelectorTree — the folded node tree the totals/hierarchy entries return

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

	// Aggregated read (totals / totals) — GROUP BY built from the spec's groupBy /
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
	// Aggregated read over a COMPUTED source — compute the rows, then the RAM GROUP-BY fold
	// (the base default would silently return the RAW rows). Enables SELECT SUM(x) FROM (subquery)
	// and aggregates over a register slice. HAVING is not folded on the RAM path (gated above).
	ibDataQueryResult ExecuteAggregate(const ibDataQuerySpec& spec) override;
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
	static ibSelectorTree    ExecuteTotals(const ibDataQuerySpec& spec);   // totals — raw hierarchical totals tree
	// The totals fold in isolation: detail SNAPSHOT -> subtotal tree. Pure (no DB) — unit-testable.
	static ibSelectorTree    BuildTotalsTree(const ibQueryRamTable& detail,
	                                         const std::vector<const ibBackendQueryColumn*>& groupFields,
	                                         const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates);
	// RECURSIVE hierarchy fold (parent-ref, unbounded depth) — the sibling of BuildTotalsTree (fixed
	// columns). From a WHOLE materialised detail SNAPSHOT: nests rows by parentKey == rowKey, sets each
	// node's m_hasChildren from the data (its key appears as some row's parent), and rolls a subtotal
	// over each node's subtree (correct for COUNT/AVG — aggregated from the raw leaves). `mode` is the
	// placement: Elements (leaves only) / Hierarchy (folders + leaves) / HierarchyOnly (folders only).
	// Pure (no DB) — unit-testable. (docs §22.1b)
	static ibSelectorTree    BuildHierarchyTree(const ibQueryRamTable& detail,
	                                             const ibBackendQueryColumn* rowKeyCol,
	                                             const ibBackendQueryColumn* parentKeyCol,
	                                             const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates,
	                                             ibDimensionKind mode);
	// Cross-catalog reference-dimension hierarchy: group the snapshot rows by `refCol`'s VALUE, then
	// arrange the value groups into the TARGET catalog's parent-ref hierarchy. The parent of each value
	// lives in the target catalog (not the snapshot) → materialised through the door from `source`'s
	// ResolveReferenceTarget(refCol). Falls back to a flat group when no target / no holder. (docs §22.1b)
	static ibSelectorTree    BuildReferenceHierarchy(const ibQueryRamTable& snapshot,
	                                                  const ibBackendQueryColumn* refCol,
	                                                  const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates,
	                                                  ibDatabaseConnectionHolder* holder,
	                                                  const ibBackendQueryable* source,
	                                                  ibDimensionKind dim);
	// GENERAL combiner: fold the snapshot by N TotalBy levels IN ORDER. Each level groups its rows by
	// the level field; a Hierarchy/HierarchyOnly level also unfolds the field's reference hierarchy
	// (target catalog parent-map, materialised through the door), and the NEXT level recurses inside
	// each value's own rows. Subtotals roll in-place at every node. Value-keyed (each level groups by
	// VALUE) — the catalog's OWN row-keyed hierarchy stays BuildHierarchyTree. (docs §22.1b)
	static ibSelectorTree    BuildDimensionTree(const ibQueryRamTable& snapshot,
	                                             const std::vector<ibTotalLevel>& levels,
	                                             const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates,
	                                             ibDatabaseConnectionHolder* holder,
	                                             const ibBackendQueryable* source);
	// Inner-join two materialised tables on (onLeft == onRight), emitting outCols (each
	// tagged by side in fromLeft). The RAM JOIN core, pure (no DB) — unit-testable.
	static ibQueryRamTable   JoinRamTables(const ibQueryRamTable& left, const ibQueryRamTable& right,
	                                       const ibBackendQueryColumn* onLeft, const ibBackendQueryColumn* onRight,
	                                       const std::vector<const ibBackendQueryColumn*>& outCols,
	                                       const std::vector<bool>& fromLeft,
	                                       ibQueryJoinKind kind = ibQueryJoinKind::Inner);   // Inner / Left / Right / Full
	// Greedy smallest-first join ORDER for a flattened pure-INNER chain. Inputs: each
	// unit's EXACT materialised row count + the join edges (unit-index pairs, one per
	// inner Join node). order[0] starts; every next unit is edge-connected to the
	// joined prefix; among the connected candidates the smallest joins first — the
	// cost-based decision with REAL costs (no estimator: the units are materialised
	// anyway). Empty result = no connected order (the caller keeps the tree order).
	// Inner joins commute/associate, so any connected order is semantics-preserving.
	// Pure (no DB) — unit-testable. (docs/temp-db.md §7 — the same size lever)
	static std::vector<size_t> PlanInnerJoinOrder(const std::vector<long>& rowCounts,
	                                              const std::vector<std::pair<size_t, size_t>>& edges);
	// Append one UNION branch's rows to `out`: each output column is read from the branch's
	// same-named column (branchCols, by position; null = absent -> NULL cell). The RAM UNION
	// stacking core, pure (no DB) — unit-testable.
	static void              AppendUnionBranch(ibQueryRamTable& out, const ibQueryRamTable& branch,
	                                           const std::vector<const ibBackendQueryColumn*>& outCols,
	                                           const std::vector<const ibBackendQueryColumn*>& branchCols);
	// Drop duplicate rows, keyed by the IDENTITY hash (GetHashKey) of every `cols` cell — the RAM
	// dedup core behind plain UNION (SQL semantics: dedupe the accumulated rows at each non-ALL
	// operator) and a future RAM DISTINCT. First occurrence wins, order preserved. Pure — unit-testable.
	static ibQueryRamTable   DedupeRows(const ibQueryRamTable& src,
	                                    const std::vector<const ibBackendQueryColumn*>& cols);
	// Keep only the rows of `src` that satisfy the boolean WHERE TREE (And/Or/Not/IsNull/Leaf) — the
	// post-compose RAM filter for an OR / IS NULL spanning a non-co-located JOIN. Pure — unit-testable.
	static ibQueryRamTable   FilterRows(const ibQueryRamTable& src, const ibQueryPredicate* predicate);
	// Compare one ORDER BY key for the RAM sort floor: NULL = smallest (SQLite / MySQL convention)
	// -> NULLS FIRST on ASC, NULLS LAST on DESC, deterministically. <0: a before b; >0: b before a;
	// 0: equal on this key. Pure — unit-testable; shared by both RAM ORDER BY comparators.
	static int               RamSortCompareKey(const ibValue& a, const ibValue& b, bool ascending);
	// Evaluate a computed output column (Column / Const / Arith / Case) against one composed row — the
	// per-row eval for a computed column over a JOIN (single source pushes it to SQL). Pure — unit-testable.
	static ibValue           EvalColumnExpr(const ibQueryColumnExpr* expr, const ibQueryRamTable& table, long row);
	static bool ExecuteWrite(const ibDataQuerySpec& spec, ibDataQueryBuilder::WriteKind kind);
};

#endif
