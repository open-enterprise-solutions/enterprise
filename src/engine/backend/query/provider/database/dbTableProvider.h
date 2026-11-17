#ifndef __DB_TABLE_PROVIDER_H__
#define __DB_TABLE_PROVIDER_H__

// ibDbTableProvider — the BIG provider: the L3<->L2 connection for a real DB table (vends
// physical paged reads / cached reads / aggregate / write) PLUS the static GET/WRITE
// data-access TEMPLATE — the one place a column's value is lifted from / bound to a DB row.
// It lives in its OWN L2-coupled file (it traffics L2 IR: BuildPageIR -> ibQueryIR), kept off
// the deliberately L2-free queryProvider.h. The lighter providers (RAM / temp) do NOT inherit
// it. (docs/private/query-language-arc.md §22.4)

#include "backend/query/queryProvider.h"                            // ibBackendQueryProvider / ibDataQuerySpec / ibReadPageRequest / ibDataQueryResult
#include "backend/databaseLayer/databaseQueryBuilder.h"             // ibQueryIR / ibQueryResult / ibQuerySortItem / ibRenderedQuery (L2)
#include "backend/metaCollection/attribute/metaAttributeObject.h"   // ibValueMetaObjectAttributeBase::ibFieldTypes (the value-assembly's TYPE-tag enum)

class ibMetaData;   // the metadata context the column-based value-assembly threads through (reference / enum reconstruction)

// ibRenderedPageCache — the build-once page cache (one scroll shape across ticks). OPAQUE at
// the door (dataQueryBuilder.h forward-declares it; the list model holds it via shared_ptr and
// builds it through ibDataQueryBuilder::NewPageCache). Its FULL layout lives here, where L2 is
// in scope (it stores an L2 ibRenderedQuery): the DB provider's ExecuteReadCached fills/reads
// it, and NewPageCache constructs it. (docs/private/query-language-arc.md §19/§20)
struct ibRenderedPageCache
{
	ibPageSignature              m_sig;             // the SQL-determining inputs, compared as values (dataQueryBuilder.h)
	std::vector<ibQuerySortItem> m_effectiveSort;   // resolved once (identity tail walk)
	ibRenderedQuery              m_rendered;         // SQL + bind plan, rendered once
	bool                         m_valid = false;
};

class ibDbTableProvider : public ibBackendQueryProvider
{
public:
	// --- the L3<->L2 read/write engine (vended by a DB-family queryable) ---
	virtual ibDataQueryResult ExecuteRead(const ibDataQuerySpec& spec, const ibReadPageRequest& req) override;
	virtual ibDataQueryResult ExecuteReadCached(const ibDataQuerySpec& spec, const ibReadPageRequest& req,
	                                            ibRenderedPageCache& cache, const ibPageSignature& signature) override;
	virtual ibDataQueryResult ExecuteAggregate(const ibDataQuerySpec& spec) override;

	// The page of one grouping level, when the shape pages (CanPageGroupLevel) — the gate asked once, then the page read.
	virtual bool TryPageGroupLevel(const ibDataQuerySpec& spec, const ibReadPageRequest& page, ibDataQueryResult& out) override;

	// Its three folds: one source — a plain GROUP BY over the levels wherever the figures add up (any engine), else
	// GROUP BY ROLLUP where the engine has it; a tree of its tables — ROLLUP over the co-located join or union.
	virtual bool TryFoldTotalsInDbms(const ibDataQuerySpec& spec, ibSelectorTree& out) override;

	// The same GROUP BY / the same read, stopped one step before they run — see the base declarations.
	virtual ibQueryRelPtr BuildAggregateRelation(const ibDataQuerySpec& spec) override;
	virtual ibQueryRelPtr BuildReadRelation(const ibDataQuerySpec& spec) override;

	// Yes, whichever connection is named: every statement of the base is this provider's, on any of the base's
	// connections — a source of another base is read by a provider of its own.
	virtual bool CanJoinOn(ibDatabaseConnectionHolder* /*holder*/) const override { return true; }
	// A join of its tables (CanColocateJoin) or a union of them (CanColocateUnion), read; a join of them folded by one
	// GROUP BY (CanColocateAggregate), when the spec groups or aggregates.
	virtual bool CanColocate(const ibDataQuerySpec& spec) const override;

	// A named query on the connected engine (ibCanUseCte).
	virtual bool CanDeclareNamedQuery(ibDatabaseConnectionHolder* holder) const override;
	// …and, of a built inner door: WOULD IT RENDER WHOLE, IF IT WERE DECLARED? Asked BEFORE `.With(name, inner)` by the
	// lowering that is choosing a road, and answered by the tier that would have to write the SQL.
	//
	// A single-source door always would. A JOINED one only when its tree co-locates — the same
	// question the join read asks (CanColocateJoin), because it is the same rendering: the leaves
	// become one server-side FROM. A door that would not render whole must go back to the rows road
	// while there is still a road to go back to; declared anyway, it would leave the outer statement
	// naming a table nothing ever wrote.
	//
	// ⚠ IT TAKES THE DOOR, NOT A SPEC. A door hands its own spec over and nobody else takes one —
	// this tier is its friend, so the question can be asked here and only here.
	virtual bool CanDeclareAsNamedQuery(const ibDataQueryBuilder& inner) const override;
	// How many values one statement may bind on the connected engine (ibDialectDictionary::m_maxParameters); 0 = none known.
	virtual size_t ParametersOneStatementTakes() const override;

	virtual long ExecuteWrite(const ibDataQuerySpec& spec, ibDataQueryBuilder::WriteKind kind) override;

	// Reference dot-walk target resolution — THIS is the ONE metadata-owning provider (clsid ->
	// metaData->GetTypeCtor -> holder -> GetQueryable, read off queryable->GetMetaData()). The base's
	// default asks here (ibDbTableProviderInstance), so the query-provider layer stays metadata-free
	// while resolution has a single home. (docs/private/query-language-arc.md §22 dot-walk)
	virtual std::shared_ptr<const ibBackendQueryable> ResolveReferenceTarget(const std::shared_ptr<const ibBackendQueryable>& queryable,
		const std::shared_ptr<const ibBackendQueryColumn>& refColumn) const override;
	virtual std::vector<std::shared_ptr<const ibBackendQueryable>> ResolveReferenceTargets(const std::shared_ptr<const ibBackendQueryable>& queryable,
		const std::shared_ptr<const ibBackendQueryColumn>& refColumn) const override;
	virtual ibClassID ResolveReferenceType(const std::shared_ptr<const ibBackendQueryable>& queryable) const override;
	virtual bool ResolveQueryConstant(const std::shared_ptr<const ibBackendQueryable>& queryable, const ibString& member, ibValue& out) const override;

	// The flat list read from this base is in hand: the references it made — this base's own — are told
	// what they say together, a table at a time (ibValueReferenceDataObject::ReadBatch). See the base's note.
	virtual void ReadReferences() const override;

	// --- the gates of CanColocate, TryPageGroupLevel and TryFoldTotalsInDbms, one road each; BACKEND_API so each
	//     routing decision is unit-testable across the DLL. Each road's execution is private, reached through
	//     ExecuteRead / ExecuteAggregate / TryPageGroupLevel / TryFoldTotalsInDbms. ---------------------------------

	// Co-located server-side JOIN (docs/private/query-language-arc.md §22.1a) — is the spec's relational tree an N-way
	// INNER/LEFT join of DISTINCT real DB tables on resolvable (explicit OR reference-derived) single-field keys, every
	// output column owned by a leaf? When true the whole join runs in ONE server-side SELECT (the DBMS does the join +
	// the cross-table filter; reference / enum / variant outputs reconstruct via the full spread). Outside this shape
	// (self-join, a RAM-computed leaf, row-key conditions, dot-walk, key-in) stays the composer's RAM path — a
	// co-location FAST PATH, not a replacement. Note a DB TEMP leaf co-locates like any real table
	// (ibDbTempTableQueryable does not override GetProvider), and an aggregate terminal is the sibling gate's,
	// CanColocateAggregate.
	static BACKEND_API bool CanColocateJoin(const ibDataQuerySpec& spec);

	// Co-located server-side AGGREGATE — the same join-tree gate, terminal = GroupBy()/Sum()/… : the
	// JOIN + GROUP BY + aggregates run in ONE server-side SELECT (vs the composer's RAM fold). Group
	// keys may be reference (grouped by the full spread); aggregate INPUTS stay scalar.
	static BACKEND_API bool CanColocateAggregate(const ibDataQuerySpec& spec);

	// Co-located server-side UNION — the branches (each a real DB table) stack as a SQL UNION ALL of
	// per-branch SELECTs (output columns resolved per branch by NAME, aligned by position); ORDER BY /
	// LIMIT wrap the union in a subquery. Scalar outputs (the common catalog ∪ catalog list); a
	// computed branch is handled by the composer's mixed-promote, not this gate.
	static BACKEND_API bool CanColocateUnion(const ibDataQuerySpec& spec);

	// Single-level group KEYSET paging (docs: group-level paging): a single PLAIN scalar grouping dimension -> the
	// level's groups page server-side as GROUP BY dim ORDER BY dim [dim > anchor] LIMIT count, instead of reading EVERY
	// detail row and folding all groups in RAM (the eager path that OOMs a nomenclature hierarchy with thousands of
	// groups per level). Outside the shape (multi-level, a dot-walk / computed dim, a multi-source group) keeps the RAM
	// fold. No dialect probe -> unit-testable without a DB, like the co-location gates.
	static BACKEND_API bool CanPageGroupLevel(const ibDataQuerySpec& spec);

	// Totals push-down via GROUP BY ROLLUP (docs/private/query-language-arc.md §22.1b). CanPushRollupTotals:
	// a single-source DB queryable, SCALAR or REFERENCE group keys (a reference groups by its full spread as ONE
	// composite ROLLUP((f0,f1,…)) element, reassembled on read) + scalar aggregate inputs, AND the connected dialect
	// advertises ROLLUP. The fold then runs ONE GROUP BY ROLLUP(keys) + the aggregates + GROUPING(key) flags — the
	// DBMS computes every subtotal level from raw detail (correct for COUNT / AVG) — and assembles the ibSelectorTree
	// node tree from the result. Else the composer RAM-folds the detail.
	// CanRollupTotalsShape = the STRUCTURAL half (no dialect probe -> unit-testable without a DB);
	// CanPushRollupTotals adds the ROLLUP-dialect capability (what TryFoldTotalsInDbms asks).
	static BACKEND_API bool CanRollupTotalsShape(const ibDataQuerySpec& spec);
	static BACKEND_API bool CanPushRollupTotals(const ibDataQuerySpec& spec);

	// A LADDER OF HEADINGS NEEDS NO ROLLUP: a plain GROUP BY over every level's fields, which every engine has
	// (Firebird has no ROLLUP), returns the bottom groups ordered by the keys and by an ordinary sort's MIN / MAX;
	// the groups above are made as they are first met and, with the grand total, add up from the ones below — so
	// only a sum, a count, a minimum or a maximum rides here. Same shape rules as the ROLLUP road otherwise; no
	// dialect probe -> unit-testable without a DB.
	static BACKEND_API bool CanFoldGroupByTotals(const ibDataQuerySpec& spec);

	// Multi-source variant of the ROLLUP totals push-down: the SAME GROUP BY ROLLUP + GROUPING()
	// mechanism, but over a co-located INNER/LEFT JOIN tree (BuildColocatedFrom) OR a UNION-of-branches
	// derived table (BuildUnionRollupFrom) instead of one table —
	// so a TOTALS over a JOIN runs server-side (the DBMS computes every subtotal level; only aggregated
	// rows transit) instead of the composer materialising both leaves and folding the tree in RAM. Same
	// ibSelectorTree either way — perf, not correctness. Split in two so the routing is unit-testable
	// without a DB (unlike the single-source CanPushRollupTotals, which conflates shape + dialect and is
	// consequently untested):
	//   CanColocateRollupTotals     — the STRUCTURAL half: CanColocateBase's join tree, SCALAR or REFERENCE group
	//                                 keys over the JOIN (a reference groups by its spread as a composite ROLLUP
	//                                 element; a UNION branch stays scalar), scalar aggregate inputs, no dot-walk /
	//                                 computed group or aggregate. No dialect probe -> testable like
	//                                 CanColocateAggregate.
	//   CanPushColocatedRollupTotals — adds the DB-intrinsic ROLLUP-dialect capability (what TryFoldTotalsInDbms asks).
	static BACKEND_API bool CanColocateRollupTotals(const ibDataQuerySpec& spec);
	static BACKEND_API bool CanPushColocatedRollupTotals(const ibDataQuerySpec& spec);

	// (The plain column value codec — GetValueColumn / SetValueColumn — was INLINED to its tier home
	//  ibColumnCodec::ReadValue / WriteValue (query/provider/database/columnCodec.h): call sites speak the tier directly,
	//  no provider forwarder. WriteFieldsOf likewise -> ColumnFieldNames. The attribute adapters below
	//  stay because they supply the attribute's OWN metaData over that tier.)

	// --- thin convenience adapters for callers that already hold the metaobject attribute (the
	//     register lowering — recorder / period / dimension / resource attributes). The attribute
	//     IS a column and carries its own metaData, so these forward to the column-based core; the
	//     core itself names no attribute.
	static void SetValueAttribute(const ibValueMetaObjectAttributeBase* attr, const ibValue& cValue, ibQueryStatement* statement, int& position);
	static bool GetValueAttribute(const ibValueMetaObjectAttributeBase* attr, ibValue& retValue, ibQueryResult& result, bool createData = false);
	// Read a NAMED field of a KNOWN variant type (a register reading a numeric balance / turnover
	// column). Forwards to the codec's read leaf (ibColumnCodec::ReadField).
	static bool GetValueAttribute(const ibString& fieldName, ibFieldTypes fieldType,
	                              const ibValueMetaObjectAttributeBase* attr, ibValue& retValue, ibQueryResult& result, bool createData = false);

	// The named queries the door declared (`With`), lowered into the SAME IR as `WITH … AS (…)`.
	// Each is an ordinary spec lowered by BuildPageIR — or, for a joined one, by the co-located
	// join's own FROM tree: a CTE is a query written in another place, not a simpler kind of query.
	//
	// ⚠ ASKED BY WHOEVER ASSEMBLES AN IR, which is why it is not private: the page read is not the
	// only assembler. The ROLLUP fold builds its own statement out of the same spec, and a statement
	// that reads a declared name without carrying the declaration is not a slower query — it is one
	// the engine cannot parse ("table unknown"). Whoever writes the FROM owes the WITH.
	static void AttachNamedQueries(const ibDataQuerySpec& spec, ibQueryIR& ir);

	// ⭐ A CONDITION, AS THE EXPRESSION A STATEMENT FILTERS BY — for a caller that writes its own SELECT
	// over this table and needs the condition INSIDE it (a register reading its totals asks for the
	// rows before it folds them). The whole tree: AND / OR / NOT, IS NULL, REFS, a comparison; a walk
	// through a reference is a correlated EXISTS, so the filter never multiplies a row. `qualifier` is
	// what the statement calls the table (empty: its own name).
	static BACKEND_API ibQueryExprPtr BuildPredicateIR(const std::shared_ptr<const ibBackendQueryable>& queryable,
	                                                   const ibQueryPredicatePtr& predicate,
	                                                   const ibString& qualifier = ibEmptyString);

	// …AND A COMPUTED VALUE, the same way — arithmetic, a CASE, a column read field by field — for the same
	// caller: one that writes its own SELECT over this table and needs the value in its projection (the rows
	// of a register's movements, each one's contribution to the totals, ibSchemaMaterialize::ToReadSpec).
	static BACKEND_API ibQueryExprPtr BuildColumnExprIR(const std::shared_ptr<const ibBackendQueryable>& queryable,
	                                                    const ibQueryColumnExprPtr& expr,
	                                                    const ibString& qualifier = ibEmptyString);

private:
	// What each gate above opens — reached only through the verbs of the interface, once their question said yes.
	static ibDataQueryResult ExecuteColocatedJoin(const ibDataQuerySpec& spec, const ibReadPageRequest& page);
	static ibDataQueryResult ExecuteColocatedAggregate(const ibDataQuerySpec& spec);
	static ibDataQueryResult ExecuteColocatedUnion(const ibDataQuerySpec& spec, const ibReadPageRequest& page);
	// The page: SELECT dim [, aggs] FROM src WHERE <conds> [AND dim </> anchor] GROUP BY dim ORDER BY dim LIMIT count —
	// the dim is the group key AND the keyset column, so a page is positioned by the anchor group's dim value.
	static ibDataQueryResult ExecuteGroupLevelPage(const ibDataQuerySpec& spec, const ibReadPageRequest& page);
	static ibSelectorTree    ExecuteRollupTotals(const ibDataQuerySpec& spec);
	static ibSelectorTree    ExecuteGroupByTotals(const ibDataQuerySpec& spec);
	static ibSelectorTree    ExecuteColocatedRollupTotals(const ibDataQuerySpec& spec);

	// The GROUP BY, assembled into an L2 builder and not yet run. ONE assembly, two endings: the
	// execute path runs it, the relation path takes its IR. Split for exactly that reason — two
	// copies of a join chain, a projection spread and a HAVING would be two chances to answer
	// differently depending on which door was used.
	static void BuildAggregateQuery(const ibDataQuerySpec& spec, ibDatabaseQueryBuilder& q);

	// Name-substitution lowering — spec -> L2 IR (connection-free Build()).
	static ibQueryIR BuildPageIR(const ibDataQuerySpec& spec, const ibReadPageRequest& req,
	                             const std::vector<ibQuerySortItem>& effective);
	static std::vector<ibValue> BuildExternal(const ibReadPageRequest& req, const std::vector<ibQuerySortItem>& effective);
};

// (A type as a value — ibTypeValueOf / ibTypeValueByName / ibTypeValueClsid / ibValueAsType — is declared by the face,
//  in queryLowering.h beside the words of a CAST, and defined in dbTableProvider.cpp.)

#endif // __DB_TABLE_PROVIDER_H__
