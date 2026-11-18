#ifndef __QUERY_COMPOSER_H__
#define __QUERY_COMPOSER_H__

// ibQueryComposer — the engine the door runs a whole query through: one source to its own provider, a join or a union
// as one source — the composite it is (OverComposite) — and what its own base cannot write, in a base in memory
// (OverMemorySpace); one translation reads it either way (docs/private/query-one-road.md §7). It is the kitchen's, and
// nothing in the face includes it: the door reaches it through its own private verbs (dataQueryBuilder.h, defined in
// queryComposer.cpp), a selection through the folds the face declares (querySelector.h), and the interface every
// provider answers is the face's (queryProvider.h).

#include "backend/query/queryProvider.h"     // ibBackendQueryProvider (+ dataQueryBuilder.h: ibDataQuerySpec / ibDataQueryResult)
#include "backend/query/querySelectorTree.h" // ibSelectorTree — the tree TryFoldTotalsInDbms fills

// The door is blind to single-vs-multi source: it hands the composer the spec, and the composer hands the provider of
// the one source the spec reads by then (TryPageGroupLevel, TryFoldTotalsInDbms, the reads) — so adding multi-source
// never reshapes the door. (docs/private/query-language-arc.md §22.1)
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
	// ⭐⭐ A JOIN OR A UNION AS ONE SOURCE — the spec over the composite its tree is (composedQueryable.h), every column
	// it names that composite's twin, so the single source's translation reads it whole. Asked by every road that
	// writes the tree into one statement: the reads above, a declared query's body. One source — handed back as it is.
	static ibDataQuerySpec   OverComposite(const ibDataQuerySpec& spec);
	// ⭐⭐ …AND A SPEC ITS OWN BASE CANNOT READ, IN A SPACE IN MEMORY — its sources copied into one taken for this execution
	// (a source computed in memory as its rows, a table as the rows it is read for), the tables its walks go into beside
	// them, and the spec over the copies as one composite source that holds them: read by the same translation as on
	// the server, as one statement (query-one-road.md §7).
	static ibDataQuerySpec   OverMemorySpace(const ibDataQuerySpec& spec);
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
	static long ExecuteWrite(const ibDataQuerySpec& spec, ibDataQueryBuilder::WriteKind kind);   // rows affected; -1 = error
};

#endif
