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
#include "queryRowCursor.h"     // ibQueryRowCursor — what the folds READ (one pass over the detail rows)

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

	// ⭐⭐ THE SAME AGGREGATE, AS A RELATION INSTEAD OF ROWS.
	//
	// `ExecuteAggregate` builds the whole GROUP BY and then RUNS it; this stops one step earlier and
	// hands back what it built. The difference is not where the sum is computed — that is the server
	// either way — it is whether the result has to be MATERIALISED before anything else can be
	// composed with it. Returning a relation is what lets a reading answer `GetSourceRelation`, so a
	// join to it, a filter over it and paging stay one SQL statement.
	//
	// Null (the default, and every non-DB provider) = this source cannot be composed with; the caller
	// falls back to reading rows, which answers the same numbers.
	virtual ibQueryRelPtr BuildAggregateRelation(const ibDataQuerySpec& /*spec*/)
	{
		return nullptr;
	}

	// The same, for a query that PROJECTS rather than folds — a listing of lines. Separate from the
	// one above because the two are different lowerings (a GROUP BY and a paged read), not one with a
	// flag; the door picks by the shape of the query it was given.
	virtual ibQueryRelPtr BuildReadRelation(const ibDataQuerySpec& /*spec*/)
	{
		return nullptr;
	}

	// Write from the spec: INSERT/UPSERT the SetValue() assignments (UPSERT matches on the
	// queryable's uniqueness key — GetPrimaryKeyColumns); DELETE by the Where() conditions
	// (a null-col condition keys off the uuid identity column). Only the DB-table provider
	// implements it; computed/temp is read-only.
	// Returns the number of rows written / deleted (>= 0), or -1 when the source is not writable
	// (computed / temp) or the write threw. A restricted DELETE that matches 0 rows returns 0 — the
	// caller reads that (under an active policy) as "no accessible row -> access denied".
	virtual long ExecuteWrite(const ibDataQuerySpec& /*spec*/, ibDataQueryBuilder::WriteKind /*kind*/)
	{
		return -1;
	}

	// Resolve a reference COLUMN of `queryable` to the queryable it points at — the dot-walk join
	// target. METADATA-BACKED, so the resolution lives in the ONE provider that owns metadata (the
	// DB provider: clsid -> GetTypeCtor -> holder -> GetQueryable, read off queryable->GetMetaData);
	// the computed provider FORWARDS to it, every other provider inherits null. This query-provider
	// layer names NO metadata — only the signature. (docs/query-language-arc.md §22 dot-walk)
	virtual const ibBackendQueryable* ResolveReferenceTarget(const ibBackendQueryable* /*queryable*/,
	                                                         const ibBackendQueryColumn* /*refColumn*/) const { return nullptr; }
	// ALL reference targets of a column — N for a COMPOSITE (multi-type) reference, 1 for a single
	// reference, empty for a non-reference. Default wraps the single-target resolver. (docs §22)
	virtual std::vector<const ibBackendQueryable*> ResolveReferenceTargets(const ibBackendQueryable* queryable,
	                                                                        const ibBackendQueryColumn* refColumn) const {
		const ibBackendQueryable* one = ResolveReferenceTarget(queryable, refColumn);
		return one != nullptr ? std::vector<const ibBackendQueryable*>{ one }
		                      : std::vector<const ibBackendQueryable*>{};
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
	// Reference dot-walk resolution over a COMPUTED source — FORWARDS to the DB provider (the one
	// metadata owner); this layer names no metadata. Makes Balance.Item.Name resolve on the RAM path.
	const ibBackendQueryable* ResolveReferenceTarget(const ibBackendQueryable* queryable, const ibBackendQueryColumn* refColumn) const override;
	std::vector<const ibBackendQueryable*> ResolveReferenceTargets(const ibBackendQueryable* queryable, const ibBackendQueryColumn* refColumn) const override;
};

// ⭐ ONE FOLD'S WHOLE ORDER — the levels it groups by and the figures it rolls, which is everything
// that distinguishes one output of a composition from another once they read the same rows.
//
// It holds them BY VALUE rather than pointing at the caller's: the folds outlive the loop that
// assembles them, and a fold reading a level list that has since been rebuilt is the kind of defect
// that shows up as a wrong number rather than a crash. Copying is a handful of pointers per level.
struct ibFoldRequest
{
	std::vector<ibTotalLevel>                      m_levels;
	std::vector<ibDataQueryBuilder::AggregateItem> m_aggregates;
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
	// Paged single-level group read (door SelectAggregatePage): server-side GROUP BY + keyset + LIMIT when the
	// shape is pageable (ibDbTableProvider::CanPageGroupLevel), else the unpaged ExecuteAggregate (all groups).
	static ibDataQueryResult ExecuteGroupLevelPage(const ibDataQuerySpec& spec, const ibReadPageRequest& page);
	static ibSelectorTree    ExecuteTotals(const ibDataQuerySpec& spec);   // totals — raw hierarchical totals tree
	// The same push-downs ExecuteTotals uses, asked as a QUESTION: true = the DBMS folded the levels
	// and `out` is the tree; false = nothing was read, fold it yourself. This is what lets the query
	// lowering — and therefore every composition — stop reading detail rows to add them up here.
	static bool              TryFoldTotalsInDbms(const ibDataQuerySpec& spec, ibSelectorTree& out);
	// ⭐ CAN A STATEMENT DECLARE A NAMED QUERY HERE — `WITH <name> AS (…)`? Asked by the LOWERING
	// before it chooses how a named result travels: declared, the server reads it itself and the
	// join is the server's; otherwise its rows come back and the join is ours. Asked THROUGH the
	// composer for the same reason the fold is — this is an L2 fact, and the tier that owns the
	// answer answers it, so the lowering never names a dialect.
	static bool              CanDeclareNamedQuery(ibDatabaseConnectionHolder* holder = nullptr);
	// ⭐ The totals fold in isolation: detail ROWS -> subtotal tree, in ONE PASS. It takes a CURSOR
	// because that is all a fold ever reads — every row once, in arrival order — and taking the whole
	// materialised detail instead is what made a report's memory a function of the number of ROWS.
	// Now it is a function of the number of GROUPS (docs/data-composer.md, the acceptance criterion).
	// Pure (no DB) — unit-testable. The snapshot overload is the same call with the table handed over
	// as a cursor (ibRamTableCursor), for the callers that already hold their rows.
	static ibSelectorTree    BuildTotalsTree(ibQueryRowCursor& rows,
	                                         const std::vector<const ibBackendQueryColumn*>& groupFields,
	                                         const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates);
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
	// ⭐ …AND THE SAME ONE PASS. A level that unfolds a reference HIERARCHY is the exception and says
	// so: its shape is not known until every value has been seen, so that fold drains the cursor into
	// a table (and journals that it did). Everything else — the ordinary report — streams.
	static ibSelectorTree    BuildDimensionTree(ibQueryRowCursor& rows,
	                                             const std::vector<ibTotalLevel>& levels,
	                                             const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates,
	                                             ibDatabaseConnectionHolder* holder,
	                                             const ibBackendQueryable* source);
	static ibSelectorTree    BuildDimensionTree(const ibQueryRamTable& snapshot,
	                                             const std::vector<ibTotalLevel>& levels,
	                                             const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates,
	                                             ibDatabaseConnectionHolder* holder,
	                                             const ibBackendQueryable* source);
	// ⭐ FILL IN THE PERIODS NOBODY REPORTED. `BY <field> PERIODS(Month, &From, &To)` asks for two
	// things: group by the period containing the value (the folds do that), and give every period
	// between the bounds a row whether or not anything happened in it (this does). No fold can — a
	// fold sees the rows it was given, and the missing month is precisely the one that produced none.
	// Both roads end here, so padding means one thing whichever of them built the tree. Bounds left
	// empty pad between the first and last period the data holds; they never FILTER.
	static void              PadPeriodLevels(ibSelectorTree& tree, const std::vector<ibTotalLevel>& levels,
	                                         const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates);
	// Join two materialised tables on (onLeft <onOp> onRight), emitting outCols (each tagged by side in
	// fromLeft). onOp == Eq is the hash-join fast path; any other op is a nested-loop theta join. The RAM
	// JOIN core, pure (no DB) — unit-testable.
	static ibQueryRamTable   JoinRamTables(const ibQueryRamTable& left, const ibQueryRamTable& right,
	                                       const ibBackendQueryColumn* onLeft, const ibBackendQueryColumn* onRight,
	                                       const std::vector<const ibBackendQueryColumn*>& outCols,
	                                       const std::vector<bool>& fromLeft,
	                                       ibQueryJoinKind kind = ibQueryJoinKind::Inner,   // Inner / Left / Right / Full
	                                       const ibJoinOn& on = {});                        // ON op + computed exprs (keys: onLeft/onRight)
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
	// Compare one ORDER BY key for the RAM sort floor: NULL = smallest (SQLite convention)
	// -> NULLS FIRST on ASC, NULLS LAST on DESC, deterministically. <0: a before b; >0: b before a;
	// 0: equal on this key. Pure — unit-testable; shared by both RAM ORDER BY comparators.
	static int               RamSortCompareKey(const ibValue& a, const ibValue& b, bool ascending);
	// Evaluate a computed output column (Column / Const / Arith / Case) against one composed row — the
	// per-row eval for a computed column over a JOIN (single source pushes it to SQL). Pure — unit-testable.
	static ibValue           EvalColumnExpr(const ibQueryColumnExpr* expr, const ibQueryRamTable& table, long row);
	static long ExecuteWrite(const ibDataQuerySpec& spec, ibDataQueryBuilder::WriteKind kind);   // rows affected; -1 = error
};

#endif
