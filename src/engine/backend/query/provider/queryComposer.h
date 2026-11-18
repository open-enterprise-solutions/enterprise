#ifndef __QUERY_COMPOSER_H__
#define __QUERY_COMPOSER_H__

// ibQueryComposer — the engine the door runs a whole query through: one source to its own provider, a join or
// a union to one statement where its sources co-locate, else each read by its provider and stitched in memory (the
// RAM engine, queryComposer.cpp). It is the kitchen's, and nothing in the face includes it: the door reaches it through
// its own private verbs (dataQueryBuilder.h, defined in queryComposer.cpp), a selection through the folds the face
// declares (querySelector.h), and the interface every provider answers is the face's (queryProvider.h).

#include "backend/query/queryProvider.h"     // ibBackendQueryProvider (+ dataQueryBuilder.h: ibDataQuerySpec / ibDataQueryResult)
#include "backend/query/querySelectorTree.h" // ibSelectorTree — the tree TryFoldTotalsInDbms fills

// The door is blind to single-vs-multi source: it hands the composer the spec, and the composer asks the provider of
// the primary source (CanColocate, TryPageGroupLevel, TryFoldTotalsInDbms) before it reads the leaves itself — so
// adding multi-source never reshapes the door. (docs/private/query-language-arc.md §22.1)
class BACKEND_API ibQueryComposer
{
public:
	static ibDataQueryResult ExecuteRead(const ibDataQuerySpec& spec, const ibReadPageRequest& page);
	static ibDataQueryResult ExecuteReadCached(const ibDataQuerySpec& spec, const ibReadPageRequest& page,
	                                           ibRenderedPageCache& cache, const ibPageSignature& signature);
	static ibDataQueryResult ExecuteAggregate(const ibDataQuerySpec& spec);
	// Paged single-level group read (door SelectAggregatePage): server-side GROUP BY + keyset + LIMIT when the
	// source's provider pages it (TryPageGroupLevel), else the unpaged ExecuteAggregate (all groups).
	static ibDataQueryResult ExecuteGroupLevelPage(const ibDataQuerySpec& spec, const ibReadPageRequest& page);
	// The push-down a totals read takes first, asked as a QUESTION: true = the DBMS folded the levels
	// and `out` is the tree; false = nothing was read, and the result folds the detail. This is what lets
	// the query lowering — and therefore every composition — stop reading detail rows to add them up here.
	static bool              TryFoldTotalsInDbms(const ibDataQuerySpec& spec, ibSelectorTree& out);
	// Cross-catalog reference-dimension hierarchy: group the snapshot rows by `refCol`'s VALUE, then
	// arrange the value groups into the TARGET catalog's parent-ref hierarchy. The parent of each value
	// lives in the target catalog (not the snapshot) → materialised through the door from `source`'s
	// ResolveReferenceTarget(refCol). Falls back to a flat group when no target / no holder. (docs §22.1b)
	static ibSelectorTree    BuildReferenceHierarchy(const ibQueryRamTable& snapshot,
	                                                  const std::shared_ptr<const ibBackendQueryColumn>& refCol,
	                                                  const std::vector<ibDataQueryBuilder::AggregateItem>& aggregates,
	                                                  ibDatabaseConnectionHolder* holder,
	                                                  const std::shared_ptr<const ibBackendQueryable>& source,
	                                                  ibDimensionKind dim);
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
	                                       const std::shared_ptr<const ibBackendQueryColumn>& onLeft,
	                                       const std::shared_ptr<const ibBackendQueryColumn>& onRight,
	                                       const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& outCols,
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
	// Pure (no DB) — unit-testable. (docs/private/temp-db.md §7 — the same size lever)
	static std::vector<size_t> PlanInnerJoinOrder(const std::vector<long>& rowCounts,
	                                              const std::vector<std::pair<size_t, size_t>>& edges);
	// Append one UNION branch's rows to `out`: each output column is read from the branch's
	// same-named column (branchCols, by position; null = absent -> NULL cell). The RAM UNION
	// stacking core, pure (no DB) — unit-testable.
	static void              AppendUnionBranch(ibQueryRamTable& out, const ibQueryRamTable& branch,
	                                           const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& outCols,
	                                           const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& branchCols);
	// …from a branch nobody reads again: its cells moved, its rows moved whole where it keys them as `out` does.
	static void              AppendUnionBranch(ibQueryRamTable& out, ibQueryRamTable&& branch,
	                                           const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& outCols,
	                                           const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& branchCols);
	// Drop duplicate rows, keyed by the IDENTITY hash (GetHashKey) of every `cols` cell — the RAM
	// dedup core behind plain UNION (SQL semantics: dedupe the accumulated rows at each non-ALL
	// operator) and a future RAM DISTINCT. First occurrence wins, order preserved. Pure — unit-testable.
	static ibQueryRamTable   DedupeRows(const ibQueryRamTable& src,
	                                    const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& cols);
	// Keep only the rows of `src` that satisfy the boolean WHERE TREE (And/Or/Not/IsNull/Leaf) — the
	// post-compose RAM filter for an OR / IS NULL spanning a non-co-located JOIN. Pure — unit-testable.
	static ibQueryRamTable   FilterRows(const ibQueryRamTable& src, const ibQueryPredicatePtr& predicate);
	// Evaluate a computed output column (Column / Const / Arith / Case) against one composed row — the
	// per-row eval for a computed column over a JOIN (single source pushes it to SQL). Pure — unit-testable.
	static ibValue           EvalColumnExpr(const ibQueryColumnExprPtr& expr, const ibQueryRamTable& table, long row);
	static long ExecuteWrite(const ibDataQuerySpec& spec, ibDataQueryBuilder::WriteKind kind);   // rows affected; -1 = error
};

#endif
