#ifndef __DATA_QUERY_BUILDER_H__
#define __DATA_QUERY_BUILDER_H__

// ibDataQueryBuilder — the universal (half-)L3 read entry. ONE door for every
// metadata read (catalog/document lists, hierarchical trees, enums today;
// registers / reports later) — NOT list-specific. Parallel to L2's
// ibDatabaseQueryBuilder: L2 is physical, L3 (this) is metadata.
//
// L3 in one line: built from a HOLDER (to execute) + METADATA (to resolve), it
// generates L2 BY SUBSTITUTING NAMES — metadata names become physical names:
//     metaobject         -> physical table          (GetTableNameDB)
//     attribute (metaID)  -> physical SQL field(s)    (_N / _S / _R)
//     reference / parent  -> guidName / _RRRef blob
// A metadata predicate "attribute X = value" becomes the L2 IR
// "physical_field_X = Const". That name substitution IS L3's job; the resulting
// ibQueryIR is metadata-free, so L2 stays metadata-blind. The substitution
// primitives live in ibMetaIRBuilder.
//
// Lives in backend/query/ — the dedicated L3 home (the future L4 text parser
// lands here too and lowers into this entry; the runtime invokes it).
//
// PERFORMANCE (docs/query-language-arc.md §19) — under every scroll tick:
//   * cacheable, no connection: the metadata resolution (the model/buffer holds
//     an ibDataQueryBuilder across ticks; rebuilds only on filter/sort change);
//   * per-fetch, borrow→run→release: FetchPage builds the page IR (cheap), runs
//     via L2, returns the RAII cursor whose dtor releases the holder reservation
//     (§12 leak protection). No connection pinned across ticks (keyset paging);
//   * the cursor anchor rides as a bound PARAM, so the SQL text is byte-identical
//     across ticks in one direction (driver prepared-statement cache hits).
//
// Row materialisation is fully L3: every consumer (list / tree / enum / register)
// reads through the selection's GetValue() / GetGuidString(); no caller touches
// the L2 result set. HEADER PURITY: this header names NO L2 type. The L2 cursor
// is pimpl'd inside ibDataQueryResult, the rendered-page cache is opaque (built
// through a factory), the IR-building primitives (the former ibMetaIRBuilder)
// live entirely in the .cpp, and the two private members that traffic in L2 IR
// only forward-declare it. So L3 callers include this without dragging in L2.

#include "queryable.h"                                     // ibBackendQueryable, ibQueryCondition, ibQuerySortItem (L2-free)

#include <vector>
#include <utility>
#include <memory>

class ibValueMetaObjectAttributeBase;
class ibDatabaseConnectionHolder;
class ibQueryResult;        // L2 cursor — consumed by the DB result source
class ibMetaResultSource;   // the selection's backing (DB scan OR RAM table) — pimpl, defined in the .cpp
struct ibRenderedPageCache; // build-once page cache — opaque; defined in the .cpp, created via NewPageCache()

// A dot-walk select column — a reference-navigation PATH of query columns + the
// output alias the leaf is projected under. The path's non-leaf segments are single-
// target reference columns (each resolved to its target queryable and joined on the
// self-reference key); the leaf is read on the final target. Trafficking in the L3
// column abstraction (ibBackendQueryColumn), not the concrete metaclass. (docs §22)
struct ibDotWalkColumn
{
	std::vector<const ibBackendQueryColumn*> m_path;    // ref segments + leaf column
	wxString                                 m_alias;   // output name (read via GetColumn)
};

// One page request — universal across read modes. The caller fills the fields
// its mode needs; the rest stay defaulted.
struct ibReadPageRequest
{
	ibFetchDirection     m_direction = ibFetchDirection::Forward;
	bool                 m_hasAnchor = false;       // cursor present? (first page = false)
	std::vector<ibValue> m_anchorSortValues;        // anchor value per sort column, in order
	wxString             m_anchorGuid;              // anchor row guid — ONLY for a guidName tiebreaker
	                                                // (catalog); register identity is in the sort cols
	int                  m_count = 0;               // batch size (caller adds its own +1 probe)
	bool                 m_reverseSort = false;     // backward-walk flips ASC/DESC

	// --- hierarchy (tree) — optional -------------------------------------
	bool      m_parentFilter = false;   // filter by parent reference (tree mode)
	wxString  m_parentRefField;         // physical reference column
	ibGuid    m_parentGuid;             // parent row guid (empty/invalid + isTopLevel = top)
	bool      m_isTopLevel  = false;    // top-level rows (empty parent)
	bool      m_flatScan    = false;    // flat-list view of a tree: skip the parent filter
};

// ibDataQueryResult — the L3 selection. Wraps the L2 cursor and yields READY
// ibValue rows (reference reconstruction, composite assembly) — consumers never
// touch the raw driver result set. The L2 ibQueryResult is held through a
// unique_ptr (pimpl) so this header forward-declares it rather than including
// L2; the cursor's dtor (run when this selection dies) releases the holder
// reservation. Move-only — the special members are out-of-line (defined where
// ibQueryResult is complete).
class BACKEND_API ibDataQueryResult
{
public:
	// DB backing — wraps the L2 cursor (physical scan).
	ibDataQueryResult(ibQueryResult&& cursor, const ibBackendQueryable* meta);
	// RAM backing — a computed virtual table (slice / balance / turnover). The
	// ibValue owns the rows (an ibValueModelTable); the selection walks them with
	// the same Next() / GetValue() surface, so consumers never learn which backing
	// they have. (docs/query-language-arc.md §22.4d)
	ibDataQueryResult(ibValue ramTable, const ibBackendQueryable* meta);
	~ibDataQueryResult();
	ibDataQueryResult(ibDataQueryResult&&) noexcept;             // moves the unique_ptr backing — noexcept
	ibDataQueryResult& operator=(ibDataQueryResult&&) noexcept;
	ibDataQueryResult(const ibDataQueryResult&)            = delete;
	ibDataQueryResult& operator=(const ibDataQueryResult&) = delete;

	bool Next();                                   // advance the selection

	// Row-key string of the current row (catalog: guidName — row ctors take it;
	// register: empty, the register consumer assembles its composite identity).
	wxString GetGuidString() const;

	// Metadata-typed value of an attribute in the current row, as a ready
	// ibValue. The reference attribute is handled too: for it, GetValue returns
	// the row's reference object (wrapped in an ibValue) — so callers loop ALL
	// attributes uniformly, no special-casing the reference column.
	ibValue GetValue(const ibValueMetaObjectAttributeBase* attr) const;

	// A result column by its OUTPUT NAME — for aggregate columns (SUM/COUNT/… AS
	// alias) that have no source attribute. Group columns still read via GetValue(attr).
	ibValue GetColumn(const wxString& alias) const;

private:
	// The backing — a physical DB scan OR a computed RAM table, behind one
	// interface. The selection NEVER branches on which: it forwards to the source.
	// DB and RAM stay fully separate (two source classes in the .cpp), and no
	// consumer — not even this class — learns the backing. (docs §22.4d)
	std::unique_ptr<ibMetaResultSource> m_source;
};

// ibRenderedPageCache — build-once cache for a repeated page query (one scroll
// shape across ticks). Held by the persistent list model (via shared_ptr);
// Select() reuses the resolved identity sort + the rendered SQL / bind plan while
// only the external anchor Param vector changes per tick. Self-invalidating on a
// signature mismatch. It stores an L2 ibRenderedQuery, so its DEFINITION lives in
// the .cpp and it is OPAQUE here — the list model holds it through a shared_ptr
// and creates it via ibDataQueryBuilder::NewPageCache(), never naming its layout.

class BACKEND_API ibDataQueryBuilder
{
public:
	// Default — pulls the holder from the CURRENT SESSION. The query shape is
	// set fluently after construction (mirroring L2's ibDatabaseQueryBuilder);
	// nothing is crammed into the constructor.
	ibDataQueryBuilder();
	// Explicit holder — overrides the session default (sessionless subsystems).
	explicit ibDataQueryBuilder(ibDatabaseConnectionHolder* holder);

	// --- fluent shape (chainable) — mirrors L2's verbs, at the metadata level.
	// Fields are identified BY ATTRIBUTE (already resolved; the L4 text parser is
	// what turns a name string into the attribute). NO ibFilterRow/ibSortOrder —
	// those are the dynamic-list (table) layer's types; that layer translates its
	// filter/sort into Where/OrderBy here. (SetParameter / &Param is an L4 thing:
	// at L3 the value goes straight into Where.)
	ibDataQueryBuilder& From(const ibBackendQueryable* meta);                                // queryable -> physical table
	ibDataQueryBuilder& Where(const ibValueMetaObjectAttributeBase* attr,
	                          ibComparisonType comparison, const ibValue& value);            // condition -> predicate
	ibDataQueryBuilder& WhereLike(const ibValueMetaObjectAttributeBase* attr,
	                              const ibValue& pattern);                                    // attr LIKE pattern (FindByCode/Description)
	ibDataQueryBuilder& WhereCompare(const ibValueMetaObjectAttributeBase* attr,
	                                 ibQueryFilterOp op, const ibValue& value);               // attr <op> value (ordered: <=, <, >, >=, LIKE)
	ibDataQueryBuilder& OrderBy(const ibValueMetaObjectAttributeBase* attr, bool ascending); // attr (null = row-key) -> sort
	ibDataQueryBuilder& GroupBy(const ibValueMetaObjectAttributeBase* attr);                 // grouping key (reports / ИТОГИ)

	// --- dot-walk select columns (reference navigation) ------------------
	// Add an output column that walks a reference PATH and reads a leaf attribute on
	// the related object: SelectPath({Производитель, Наименование}, "prod_name") auto-
	// joins the producer catalog and projects its Наименование AS prod_name. The path
	// is resolved attributes — every NON-leaf segment is a single-target reference
	// attribute (the door resolves its target queryable and joins on the self-reference
	// column); the leaf is read on the final target. Read it back with GetColumn(alias).
	// Paths sharing a prefix reuse one join. Catalog/document targets only today (they
	// carry the self-reference join key). (docs/query-language-arc.md §22 dot-walk)
	ibDataQueryBuilder& SelectPath(const std::vector<const ibBackendQueryColumn*>& path,
	                               const wxString& alias);

	// --- aggregation (ИТОГИ / totals) ------------------------------------
	// GroupBy(attr)* + an aggregate per output column. When any group key is set,
	// Select lowers to a GROUP BY query: the group attributes are the group columns
	// (read back through GetValue(attr)); each aggregate becomes SUM/COUNT/… AS alias
	// (read back through GetColumn(alias)). attr = null for Count -> COUNT(*).
	enum class AggregateFn { Sum, Count, Min, Max, Avg };
	ibDataQueryBuilder& Aggregate(AggregateFn fn, const ibValueMetaObjectAttributeBase* attr, const wxString& alias);
	ibDataQueryBuilder& Sum  (const ibValueMetaObjectAttributeBase* attr, const wxString& alias) { return Aggregate(AggregateFn::Sum,   attr,    alias); }
	ibDataQueryBuilder& Min  (const ibValueMetaObjectAttributeBase* attr, const wxString& alias) { return Aggregate(AggregateFn::Min,   attr,    alias); }
	ibDataQueryBuilder& Max  (const ibValueMetaObjectAttributeBase* attr, const wxString& alias) { return Aggregate(AggregateFn::Max,   attr,    alias); }
	ibDataQueryBuilder& Avg  (const ibValueMetaObjectAttributeBase* attr, const wxString& alias) { return Aggregate(AggregateFn::Avg,   attr,    alias); }
	ibDataQueryBuilder& Count(const wxString& alias)                                              { return Aggregate(AggregateFn::Count, nullptr, alias); }

	// HAVING — post-aggregation filter on an aggregate (HAVING SUM(qty) > 100). It
	// references the aggregate EXPRESSION (portable), not the output alias. Several
	// Having() are AND-folded. op = the ordered/LIKE filter op (>, >=, <, <=, LIKE).
	ibDataQueryBuilder& Having(AggregateFn fn, const ibValueMetaObjectAttributeBase* attr,
	                           ibQueryFilterOp op, const ibValue& value);

	// Row-identity (guidName) lookups — by the row's OWN key, not an attribute.
	// WhereKey: one row; WhereKeyIn: a set (rendered as OR-of-equals).
	ibDataQueryBuilder& WhereKey(const ibGuid& rowGuid);
	ibDataQueryBuilder& WhereKeyIn(const std::vector<ibGuid>& rowGuids);

	// --- write surface (by ATTRIBUTE — no statement, no positions) --------
	// SetValue accumulates an assignment for the write terminal. The same door
	// you read through writes through: From(meta) + SetValue(attr, value)* +
	// Upsert(rowKey). The per-attribute -> physical-field decomposition and the
	// positional bind happen inside (an L2 ibQueryStatement no one sees here).
	ibDataQueryBuilder& SetValue(const ibValueMetaObjectAttributeBase* attr, const ibValue& value);

	// Effective sort order = user sort ++ the queryable's identity tail (deduped
	// by metaID; the row-key sentinel kept once). This is the cursor's TOTAL
	// order — Select() uses it internally, and a caller that builds a keyset
	// anchor iterates it to produce the per-column anchor values in EXACTLY the
	// order BuildAnchorPredicate binds them.
	static std::vector<ibQuerySortItem> EffectiveSort(const ibBackendQueryable* meta,
	                                                  const std::vector<ibQuerySortItem>& userSorts);

	// --- terminal --------------------------------------------------------
	// Run the SELECT and open the selection. Borrows the holder through L2,
	// runs, and returns the L3 selection (ibDataQueryResult) whose dtor releases
	// the reservation. For a read query, run + open collapse here (no L2 cursor
	// leaks out). Move-only result.
	[[nodiscard]] ibDataQueryResult Select(const ibReadPageRequest& request) const;

	// Build-once overload: reuses `cache` when `signature` matches its last
	// build, otherwise resolves identity + builds IR + renders and refills the
	// cache. Only the external anchor is rebound per tick. `signature` MUST
	// capture every SQL-determining input (the caller owns that contract).
	[[nodiscard]] ibDataQueryResult Select(const ibReadPageRequest& request,
	                                       ibRenderedPageCache& cache,
	                                       const wxString& signature) const;

	// Aggregated terminal (ИТОГИ / totals) — runs the GROUP BY query built from
	// GroupBy() + Sum()/Count()/… + Where()/OrderBy(). NOT paged: returns the full
	// grouped set (totals are small). In the result, group columns read through
	// GetValue(attr); aggregate columns through GetColumn(alias). Always physical
	// (a DB GROUP BY), so it bypasses the keyset paging provider.
	[[nodiscard]] ibDataQueryResult SelectAggregate() const;

	// Factory for the opaque build-once cache — the list model owns the result via
	// shared_ptr. Defined in the .cpp where ibRenderedPageCache is complete, so
	// callers never need its layout (header purity).
	static std::shared_ptr<ibRenderedPageCache> NewPageCache();

	// Write terminal — INSERT-or-update the row keyed by `rowKey`, from the
	// SetValue() assignments. The dialect closes the UPSERT spelling; the
	// attribute decomposition reuses SetValueAttribute under the hood. Returns
	// false on a DB error (caller treats as save failure). Symmetric to Select().
	bool Upsert(const ibGuid& rowKey) const;

	// Delete terminal — remove the row keyed by `rowKey`. Symmetric to Upsert;
	// no statement / L2 visible at the call site. False on a DB error.
	bool DeleteByKey(const ibGuid& rowKey) const;

	// L3-native write kind — the public surface never names the L2 statement
	// (ibQueryStatement::Kind is translated to this only inside the .cpp).
	enum class WriteKind { Insert, Upsert, Delete };

	// L3 write CORE — the single place that owns the column model and POSITIONAL
	// binding, so no writer splits fields or counts positions at its call site.
	// A column is either 1->1 (keyColumn, e.g. uuid — one physical field, bound
	// as a string) or 1->N (an attribute — TYPE + per-type data + reference blob,
	// expanded and bound by SetValueAttribute). WriteRow builds the column list
	// from keyColumn + the assignments' attributes (expansion is INTERNAL), binds
	// keyValue then each assignment value positionally, and runs one L2 statement.
	// Kind::Upsert matches on keyColumn (if any) + matchKeyAttrs' fields;
	// Kind::Delete turns the assignments into the WHERE-equality columns. Used by
	// the door's Upsert/DeleteByKey and by the register / tabular / constant
	// writers — all of which pass ATTRIBUTES, never physical fields.
	static bool WriteRow(WriteKind kind, const wxString& table,
	                     const wxString& keyColumn, const ibValue& keyValue,
	                     const std::vector<std::pair<const ibValueMetaObjectAttributeBase*, ibValue>>& assignments,
	                     const std::vector<const ibValueMetaObjectAttributeBase*>& matchKeyAttrs = {},
	                     ibDatabaseConnectionHolder* holder = nullptr);

private:
	// The read lowering (IR build + anchor binds) lives in the DB provider now
	// (ibDbTableProvider in the .cpp), built from this state per Select. The
	// querybuilder holds only the metadata query; see docs/query-language-arc.md §22.
	ibDatabaseConnectionHolder*  m_holder;          // threaded down — L2 borrows it per fetch
	const ibBackendQueryable*    m_meta = nullptr;  // .From()
	std::vector<ibQueryCondition> m_conditions;     // .Where() (+ .WhereKey: null-attr = row-key)
	std::vector<ibQuerySortItem>  m_sorts;          // .OrderBy() — USER sort; identity tail appended in Select
	std::vector<const ibValueMetaObjectAttributeBase*> m_groupBy;   // .GroupBy()
	std::vector<ibValue>          m_keyIn;          // .WhereKeyIn() — row-key IN (OR-of-equals)
	std::vector<std::pair<const ibValueMetaObjectAttributeBase*, ibValue>> m_writeValues;   // .SetValue() — write assignments

	// .Aggregate() / .Sum() / .Count() … — one per output aggregate column.
	struct AggregateItem {
		AggregateFn                           m_fn;
		const ibValueMetaObjectAttributeBase* m_attr;   // null = COUNT(*)
		wxString                              m_alias;
	};
	std::vector<AggregateItem>    m_aggregates;

	// .Having() — post-aggregation predicates on aggregate expressions (AND-folded).
	struct HavingItem {
		AggregateFn                           m_fn;
		const ibValueMetaObjectAttributeBase* m_attr;   // null = COUNT(*)
		ibQueryFilterOp                       m_op;
		ibValue                               m_value;
	};
	std::vector<HavingItem>       m_having;

	// .SelectPath() — reference-navigation output columns. The join chain is built in
	// the provider (one join per distinct prefix); the leaf projects AS m_alias.
	std::vector<ibDotWalkColumn>  m_dotWalks;
};

#endif
