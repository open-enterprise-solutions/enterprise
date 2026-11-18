#ifndef __QUERY_PROVIDER_H__
#define __QUERY_PROVIDER_H__

// ibBackendQueryProvider — the L3 execution ENGINE for one source (a relation). The
// door (ibDataQueryBuilder) is the metadata SURFACE and is L2-BLIND; the provider is
// the engine that realizes a source and holds ALL the L2 + the metadata->physical
// lowering. The interface is the face's; every provider is the kitchen's, in a folder of
// its own under provider/, and nothing in the face includes one:
//   - ibDbTableProvider  — lowers to L2 over the real table (read / cached / aggregate
//                          / write); provider/database/;
//   - ibComputedProvider — computes a RAM virtual table (register slice / balance /
//                          turnover, a temp table, a subquery); provider/computed/.
// The queryable VENDS the provider (ibBackendQueryable::GetProvider()); the door reads
// the whole query through THIS interface, passing the accumulated ibDataQuerySpec and
// never naming a concrete provider or an L2 type. The providers are STATELESS (the spec
// carries all state), so a queryable hands back a shared static instance.
// See docs/private/query-language-arc.md §22.

#include "backend/query/dataQueryBuilder.h"  // ibDataQueryResult / ibReadPageRequest / ibDataQuerySpec / ibRenderedPageCache (all L2-free)

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
	                                            ibRenderedPageCache& /*cache*/, const ibPageSignature& /*signature*/)
	{
		return ExecuteRead(spec, page);
	}

	// Aggregated read — GROUP BY built from the spec's groupBy /
	// aggregates / having. NOT paged. Non-DB providers fall back to a plain read.
	virtual ibDataQueryResult ExecuteAggregate(const ibDataQuerySpec& spec)
	{
		return ExecuteRead(spec, ibReadPageRequest{});
	}

	// ⭐ ONE LEVEL OF GROUPS, A PAGE AT A TIME, if it can — the drill of a grouped list: one grouping level of one source
	// read as GROUP BY dim ORDER BY dim [dim </> anchor] LIMIT count, instead of every group folded at once. Asked of the
	// provider of the spec's primary source; ONE verb, as TryFoldTotalsInDbms below. True = `out` is the page; false
	// (the default) = nothing was read, and the composer takes the unpaged aggregate, which answers the same groups,
	// all of them.
	virtual bool TryPageGroupLevel(const ibDataQuerySpec& /*spec*/, const ibReadPageRequest& /*page*/, ibDataQueryResult& /*out*/) { return false; }

	// ⭐ TOTALS FOLDED BY ITS OWN STATEMENT, if it can — the ladder of headings (spec.m_totals) and their figures
	// computed where the rows live, so only the folded rows come back. Asked of the provider of the spec's primary
	// source — a tree is one source by then, the composite it is (ibQueryComposer::OverComposite). ONE verb, the composer's own
	// (ibQueryComposer::TryFoldTotalsInDbms): a provider with several folds asks each of its gates once and folds by
	// the first that says yes. True = `out` is the tree; false (the default) = nothing was read, and the composer reads
	// the detail and folds it in memory — the numbers do not depend on the road.
	virtual bool TryFoldTotalsInDbms(const ibDataQuerySpec& /*spec*/, ibSelectorTree& /*out*/) { return false; }

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

	// ⭐ ARE THE ROWS BUILT IN MEMORY — by the source's ComputeRows, with no statement written: a register's virtual
	// table, a nested query, a temp table we filled, the registration journal. It is the ROAD's answer, so the
	// provider that takes the road gives it, and a source that changes roads (a register's totals, materialised or
	// not) changes the provider it vends. False (the default) = the provider writes a statement.
	// ⚠ NOT "may it go into THIS statement": a provider of another connection builds nothing in memory and still has
	// no place there — that is CanJoinOn, below.
	virtual bool IsComputedInRam() const { return false; }

	// ⭐ CAN ITS SOURCES BE WRITTEN INTO A STATEMENT ON THIS CONNECTION — joined, unioned, folded, semi-joined together
	// with that connection's tables. Asked of the provider of every source a statement would take, for the connection
	// the statement runs on; null = the base's own statement, asked where no connection is at hand yet (a gate, the
	// renderer, the lowering). The database provider: yes — every statement of the base is its. False (the default):
	// a provider that writes no statement (computed in RAM), or one that writes it on another connection, which says
	// yes for its own connection only.
	virtual bool CanJoinOn(ibDatabaseConnectionHolder* /*holder*/) const { return false; }

	// ⭐ CAN A STATEMENT DECLARE A NAMED QUERY — `WITH <name> AS (…)`? Asked by the LOWERING before it chooses how a
	// nested query travels: declared, the server reads it and the join is the server's; otherwise its rows come
	// back and the join is ours. Asked of the provider every source of the declaration belongs to — the one that
	// would write it — first of its engine on that connection (CanDeclareNamedQuery), then of the built inner door:
	// would it be written whole (CanDeclareAsNamedQuery). False (the default, and every provider that writes no
	// statement) = the rows road.
	virtual bool CanDeclareNamedQuery(ibDatabaseConnectionHolder* /*holder*/) const { return false; }
	virtual bool CanDeclareAsNamedQuery(const ibDataQueryBuilder& /*inner*/) const { return false; }

	// How many values one statement binds — what a road that would bind a long list (a set of references asked
	// inside the statement) must fit. 0 (the default, and every provider that writes no statement) = no limit.
	virtual size_t ParametersOneStatementTakes() const { return 0; }

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
	// DB provider: clsid -> GetTypeCtor -> holder -> GetQueryable, read off queryable->GetMetaData).
	// A reference is an identity in the base whatever source holds it, so every other provider is
	// answered by that one (ibDbTableProviderInstance) — the default below; a provider overrides it
	// only to answer differently (a test's fake sources). This query-provider layer names NO
	// metadata — only the signature. (docs/private/query-language-arc.md §22 dot-walk)
	virtual std::shared_ptr<const ibBackendQueryable> ResolveReferenceTarget(const std::shared_ptr<const ibBackendQueryable>& queryable,
	                                                                         const std::shared_ptr<const ibBackendQueryColumn>& refColumn) const {
		return ibDbTableProviderInstance().ResolveReferenceTarget(queryable, refColumn);
	}
	// ALL reference targets of a column — N for a COMPOSITE (multi-type) reference, 1 for a single
	// reference, empty for a non-reference. Answered the same way. (docs §22)
	virtual std::vector<std::shared_ptr<const ibBackendQueryable>> ResolveReferenceTargets(const std::shared_ptr<const ibBackendQueryable>& queryable,
	                                                                                        const std::shared_ptr<const ibBackendQueryColumn>& refColumn) const {
		return ibDbTableProviderInstance().ResolveReferenceTargets(queryable, refColumn);
	}
	// …and the other way: the class of a reference TO `queryable` — what `x REFS Catalog.Goods` and a CAST naming a
	// table compare a value's type with. 0 when nothing references it. Asked of the source's OWN provider, unlike the
	// two above: only a table of the base is referenced, and the DB provider says by what class; a source computed in
	// memory is referenced by nothing, so every other inherits 0.
	virtual ibClassID ResolveReferenceType(const std::shared_ptr<const ibBackendQueryable>& /*queryable*/) const { return 0; }
	// A constant of `queryable` by name — the L4-1 literal `VALUE(Catalog.Goods.<member>)`: an empty reference, a
	// predefined item. TRUE + the value in `out` when the member resolves; the same owner answers it, every other
	// provider has none.
	virtual bool ResolveQueryConstant(const std::shared_ptr<const ibBackendQueryable>& /*queryable*/,
	                                  const ibString& /*member*/, ibValue& /*out*/) const { return false; }
	// …and what a sort by that COLUMN sorts by — its target's order column (ibBackendQueryable::GetSortColumn). A
	// reference of SEVERAL kinds sorts by the one every kind has under one name, which a walk through it finds in
	// each. Null for a column that is no reference, or whose kind has no order of its own. Asked of the two above,
	// so every provider answers it alike. (Body in queryProvider.cpp, beside this header.)
	std::shared_ptr<const ibBackendQueryColumn> ResolveReferenceSortColumn(const std::shared_ptr<const ibBackendQueryable>& queryable,
	                                                                       const std::shared_ptr<const ibBackendQueryColumn>& refColumn) const;

	// ⭐⭐ A FLAT LIST IS WHOLE, AND ITS REFERENCES ARE MADE — the event, raised by the one door every RAM
	// road ends at (the RAM-backed ibDataQueryResult), of the provider of the source it was read from. It
	// carries no rows: the references the list holds are in the register already, and that is where they
	// are read from — by the provider of OUR base (ibDbTableProvider), which every other provider's rows
	// were made out of too; so that is the default.
	virtual void ReadReferences() const { ibDbTableProviderInstance().ReadReferences(); }
};

#endif
