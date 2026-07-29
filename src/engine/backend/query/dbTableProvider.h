#ifndef __DB_TABLE_PROVIDER_H__
#define __DB_TABLE_PROVIDER_H__

// ibDbTableProvider — the BIG provider: the L3<->L2 connection for a real DB table (vends
// physical paged reads / cached reads / aggregate / write) PLUS the static GET/WRITE
// data-access TEMPLATE — the one place a column's value is lifted from / bound to a DB row.
// It lives in its OWN L2-coupled file (it traffics L2 IR: BuildPageIR -> ibQueryIR), kept off
// the deliberately L2-free queryProvider.h. The lighter providers (RAM / temp) do NOT inherit
// it. (docs/query-language-arc.md §22.4)

#include "queryProvider.h"                                          // ibBackendQueryProvider / ibDataQuerySpec / ibReadPageRequest / ibDataQueryResult
#include "backend/databaseLayer/databaseQueryBuilder.h"             // ibQueryIR / ibQueryResult / ibQuerySortItem / ibRenderedQuery (L2)
#include "backend/metaCollection/attribute/metaAttributeObject.h"   // ibValueMetaObjectAttributeBase::ibFieldTypes (the value-assembly's TYPE-tag enum)

class ibMetaData;   // the metadata context the column-based value-assembly threads through (reference / enum reconstruction)

// ibRenderedPageCache — the build-once page cache (one scroll shape across ticks). OPAQUE at
// the door (dataQueryBuilder.h forward-declares it; the list model holds it via shared_ptr and
// builds it through ibDataQueryBuilder::NewPageCache). Its FULL layout lives here, where L2 is
// in scope (it stores an L2 ibRenderedQuery): the DB provider's ExecuteReadCached fills/reads
// it, and NewPageCache constructs it. (docs/query-language-arc.md §19/§20)
struct ibRenderedPageCache
{
	wxString                     m_sig;             // signature of the SQL-determining inputs
	std::vector<ibQuerySortItem> m_effectiveSort;   // resolved once (identity tail walk)
	ibRenderedQuery              m_rendered;         // SQL + bind plan, rendered once
	bool                         m_valid = false;
};

class ibDbTableProvider : public ibBackendQueryProvider
{
public:
	// --- the L3<->L2 read/write engine (vended by a DB-family queryable) ---
	ibDataQueryResult ExecuteRead(const ibDataQuerySpec& spec, const ibReadPageRequest& req) override;
	ibDataQueryResult ExecuteReadCached(const ibDataQuerySpec& spec, const ibReadPageRequest& req,
	                                    ibRenderedPageCache& cache, const wxString& signature) override;
	ibDataQueryResult ExecuteAggregate(const ibDataQuerySpec& spec) override;
	long ExecuteWrite(const ibDataQuerySpec& spec, ibDataQueryBuilder::WriteKind kind) override;

	// Reference dot-walk target resolution — THIS is the ONE metadata-owning provider (clsid ->
	// metaData->GetTypeCtor -> holder -> GetQueryable, read off queryable->GetMetaData()). The base
	// returns null and the computed provider forwards here, so the query-provider layer stays
	// metadata-free while resolution has a single home. (docs/query-language-arc.md §22 dot-walk)
	const ibBackendQueryable* ResolveReferenceTarget(const ibBackendQueryable* queryable, const ibBackendQueryColumn* refColumn) const override;
	std::vector<const ibBackendQueryable*> ResolveReferenceTargets(const ibBackendQueryable* queryable, const ibBackendQueryColumn* refColumn) const override;

	// --- multi-source: co-located server-side JOIN (docs/query-language-arc.md §22.1a) -------
	// CanColocateJoin — is the spec's relational tree an N-way INNER/LEFT join of DISTINCT real DB
	// tables on resolvable (explicit OR reference-derived) single-field keys, every output column
	// owned by a leaf? When true the whole join runs in ONE server-side SELECT (the DBMS does the
	// join + the cross-table filter; reference / enum / variant outputs reconstruct via the full
	// spread). Outside this shape (self-join, a RAM-computed leaf, row-key conditions, dot-walk,
	// key-in) stays the composer's RAM path — a co-location FAST PATH, not a replacement. Note a DB
	// TEMP leaf co-locates like any real table (ibDbTempTableQueryable does not override GetProvider),
	// and an aggregate terminal routes to the sibling gate CanColocateAggregate, not to RAM.
	// BACKEND_API on the gates so the routing decision is unit-testable across the DLL.
	static BACKEND_API bool  CanColocateJoin(const ibDataQuerySpec& spec);
	static ibDataQueryResult ExecuteColocatedJoin(const ibDataQuerySpec& spec, const ibReadPageRequest& page);

	// Co-located server-side AGGREGATE — the same join-tree gate, terminal = GroupBy()/Sum()/… : the
	// JOIN + GROUP BY + aggregates run in ONE server-side SELECT (vs the composer's RAM fold). Group
	// keys may be reference (grouped by the full spread); aggregate INPUTS stay scalar.
	static BACKEND_API bool  CanColocateAggregate(const ibDataQuerySpec& spec);
	static ibDataQueryResult ExecuteColocatedAggregate(const ibDataQuerySpec& spec);

	// Single-level group KEYSET paging (docs: group-level paging). CanPageGroupLevel: a single PLAIN scalar
	// grouping dimension over a SINGLE DB source -> the level's groups page server-side as
	// GROUP BY dim ORDER BY dim [dim > anchor] LIMIT count, instead of reading EVERY detail row and folding
	// all groups in RAM (the eager path that OOMs a nomenclature hierarchy with thousands of groups per level).
	// Outside the shape (multi-level, a dot-walk / computed dim, a multi-source group) keeps the RAM fold.
	// BACKEND_API + no dialect probe -> unit-testable without a DB, like the co-location gates.
	static BACKEND_API bool CanPageGroupLevel(const ibDataQuerySpec& spec);

	// The paged group-level read (gate above). SELECT dim [, aggs] FROM src WHERE <conds> [AND dim </> anchor]
	// GROUP BY dim ORDER BY dim LIMIT count -- the dim is the group key AND the keyset column, so a page is
	// positioned by the anchor group's dim value. Returns the groups for ONE level; the model wraps them as
	// group nodes. (Single plain scalar dim over a single source -- the gate guarantees the shape.)
	static ibDataQueryResult ExecuteGroupLevelPage(const ibDataQuerySpec& spec, const ibReadPageRequest& page);

	// Co-located server-side UNION — the branches (each a real DB table) stack as a SQL UNION ALL of
	// per-branch SELECTs (output columns resolved per branch by NAME, aligned by position); ORDER BY /
	// LIMIT wrap the union in a subquery. Scalar outputs (the common catalog ∪ catalog list); a
	// computed branch is handled by the composer's mixed-promote, not this gate.
	static BACKEND_API bool  CanColocateUnion(const ibDataQuerySpec& spec);
	static ibDataQueryResult ExecuteColocatedUnion(const ibDataQuerySpec& spec, const ibReadPageRequest& page);

	// Totals push-down via GROUP BY ROLLUP (docs/query-language-arc.md §22.1b). CanPushRollupTotals:
	// a single-source DB queryable, SCALAR or REFERENCE group keys (a reference groups by its full spread as ONE
	// composite ROLLUP((f0,f1,…)) element, reassembled on read) + scalar aggregate inputs, AND the connected dialect
	// advertises ROLLUP. ExecuteRollupTotals then runs ONE GROUP BY ROLLUP(keys) + the aggregates +
	// GROUPING(key) flags — the DBMS computes every subtotal level from raw detail (correct for
	// COUNT / AVG) — and assembles the ibSelectorTree node tree from the result. Else the composer
	// RAM-folds the detail. BACKEND_API for the unit test.
	// CanRollupTotalsShape = the STRUCTURAL half (no dialect probe -> unit-testable without a DB);
	// CanPushRollupTotals adds the ROLLUP-dialect capability (what the composer dispatches on).
	static BACKEND_API bool CanRollupTotalsShape(const ibDataQuerySpec& spec);
	static BACKEND_API bool CanPushRollupTotals(const ibDataQuerySpec& spec);
	static ibSelectorTree   ExecuteRollupTotals(const ibDataQuerySpec& spec);

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
	//   CanPushColocatedRollupTotals — adds the DB-intrinsic ROLLUP-dialect capability. The composer
	//                                 dispatches on this.
	static BACKEND_API bool CanColocateRollupTotals(const ibDataQuerySpec& spec);
	static BACKEND_API bool CanPushColocatedRollupTotals(const ibDataQuerySpec& spec);
	static ibSelectorTree   ExecuteColocatedRollupTotals(const ibDataQuerySpec& spec);

	// (The plain column value codec — GetValueColumn / SetValueColumn — was INLINED to its tier home
	//  ibColumnCodec::ReadValue / WriteValue (query/columnLayout.h): call sites speak the tier directly,
	//  no provider forwarder. WriteFieldsOf likewise -> ColumnFieldNames. The attribute adapters below
	//  stay because they supply the attribute's OWN metaData over that tier.)

	// --- thin convenience adapters for callers that already hold the metaobject attribute (the
	//     register lowering — recorder / period / dimension / resource attributes). The attribute
	//     IS a column and carries its own metaData, so these forward to the column-based core; the
	//     core itself names no attribute.
	static void SetValueAttribute(const ibValueMetaObjectAttributeBase* attr, const ibValue& cValue, ibQueryStatement* statement, int& position);
	static bool GetValueAttribute(const ibValueMetaObjectAttributeBase* attr, ibValue& retValue, ibQueryResult& result, bool createData = true);
	// Read a NAMED field of a KNOWN variant type (a register reading a numeric balance / turnover
	// column). Forwards to the codec's read leaf (ibColumnCodec::ReadField).
	static bool GetValueAttribute(const wxString& fieldName, ibFieldTypes fieldType,
	                              const ibValueMetaObjectAttributeBase* attr, ibValue& retValue, ibQueryResult& result, bool createData = true);

private:
	// Name-substitution lowering — spec -> L2 IR (connection-free Build()).
	static ibQueryIR BuildPageIR(const ibDataQuerySpec& spec, const ibReadPageRequest& req,
	                             const std::vector<ibQuerySortItem>& effective);
	static std::vector<ibValue> BuildExternal(const ibReadPageRequest& req, const std::vector<ibQuerySortItem>& effective);
};

#endif // __DB_TABLE_PROVIDER_H__
