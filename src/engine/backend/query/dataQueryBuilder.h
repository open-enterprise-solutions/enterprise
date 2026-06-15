#ifndef __DATA_QUERY_BUILDER_H__
#define __DATA_QUERY_BUILDER_H__

// ibDataQueryBuilder — the universal (half-)L3 read entry. ONE door for every
// metadata read (catalog/document lists, hierarchical trees, enums today;
// registers / reports later) — NOT list-specific. Parallel to L2's
// ibDatabaseQueryBuilder: L2 is physical, L3 (this) is metadata.
//
// L3 in one line: built from a HOLDER (to execute) + METADATA (to resolve), it
// generates L2 BY SUBSTITUTING NAMES — metadata names become physical names:
//     metaobject         -> physical table          (GetPhysicalTableName)
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
// reads through the selection's GetValue() (the row guid via the uuid identity column);
// no caller touches the L2 result set. HEADER PURITY: this header names NO L2 type. The L2 cursor
// is pimpl'd inside ibDataQueryResult, the rendered-page cache is opaque (built
// through a factory), the IR-building primitives (the former ibMetaIRBuilder)
// live entirely in the .cpp, and the two private members that traffic in L2 IR
// only forward-declare it. So L3 callers include this without dragging in L2.

#include "queryable.h"                                     // ibBackendQueryable, ibQueryCondition, ibQuerySortItem (L2-free)
#include "queryRamTable.h"                                  // ibQueryRamTable — the raw flat snapshot Execute() returns (L2-free)
#include "querySelectorTree.h"                              // ibSelectorTree — the folded node tree SelectTotals returns (L2-free)

#include <vector>
#include <utility>
#include <memory>

class ibValueMetaObjectAttributeBase;
class ibDatabaseConnectionHolder;
class ibQueryResult;        // L2 cursor — consumed by the DB result source
class ibDataResultSource;   // the selection's backing (DB scan OR RAM table) — pimpl, defined in the .cpp
class ibSelector;           // the traversal a result hands out: result.Select(mode) — defined in querySelector.h
struct ibRenderedPageCache; // build-once page cache — opaque; defined in the .cpp, created via NewPageCache()
struct ibDataQuerySpec;     // the door's accumulated query as a value — defined below the class, read by the provider

// === selection KINDS (two axes) + aggregate / totals PODs ====================================
// At NAMESPACE scope (before ibDataQueryResult, which names them). ibDataQueryBuilder re-exposes the
// aggregate PODs as nested aliases for back-compat (ibDataQueryBuilder::AggregateItem / AggregateFn).
//
// AXIS 1 — TRAVERSAL kind (Select(kind)): Direct (rows as-is) / ByGroups (by levels) /
//   ByGroupsHierarchy (by levels + dimension hierarchy).
// Plain enum (implicitly numeric) with prefixed enumerators — the system-enum convention, so it can be
// reflected to the runtime like the other 30+ enums (ibValueEnumeration needs the enum→number conversion).
enum ibSelectKind { ibSelectKind_Direct, ibSelectKind_ByGroups, ibSelectKind_ByGroupsHierarchy };
// AXIS 2 — DIMENSION kind (TotalBy field): Elements / Hierarchy / HierarchyOnly — how a reference
//   field unfolds (folders).
enum class ibDimensionKind { Elements, Hierarchy, HierarchyOnly };

enum class ibAggregateFn { Sum, Count, Min, Max, Avg };
// One output aggregate (.Sum/.Count…): rolls IN-PLACE into m_col (alias vestigial, used only by the
// flat SelectAggregate path). m_col null = COUNT(*).
// m_path (non-empty) = the aggregate input is a reference DOT-WALK leaf (SUM(Producer.Weight)) — the
// provider joins the path and qualifies m_col (== m_path.back()) by the join alias. Empty = plain column.
// m_expr (set) = the aggregate input is a COMPUTED expression (SUM(Qty * Price)) — the provider lowers
// it via BuildColumnExpr; m_col stays null, m_path empty. Single-source DB aggregates only (gated).
struct ibAggregateItem { ibAggregateFn m_fn; const ibBackendQueryColumn* m_col; wxString m_alias;
                         std::vector<const ibBackendQueryColumn*> m_path;
                         ibQueryColumnExprPtr m_expr; };
// One TotalBy dimension level — the field to roll up by + how it unfolds. Levels apply IN ORDER.
struct ibTotalLevel    { const ibBackendQueryColumn* m_col; ibDimensionKind m_dim; };

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
	bool                 m_lockForUpdate = false;   // pessimistic read-for-update (register set lock) — adds the dialect's row-lock clause

	// --- hierarchy (tree) — optional -------------------------------------
	bool      m_parentFilter = false;   // filter by parent reference (tree mode)
	// The parent-ref COLUMN (preferred — the provider derives the physical field
	// itself); m_parentRefField is the legacy physical name, used when set.
	const ibBackendQueryColumn* m_parentCol = nullptr;
	wxString  m_parentRefField;         // physical reference column (legacy seam)
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
	ibDataQueryResult(ibQueryResult&& cursor, const ibBackendQueryable* queryable);
	// RAM backing — every RAM source rides on ONE table type, ibQueryRamTable (L3's own;
	// names no runtime type): the multi-source JOIN / UNION / aggregate result AND a
	// computed virtual table (slice / balance / turnover / temp, via ComputeRows). The
	// selection walks it with the same Next() / GetValue() surface, so consumers never
	// learn which backing they have. (docs §22.1a, §22.4d, §22.6)
	ibDataQueryResult(ibQueryRamTable&& ramTable, const ibBackendQueryable* queryable);
	~ibDataQueryResult();
	ibDataQueryResult(ibDataQueryResult&&) noexcept;             // moves the unique_ptr backing — noexcept
	ibDataQueryResult& operator=(ibDataQueryResult&&) noexcept;
	ibDataQueryResult(const ibDataQueryResult&)            = delete;
	ibDataQueryResult& operator=(const ibDataQueryResult&) = delete;

	bool Next();                                   // advance the selection

	// Typed value of a COLUMN in the current row, as a ready ibValue. The backing
	// (DB cursor / computed RAM table) recovers the attribute behind the column on its
	// own (no downcast) and materialises — so the reference column yields the row's
	// reference object too, and callers loop ALL columns uniformly. A RAW column (the
	// uuid; a computed balance field) is read straight off the backing by its RawType.
	// The row guid is NO special case: read the uuid identity column —
	//   GetValue(queryable->GetIdentitySort().back().m_col).GetString()
	// (the raw overload takes a column by ref so a temporary works). No GetGuidString.
	ibValue GetValue(const ibBackendQueryColumn* col) const;
	ibValue GetValue(const ibRawDBColumn& rawColumn) const;

	// A result column by its OUTPUT NAME — for aggregate columns (SUM/COUNT/… AS
	// alias) that have no source column. Group columns still read via GetValue(col).
	ibValue GetColumn(const wxString& alias) const;

	// A dot-walk leaf that is a reference / enum / composite — reassembled from its field spread projected
	// under `prefix` (vs GetColumn, which reads one scalar field). An empty / broken ref reads as пусто.
	ibValue GetColumnObject(const wxString& prefix, const ibBackendQueryColumn* col) const;

	// TRAVERSAL — selection = result.Select(mode). Drains the cursor ONCE into a flat snapshot
	// (the materialise-columns the door stamped) and hands it to an ibSelector, which decides HOW to
	// walk it per the mode — flat / hierarchy / hierarchy-only — polymorphically; the caller sees one
	// interface, never which. Configure the fold on the returned Selector (ByParentRef / ByGroups /
	// Aggregating). Consumes the result (the cursor is drained). Defined where ibSelector is complete.
	// (docs/query-language-arc.md §22.1b)
	ibSelector Select(ibSelectKind kind = ibSelectKind::ibSelectKind_Direct);

	// The door stamps the columns a later Select(mode) must materialise (its Select() output list +
	// aggregate inputs) — so the snapshot carries exactly what the fold reads. No-op for the working
	// fetch path (it consumes Next()/GetValue() directly and never calls Select(mode)).
	void SetMaterialiseColumns(std::vector<const ibBackendQueryColumn*> cols);

	// The door also stamps the totals config — the TotalBy dimension levels (in order) + the common
	// totals aggregate set — so result.Select(kind) folds by them automatically (no manual fold on
	// the Selector). (docs/query-language-arc.md §22.1b)
	void SetTotals(std::vector<ibTotalLevel> levels, std::vector<ibAggregateItem> aggregates);

	// The stamped totals config — read by result.Select(kind) to configure the Selector's fold.
	const std::vector<ibTotalLevel>&    TotalLevels()     const { return m_totalLevels; }
	const std::vector<ibAggregateItem>& TotalAggregates() const { return m_totalAggregates; }

	// The door also stamps the SOURCE (holder + queryable + select list + filter) so the Selector can
	// run LAZY sub-selections (selection.Select() re-Executes a node's children) and resolve a
	// reference-dimension's catalog hierarchy. (docs/query-language-arc.md §22.1b)
	void SetSource(ibDatabaseConnectionHolder* holder, const ibBackendQueryable* queryable,
		std::vector<std::pair<const ibBackendQueryColumn*, wxString>> selectCols,
		std::vector<ibQueryCondition> conditions);

private:
	// The backing — a physical DB scan OR a computed RAM table, behind one
	// interface. The selection NEVER branches on which: it forwards to the source.
	// DB and RAM stay fully separate (two source classes in the .cpp), and no
	// consumer — not even this class — learns the backing. (docs §22.4d)
	std::unique_ptr<ibDataResultSource> m_source;
	std::vector<const ibBackendQueryColumn*> m_matColumns;   // columns a Select(mode) drains into the snapshot
	std::vector<ibTotalLevel>    m_totalLevels;              // totals dimensions (stamped by the door)
	std::vector<ibAggregateItem> m_totalAggregates;          // common totals aggregate set
	// source recipe (for lazy sub-selections + reference-dimension resolution)
	ibDatabaseConnectionHolder*  m_srcHolder    = nullptr;
	const ibBackendQueryable*    m_srcQueryable = nullptr;
	std::vector<std::pair<const ibBackendQueryColumn*, wxString>> m_srcSelectCols;
	std::vector<ibQueryCondition> m_srcConditions;
};

// ibRenderedPageCache — build-once cache for a repeated page query (one scroll
// shape across ticks). Held by the persistent list model (via shared_ptr);
// Select() reuses the resolved identity sort + the rendered SQL / bind plan while
// only the external anchor Param vector changes per tick. Self-invalidating on a
// signature mismatch. It stores an L2 ibRenderedQuery, so its DEFINITION lives in
// the .cpp and it is OPAQUE here — the list model holds it through a shared_ptr
// and creates it via ibDataQueryBuilder::NewPageCache(), never naming its layout.

// L3 join kind — the door's own (NOT L2's ibQueryJoinType; the door is L2-blind).
enum class ibQueryJoinKind { Inner, Left, Right, Full };   // Right / Full -> RAM stitch only (co-located does Inner/Left)

// (ibSelectKind / ibDimensionKind — defined at namespace scope above, before ibDataQueryResult.)

// ==========================================================================
// ibQueryNode — the L3 relational TREE the door builds and the composer executes.
// A query is NOT a single source: it is a tree of heterogeneous sources combined
// horizontally (Join) and vertically (Union), nestably. Each LEAF is a queryable
// (catalog / temp table / register slice / external) that vends its own provider;
// the composer realizes the tree over those per-leaf providers (co-locate into one
// SQL where possible, else materialise + stitch). Union-ready by shape — that is
// the whole point (FROM catalog, FROM temp table, then UNION).
// (docs/query-language-arc.md §22.1)
// ==========================================================================
struct ibQueryNode
{
	enum class Kind { Source, Join, Union };
	Kind m_kind = Kind::Source;

	// --- Source (leaf) ---------------------------------------------------
	const ibBackendQueryable* m_queryable = nullptr;   // the source this leaf reads
	wxString                  m_alias;                 // disambiguator (the column prefix)

	// --- Join (horizontal): m_left ⋈ m_right ----------------------------
	std::shared_ptr<ibQueryNode> m_left;
	std::shared_ptr<ibQueryNode> m_right;
	const ibBackendQueryColumn*  m_onLeft  = nullptr;  // explicit on-cols; both null = auto by reference (dot-walk style)
	const ibBackendQueryColumn*  m_onRight = nullptr;
	ibQueryJoinKind              m_joinKind = ibQueryJoinKind::Inner;
	bool                         m_cross    = false;   // CROSS / ON TRUE — no keys, cartesian product (RAM only)

	// --- Union (vertical): N branches of one shape ----------------------
	std::vector<std::shared_ptr<ibQueryNode>> m_parts;
	// Parallel to m_parts: true = this branch was attached with UNION ALL (keep duplicates).
	// A plain-UNION branch (false) DEDUPES the accumulated rows at its operator — SQL semantics
	// (left-assoc: A UNION B UNION ALL C dedupes after B, keeps C's duplicates). parts[0] = true.
	std::vector<bool> m_partAll;

	static std::shared_ptr<ibQueryNode> Source(const ibBackendQueryable* q, const wxString& alias = wxEmptyString) {
		auto n = std::make_shared<ibQueryNode>();
		n->m_kind = Kind::Source; n->m_queryable = q; n->m_alias = alias;
		return n;
	}
};

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
	ibDataQueryBuilder& From(const ibBackendQueryable* queryable, const wxString& alias = wxEmptyString); // primary source -> the relational tree root

	// --- multi-source composition (the crown) ----------------------------
	// Add another source. Join(b) auto-joins by reference (dot-walk style); Join(b,
	// left, right) joins on explicit columns (temp / RAM / arbitrary). Union(b) stacks
	// b vertically (same projection). The door builds an ibQueryNode TREE; the composer
	// realizes it over each leaf's provider — co-located into one SQL where possible,
	// else materialised + stitched. (docs/query-language-arc.md §22.1)
	ibDataQueryBuilder& Join(const ibBackendQueryable* queryable,
	                         ibQueryJoinKind kind = ibQueryJoinKind::Inner,
	                         const wxString& alias = wxEmptyString);                            // auto-join by reference
	ibDataQueryBuilder& Join(const ibBackendQueryable* queryable,
	                         const ibBackendQueryColumn* onLeft, const ibBackendQueryColumn* onRight,
	                         ibQueryJoinKind kind = ibQueryJoinKind::Inner,
	                         const wxString& alias = wxEmptyString);                            // explicit on-columns
	ibDataQueryBuilder& CrossJoin(const ibBackendQueryable* queryable,
	                              ibQueryJoinKind kind = ibQueryJoinKind::Inner,
	                              const wxString& alias = wxEmptyString);                        // CROSS / ON TRUE — cartesian
	// Union(b) stacks vertically. keepDuplicates = UNION ALL (default — the historic stack
	// behaviour); false = plain UNION, deduplicating the accumulated rows at this operator.
	ibDataQueryBuilder& Union(const ibBackendQueryable* queryable, const wxString& alias = wxEmptyString,
	                          bool keepDuplicates = true);
	ibDataQueryBuilder& Where(const ibBackendQueryColumn* col,
	                          ibComparisonType comparison, const ibValue& value);            // condition -> predicate
	ibDataQueryBuilder& Where(const ibBackendQueryColumn* col, const ibValue& value);        // 2-arg = equality (meta column)
	ibDataQueryBuilder& Where(const ibRawDBColumn& rawColumn, const ibValue& value);         // direct field = value (door owns a copy)
	ibDataQueryBuilder& WhereLike(const ibBackendQueryColumn* col,
	                              const ibValue& pattern);                                    // col LIKE pattern (FindByCode/Description)
	ibDataQueryBuilder& WhereCompare(const ibBackendQueryColumn* col,
	                                 ibQueryFilterOp op, const ibValue& value);               // col <op> value (ordered: <=, <, >, >=, LIKE)
	// Full boolean WHERE — a predicate TREE (OR / NOT / IS NULL beyond the flat AND-fold of the
	// verbs above). L4 builds it from its parsed WHERE; the provider lowers it to the L2 IR. AND-folded
	// with any verb conditions / row-key filters. (docs/query-language-arc.md §23 — door Where via L2.)
	ibDataQueryBuilder& Where(const ibQueryPredicatePtr& predicate);
	// COMPUTED-expression WHERE — `expr <op> value` (WHERE Qty * Price > 100, a CASE …). The provider
	// lowers the expression via BuildColumnExpr. Single-source DB reads / aggregates only (the L4
	// lowering gates it: the RAM stitch and computed sources do not evaluate condition expressions).
	ibDataQueryBuilder& WhereExpr(const ibQueryColumnExprPtr& expr,
	                              ibComparisonType comparison, const ibValue& value);   // = / <>
	ibDataQueryBuilder& WhereExprCompare(const ibQueryColumnExprPtr& expr,
	                                     ibQueryFilterOp op, const ibValue& value);     // ordered / LIKE
	// Reference DOT-WALK filter / compare — filter the LEAF attribute of a reference path
	// (Producer.Region = value). The provider auto-joins the path (the same join SelectPath builds) and
	// compares on the joined leaf. Single-source read path only. (docs §23 — dot-walk in WHERE.)
	ibDataQueryBuilder& Where(const std::vector<const ibBackendQueryColumn*>& path,
	                          ibComparisonType comparison, const ibValue& value);
	ibDataQueryBuilder& WhereCompare(const std::vector<const ibBackendQueryColumn*>& path,
	                                 ibQueryFilterOp op, const ibValue& value);
	ibDataQueryBuilder& OrderBy(const ibBackendQueryColumn* col, bool ascending);            // col (null = row-key) -> sort
	ibDataQueryBuilder& OrderBy(const std::vector<const ibBackendQueryColumn*>& path, bool ascending); // dot-walk leaf sort
	ibDataQueryBuilder& GroupBy(const ibBackendQueryColumn* col);                            // grouping key (reports / totals)
	ibDataQueryBuilder& GroupBy(const std::vector<const ibBackendQueryColumn*>& path);       // dot-walk grouping key (Producer.Region)

	// --- dot-walk select columns (reference navigation) ------------------
	// Add an output column that walks a reference PATH and reads a leaf attribute on
	// the related object: SelectPath({Producer, Name}, "prod_name") auto-
	// joins the producer catalog and projects its Name AS prod_name. The path
	// is resolved attributes — every NON-leaf segment is a single-target reference
	// attribute (the door resolves its target queryable and joins on the self-reference
	// column); the leaf is read on the final target. Read it back with GetColumn(alias).
	// Paths sharing a prefix reuse one join. Catalog/document targets only today (they
	// carry the self-reference join key). (docs/query-language-arc.md §22 dot-walk)
	ibDataQueryBuilder& SelectPath(const std::vector<const ibBackendQueryColumn*>& path,
	                               const wxString& alias);

	// --- computed output column (arithmetic / CASE) ----------------------
	// Project a COMPUTED expression (a * b, CASE …) under `alias`; read it back with GetColumn(alias).
	// The provider lowers the L3 expression tree to the L2 IR. Single-source read. (docs §23 computed.)
	ibDataQueryBuilder& SelectExpr(const ibQueryColumnExprPtr& expr, const wxString& alias);

	// --- aggregation (totals / totals) ------------------------------------
	// AggregateFn / AggregateItem / ibTotalLevel are defined at namespace scope (above, before
	// ibDataQueryResult). Re-exposed here as nested aliases for back-compat: callers and the SPEC
	// keep naming ibDataQueryBuilder::AggregateItem / ::AggregateFn. (attr = null for Count -> COUNT(*).)
	using AggregateFn   = ibAggregateFn;
	using AggregateItem = ibAggregateItem;
	using ibTotalLevel  = ::ibTotalLevel;

	// HAVING — post-aggregation predicate (stays nested; only the door + provider name it).
	struct HavingItem {
		AggregateFn                 m_fn;
		const ibBackendQueryColumn* m_col;   // null = COUNT(*)
		ibQueryFilterOp             m_op;
		ibValue                     m_value;
	};

	ibDataQueryBuilder& Aggregate(AggregateFn fn, const ibBackendQueryColumn* col, const wxString& alias);
	// Aggregate over a reference DOT-WALK leaf — SUM(Producer.Weight). The provider joins the path and
	// qualifies the leaf by the join alias; read back by alias like any aggregate.
	ibDataQueryBuilder& Aggregate(AggregateFn fn, const std::vector<const ibBackendQueryColumn*>& path, const wxString& alias);
	// Aggregate over a COMPUTED expression — SUM(Qty * Price). The provider lowers the input via
	// BuildColumnExpr. Single-source DB aggregates only (gated at the L4 lowering).
	ibDataQueryBuilder& Aggregate(AggregateFn fn, const ibQueryColumnExprPtr& expr, const wxString& alias);
	ibDataQueryBuilder& Sum  (const ibBackendQueryColumn* col, const wxString& alias) { return Aggregate(AggregateFn::Sum,   col,     alias); }
	ibDataQueryBuilder& Min  (const ibBackendQueryColumn* col, const wxString& alias) { return Aggregate(AggregateFn::Min,   col,     alias); }
	ibDataQueryBuilder& Max  (const ibBackendQueryColumn* col, const wxString& alias) { return Aggregate(AggregateFn::Max,   col,     alias); }
	ibDataQueryBuilder& Avg  (const ibBackendQueryColumn* col, const wxString& alias) { return Aggregate(AggregateFn::Avg,   col,     alias); }
	ibDataQueryBuilder& Count(const wxString& alias)                                  { return Aggregate(AggregateFn::Count, nullptr, alias); }

	// In-place overloads — the aggregate rolls INTO its own column (no alias). Routed to the CURRENT
	// aggregate context (Group() = the GroupBy set / Totals() = the TotalBy set).
	ibDataQueryBuilder& Sum  (const ibBackendQueryColumn* col) { return Aggregate(AggregateFn::Sum,   col, wxEmptyString); }
	ibDataQueryBuilder& Min  (const ibBackendQueryColumn* col) { return Aggregate(AggregateFn::Min,   col, wxEmptyString); }
	ibDataQueryBuilder& Max  (const ibBackendQueryColumn* col) { return Aggregate(AggregateFn::Max,   col, wxEmptyString); }
	ibDataQueryBuilder& Avg  (const ibBackendQueryColumn* col) { return Aggregate(AggregateFn::Avg,   col, wxEmptyString); }
	ibDataQueryBuilder& Count(const ibBackendQueryColumn* col) { return Aggregate(AggregateFn::Count, col, wxEmptyString); }

	// --- totals structure: two aggregate CONTEXTS + dimension levels ----------
	// Group()  switches subsequent .Sum/.Count… into the GROUPBY common aggregate set;
	// Totals() switches them into the TOTALBY common aggregate set. Each set is COMMON across its
	// levels — a level has no own aggregates. (docs/query-language-arc.md §22.1b)
	ibDataQueryBuilder& Group()  { m_aggInTotals = false; return *this; }
	ibDataQueryBuilder& Totals() { m_aggInTotals = true;  return *this; }

	// SELECT DISTINCT — collapse duplicate output rows (SELECT DISTINCT). Applies to the read.
	ibDataQueryBuilder& Distinct() { m_distinct = true; return *this; }
	// Row limit for the AGGREGATE terminal (SELECT TOP n + GROUP BY): the DB path renders the
	// dialect LIMIT, the RAM fold truncates after grouping. Plain reads ride the page request
	// instead (ibReadPageRequest::m_count) — this is only for the unpaged SelectAggregate.
	ibDataQueryBuilder& Top(long count) { m_top = count; return *this; }
	// One totals dimension level — the field to roll up by + how it unfolds. Levels apply IN ORDER.
	ibDataQueryBuilder& TotalBy(const ibBackendQueryColumn* col, ibDimensionKind dim);
	ibDataQueryBuilder& TotalBy(const std::vector<const ibBackendQueryColumn*>& path, ibDimensionKind dim);   // dot-walk dimension
	// Dot-walk dimension with a caller-owned SYNTHETIC dimension column: the provider joins the path and
	// projects the leaf's scalar value under `alias` (collision-free vs the main table), and the fold groups
	// by `dimCol` (which reads that alias) under its OWN unique model id — so a self-referential dimension
	// (Parent.Code) does not clash by metaID with the row's own attribute. The caller owns dimCol.
	ibDataQueryBuilder& TotalByDotWalk(const std::vector<const ibBackendQueryColumn*>& path,
		const ibBackendQueryColumn* dimCol, const wxString& alias, ibDimensionKind dim);

	// HAVING — post-aggregation filter on an aggregate (HAVING SUM(qty) > 100). It
	// references the aggregate EXPRESSION (portable), not the output alias. Several
	// Having() are AND-folded. op = the ordered/LIKE filter op (>, >=, <, <=, LIKE).
	ibDataQueryBuilder& Having(AggregateFn fn, const ibBackendQueryColumn* col,
	                           ibQueryFilterOp op, const ibValue& value);

	// --- explicit output list (multi-source) -----------------------------
	// Select(col, alias) names ONE output column of the (joined / unioned) result, read
	// back by GetColumn(alias). Single-source reads need no select-list (consumers loop
	// attributes via GetValue); a JOIN/UNION composes columns from several leaves, so the
	// output is the select-list — each value pulled from the leaf that owns the column,
	// projected under `alias` (caller-unique). (docs/query-language-arc.md §22.1b)
	ibDataQueryBuilder& Select(const ibBackendQueryColumn* col, const wxString& alias);

	// Row-identity (guidName) lookups — by the row's OWN key, not an attribute.
	// WhereKey: one row; WhereKeyIn: a set (rendered as OR-of-equals).
	ibDataQueryBuilder& WhereKey(const ibGuid& rowGuid);
	ibDataQueryBuilder& WhereKeyIn(const std::vector<ibGuid>& rowGuids);

	// --- write surface (by COLUMN — no statement, no positions) --------
	// SetValue accumulates one COLUMN assignment for INSERT / UPSERT (the values to write,
	// including the key columns: uuid via a raw column, the data-reference / a register's
	// recorder/period/dimensions via their attributes). The same door you read through writes
	// through: From(queryable) + SetValue(col, value)* + Insert()/Upsert(); DELETE keys off
	// Where() only. The UPSERT match is the QUERYABLE's uniqueness key (GetPrimaryKeyColumns — a
	// record's data-reference, a register's composite), NOT a per-column flag. The uuid is the read
	// keyset (GetIdentitySort). The provider does the per-column physical decomposition + positional bind
	// DEEP in L2 (ExecuteWrite builds an ibQueryStatement and calls SetValueAttribute); no field
	// / position is ever visible here. The column is the ibBackendQueryColumn abstraction (an
	// attribute IS one; a raw db field — ibRawDBColumn — is one too). (docs §22.1)
	ibDataQueryBuilder& SetValue(const ibBackendQueryColumn* column, const ibValue& value);
	ibDataQueryBuilder& SetValue(const ibRawDBColumn& rawColumn, const ibValue& value);   // direct field (door owns a copy)

	// Effective sort order = user sort ++ the queryable's identity tail (deduped
	// by metaID; the row-key sentinel kept once). This is the cursor's TOTAL
	// order — Select() uses it internally, and a caller that builds a keyset
	// anchor iterates it to produce the per-column anchor values in EXACTLY the
	// order BuildAnchorPredicate binds them.
	static std::vector<ibQuerySortItem> EffectiveSort(const ibBackendQueryable* queryable,
	                                                  const std::vector<ibQuerySortItem>& userSorts);

	// --- terminal --------------------------------------------------------
	// Run the SELECT and open the selection. Borrows the holder through L2,
	// runs, and returns the L3 selection (ibDataQueryResult) whose dtor releases
	// the reservation. For a read query, run + open collapse here (no L2 cursor
	// leaks out). Move-only result.
	[[nodiscard]] ibDataQueryResult Execute(const ibReadPageRequest& request) const;

	// Build-once overload: reuses `cache` when `signature` matches its last
	// build, otherwise resolves identity + builds IR + renders and refills the
	// cache. Only the external anchor is rebound per tick. `signature` MUST
	// capture every SQL-determining input (the caller owns that contract).
	[[nodiscard]] ibDataQueryResult Execute(const ibReadPageRequest& request,
	                                       ibRenderedPageCache& cache,
	                                       const wxString& signature) const;

	// Aggregated terminal — a FLAT GROUP BY built from GroupBy() + Sum()/Count()/… +
	// Where()/Having(). NOT paged: returns the full grouped set (one row per group). In
	// the result, group columns read through GetValue(attr); aggregate columns through
	// GetColumn(alias). Single-source = physical DB GROUP BY; multi-source = RAM.
	[[nodiscard]] ibDataQueryResult SelectAggregate() const;

	// Totals terminal (hierarchical totals) — returns the RAW totals TREE (ibSelectorTree), NOT a
	// flat group set and NOT a paged cursor. The GroupBy() columns are the LEVELS in order
	// (TOTALS field1, field2), Sum()/… the folded aggregates (BY sum1, sum2); the
	// root is the grand total, each level a subtotal node. L3 stops at the tree —
	// flat-vs-hierarchical rendering is the runtime's call (it walks Root()/Node and
	// builds its own model; L3 names no runtime type but ibValue). (docs §22.1b)
	[[nodiscard]] ibSelectorTree SelectTotals() const;

	// --- introspection (a nested subquery reads its inner query's shape) -----
	// The explicit Select(col, alias) output list (empty = SELECT *), and the PRIMARY
	// source (.From()). A subquery-as-source derives its columns from these: the select
	// list when given, else the primary source's full column set. (docs §22 nested subquery)
	const std::vector<std::pair<const ibBackendQueryColumn*, wxString>>& GetSelectColumns() const { return m_selectCols; }
	const ibBackendQueryable* GetPrimarySource() const { return m_queryable; }
	// Aggregate-shape introspection — an AGGREGATE subquery (FROM (SELECT …, SUM(x) … GROUP BY …))
	// derives its exposed columns from these: the GROUP BY keys + one synthetic column per aggregate
	// alias; its ComputeRows runs SelectAggregate instead of a plain read. (docs §23 — agg subquery)
	const std::vector<AggregateItem>&                GetAggregates() const { return m_aggregates; }
	const std::vector<const ibBackendQueryColumn*>&  GetGroupBy()    const { return m_groupBy; }

	// Factory for the opaque build-once cache — the list model owns the result via
	// shared_ptr. Defined in the .cpp where ibRenderedPageCache is complete, so
	// callers never need its layout (header purity).
	static std::shared_ptr<ibRenderedPageCache> NewPageCache();

	// Write terminals — NO arguments: the target row's identity is already in the builder state
	// (the key columns SetValue()'d for INSERT/UPSERT; the Where() conditions for DELETE), and
	// the UPSERT match is the queryable's uniqueness key. Mirrors L2's fluent
	// ibDatabaseQueryBuilder, at the metadata level. False on a DB error.
	//   Insert — INSERT the row from ALL SetValue() assignments (key columns included).
	//   Upsert — INSERT-or-update (the dialect closes the UPSERT spelling); match on the key.
	//   Delete — DELETE the row(s) WHERE the primary key matches (row-key value / key attrs).
	bool Insert() const;
	bool Upsert() const;
	bool Delete() const;

	// L3-native write kind — the public surface never names the L2 statement
	// (ibQueryStatement::Kind is translated to this only inside the .cpp).
	enum class WriteKind { Insert, Upsert, Delete };

private:
	// The read lowering (IR build + anchor binds) lives in the DB provider now
	// (ibDbTableProvider in the .cpp), built from this state per Select. The
	// querybuilder holds only the metadata query; see docs/query-language-arc.md §22.
	ibDatabaseConnectionHolder*  m_holder;          // threaded down — L2 borrows it per fetch
	const ibBackendQueryable*    m_queryable = nullptr;  // .From() — the PRIMARY source (single-source fast path)
	std::shared_ptr<ibQueryNode> m_root;            // the relational TREE (.From / .Join / .Union) — the composer executes it
	std::vector<ibQueryCondition> m_conditions;     // .Where() (+ .WhereKey: null-attr = row-key)
	ibQueryPredicatePtr           m_predicate;      // .Where(tree) — full boolean WHERE (OR/NOT/IS NULL); AND-folded with m_conditions
	std::vector<ibQuerySortItem>  m_sorts;          // .OrderBy() — USER sort; identity tail appended in Select
	std::vector<const ibBackendQueryColumn*> m_groupBy;   // .GroupBy()
	std::vector<std::vector<const ibBackendQueryColumn*>> m_groupPaths;   // parallel to m_groupBy: dot-walk path (empty = plain column)
	std::vector<ibValue>          m_keyIn;          // .WhereKeyIn() — row-key IN (OR-of-equals)
	std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> m_writeValues;   // .SetValue() — write assignments (key + data columns)
	std::vector<AggregateItem>    m_aggregates;     // .Group().Sum()… — GroupBy common aggregate set
	std::vector<HavingItem>       m_having;         // .Having()
	std::vector<ibTotalLevel>     m_totals;         // .TotalBy(field, dim) — totals dimensions, in order
	std::vector<AggregateItem>    m_totalAggregates;// .Totals().Sum()… — TotalBy common aggregate set
	bool                          m_aggInTotals = false;  // routing flag: .Aggregate → totals set when Totals() active
	bool                          m_distinct    = false;  // .Distinct() — SELECT DISTINCT
	long                          m_top         = 0;      // .Top() — aggregate-terminal row limit (0 = all groups)
	std::vector<ibDotWalkColumn>  m_dotWalks;       // .SelectPath()
	std::vector<ibDotWalkColumn>  m_dimWalks;       // .TotalByDotWalk() — a dot-walk TOTALS dimension: the provider
	                                                //   joins the path and projects the leaf's SCALAR value under
	                                                //   m_alias (distinct), read by a synthetic dimension column.
	std::vector<ibQueryColumnSelect> m_selectExprs; // .SelectExpr() — computed output columns (arithmetic / CASE)
	std::vector<std::pair<const ibBackendQueryColumn*, wxString>> m_selectCols;   // .Select(col, alias) — multi-source output list

	// Raw (direct) columns the door was handed by value — owned here so the column pointers
	// stored above (conditions / writeValues / selectCols) stay valid for the query's life.
	// shared_ptr (not by-value vector) keeps the pointers stable across reallocation AND keeps
	// the door COPYABLE (ibSubqueryQueryable copies the inner builder) — a copied door shares
	// these immutable column descriptors, so the copy's stored pointers stay alive.
	std::vector<std::shared_ptr<ibRawDBColumn>> m_ownedRawColumns;

	// Bundle the accumulated state into the value the provider reads (a short-lived
	// view of refs). The door builds it per terminal and hands it to the vended
	// provider — so the door names no L2 and the provider never reaches into the door.
	ibDataQuerySpec BuildSpec() const;

	// Columns a later result.Select(mode) drains into its snapshot (Select() list + agg inputs + TotalBy fields).
	std::vector<const ibBackendQueryColumn*> MaterialiseColumns() const;
	// Stamp the materialise-columns + totals config onto a freshly executed result.
	void StampResult(ibDataQueryResult& r) const;
};

// ==========================================================================
// ibDataQuerySpec — the door's accumulated query as a VALUE the provider reads.
// NO "meta" in the name: this is L3 (the door), which is metadata-BLIND — it speaks
// columns / queryable, builds this, and hands it to the provider it pulls from the
// queryable (ibBackendQueryable::GetProvider()). The provider owns ALL the L2 and the
// metadata->physical lowering. A view of refs into the door, valid for the single
// terminal call that built it. (docs/query-language-arc.md §22.2)
// ==========================================================================
struct ibDataQuerySpec
{
	ibDatabaseConnectionHolder*                     m_holder      = nullptr;
	const ibBackendQueryable*                       m_queryable   = nullptr;   // the CURRENT source a leaf provider reads (single-source = the one source)
	const ibQueryNode*                              m_root        = nullptr;   // the relational TREE — the composer walks it (null = pure single-source)
	const std::vector<ibQueryCondition>*            m_conditions  = nullptr;
	ibQueryPredicatePtr                             m_predicate;               // .Where(tree) — full boolean WHERE (null = none)
	const std::vector<ibValue>*                     m_keyIn       = nullptr;
	const std::vector<ibQuerySortItem>*             m_sorts       = nullptr;
	const std::vector<const ibBackendQueryColumn*>* m_groupBy     = nullptr;
	const std::vector<std::vector<const ibBackendQueryColumn*>>* m_groupPaths = nullptr;   // parallel to m_groupBy: dot-walk path per key (empty = plain)
	const std::vector<ibDataQueryBuilder::AggregateItem>* m_aggregates = nullptr;
	const std::vector<ibDataQueryBuilder::HavingItem>*    m_having     = nullptr;
	const std::vector<std::pair<const ibBackendQueryColumn*, ibValue>>* m_writeValues = nullptr;
	const std::vector<ibDotWalkColumn>*             m_dotWalks    = nullptr;
	const std::vector<ibDotWalkColumn>*             m_dimWalks    = nullptr;   // dot-walk TOTALS dimensions
	const std::vector<ibQueryColumnSelect>*         m_selectExprs = nullptr;   // computed output columns (arithmetic / CASE)
	const std::vector<std::pair<const ibBackendQueryColumn*, wxString>>* m_selectCols = nullptr;   // multi-source output list
	bool                                            m_distinct    = false;   // .Distinct() — SELECT DISTINCT
	long                                            m_topCount    = 0;       // .Top() — aggregate-terminal row limit (0 = all groups)
};

#endif
