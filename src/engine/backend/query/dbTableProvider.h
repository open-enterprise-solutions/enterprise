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
	bool ExecuteWrite(const ibDataQuerySpec& spec, ibDataQueryBuilder::WriteKind kind) override;

	// --- multi-source: co-located server-side JOIN (docs/query-language-arc.md §22.1a) -------
	// CanColocateJoin — is the spec's relational tree an N-way INNER/LEFT join of DISTINCT real DB
	// tables on resolvable (explicit OR reference-derived) single-field keys, every output column
	// owned by a leaf? When true the whole join runs in ONE server-side SELECT (the DBMS does the
	// join + the cross-table filter; reference / enum / variant outputs reconstruct via the full
	// spread). Outside this shape (self-join, a computed/temp leaf, row-key conditions, dot-walk,
	// key-in, an aggregate terminal) stays the composer's RAM path — a co-location FAST PATH, not a
	// replacement. BACKEND_API on the gates so the routing decision is unit-testable across the DLL.
	static BACKEND_API bool  CanColocateJoin(const ibDataQuerySpec& spec);
	static ibDataQueryResult ExecuteColocatedJoin(const ibDataQuerySpec& spec, const ibReadPageRequest& page);

	// Co-located server-side AGGREGATE — the same join-tree gate, terminal = GroupBy()/Sum()/… : the
	// JOIN + GROUP BY + aggregates run in ONE server-side SELECT (vs the composer's RAM fold). Group
	// keys may be reference (grouped by the full spread); aggregate INPUTS stay scalar.
	static BACKEND_API bool  CanColocateAggregate(const ibDataQuerySpec& spec);
	static ibDataQueryResult ExecuteColocatedAggregate(const ibDataQuerySpec& spec);

	// Co-located server-side UNION — the branches (each a real DB table) stack as a SQL UNION ALL of
	// per-branch SELECTs (output columns resolved per branch by NAME, aligned by position); ORDER BY /
	// LIMIT wrap the union in a subquery. Scalar outputs (the common catalog ∪ catalog list); a
	// computed branch is handled by the composer's mixed-promote, not this gate.
	static BACKEND_API bool  CanColocateUnion(const ibDataQuerySpec& spec);
	static ibDataQueryResult ExecuteColocatedUnion(const ibDataQuerySpec& spec, const ibReadPageRequest& page);

	// Totals push-down via GROUP BY ROLLUP (docs/query-language-arc.md §22.1b). CanPushRollupTotals:
	// a single-source DB queryable, scalar group keys / aggregate inputs, AND the connected dialect
	// advertises ROLLUP. ExecuteRollupTotals then runs ONE GROUP BY ROLLUP(keys) + the aggregates +
	// GROUPING(key) flags — the DBMS computes every subtotal level from raw detail (correct for
	// COUNT / AVG) — and assembles the ibSelectorTree node tree from the result. Else the composer
	// RAM-folds the detail. BACKEND_API for the unit test.
	static BACKEND_API bool CanPushRollupTotals(const ibDataQuerySpec& spec);
	static ibSelectorTree   ExecuteRollupTotals(const ibDataQuerySpec& spec);

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
