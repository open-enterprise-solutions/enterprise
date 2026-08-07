#ifndef __QUERYABLE_H__
#define __QUERYABLE_H__

// ibBackendQueryable — the data-navigation interface L3 (ibDataQueryBuilder)
// reads a metaobject through. ONE contract, implemented by the two queryable
// metaobject families:
//   ibValueMetaObjectRecordDataRef  (catalog / document / charts / enums)
//   ibValueMetaObjectRegisterData   (information / accumulation / accounting)
//
// L3 stays family-blind: the physical table, the row-identity (keyset) tail,
// reference/attribute materialisation, and virtual-table availability all come
// through here, so the per-family fork in the query builder dies. This is the
// "internals of the catalog / register" door — and it is where L3 learns
// whether a metaobject has virtual tables (balances / turnovers / slices).
//
// The interface carries NO data and does NOT derive ibValue — it is a pure
// mixin. Implementers list it as a SECOND base (after the ibValue-deriving
// metaobject base) so ibValue stays at offset 0 (see the first-base PMF rule).
//
// See docs/query-language-arc.md §18 (name substitution) / §20 (this interface).

#include "backend/modelView.h"          // ibMetaID (via backend.h) — NOT model.h, so model.h can hold
                                        // ibDataDBComposer BY VALUE (breaks the dataComposer→queryLowering→queryable→
                                        // model include cycle). (ibComparisonType is no longer used here — the
                                        // L3 condition op is ibQueryFilterOp now, defined below.)
#include "backend/compiler/value.h"     // ibValue
#include "queryColumn.h"                // ibBackendQueryColumn (the column counterpart)
#include "queryRamTable.h"              // ibQueryRamTable — ComputeRows produces the L3 table (no runtime type)
#include "backend/databaseLayer/databaseLayer.h"   // ibTotalsPeriod — the unit a PeriodTrunc expression carries

#include <map>
#include <memory>
#include <vector>

// The L2-1 relation tree (databaseQueryBuilder.h). Forward-declared: a queryable may DESCRIBE
// itself as a derived table without this header pulling in the whole L2-1 vocabulary.
struct ibQueryRel;
using  ibQueryRelPtr = std::shared_ptr<ibQueryRel>;

class ibBackendQueryProvider;   // the engine the queryable vends — defined in queryProvider.h; it IS the whole L3<->L2-1 layer
class ibDataQueryBuilder;       // the inner query a system subquery-queryable wraps (defined below) — full type only in its .cpp
class ibMetaData;               // the metadata context — the provider needs it to reconstruct reference / enum values column-based
class ibBackendQueryable;       // defined below — ibTempSourceScope references it by pointer only

// ==========================================================================
// ibTempSourceScope — the AUXILIARY, per-query queryable registry (the L4 seam).
//
// The MAIN source factory resolves metaobjects BY NAME. A transient queryable —
// a RAM table (value table / tabular section) or a temp table — carries no
// metaobject and no registered name, so it cannot be re-resolved that way. The
// composer registers such a queryable HERE under a unique local name (t0, t1, …)
// and renders "FROM Temp.<name>"; ResolveSource consults this registry BEFORE the
// factory and returns the queryable directly. The L3 door then runs it unchanged —
// it already IS a complete L3 queryable, so nothing is registered down at L3.
//
// The registry is THREAD-LOCAL and RAII-scoped to ONE query execution: the composer
// installs it around Execute, and it vanishes when the query finishes — no trace
// left behind (the queryable stays owned by its model). This is the temp-table
// feature: L5 registers at L4, L4 resolves directly, L3 needs nothing.
class BACKEND_API ibTempSourceScope
{
public:
	explicit ibTempSourceScope(const std::map<wxString, const ibBackendQueryable*>& sources);
	~ibTempSourceScope();

	// The active scope's queryable for `name`, or null (no scope / not a temp source).
	static const ibBackendQueryable* Find(const wxString& name);

	ibTempSourceScope(const ibTempSourceScope&) = delete;
	ibTempSourceScope& operator=(const ibTempSourceScope&) = delete;

private:
	const std::map<wxString, const ibBackendQueryable*>* m_prev;
};

class ibMetaData;   // the config a query runs on behalf of — its factory resolves by-name metaobject sources

// ibSourceMetaDataScope — the CONFIG a query runs ON BEHALF OF, for the duration of one execution. The composer sets
// it (from its metadata) before lowering; ResolveSource resolves a by-name metaobject source through THIS config's
// factory (sources register per-config), never the global one. Parallel to ibTempSourceScope; null outside a scope.
class BACKEND_API ibSourceMetaDataScope
{
public:
	explicit ibSourceMetaDataScope(const ibMetaData* metaData);
	~ibSourceMetaDataScope();

	// The config in force for THIS execution, or null (no scope / a sourceless composer).
	static const ibMetaData* Get();

	ibSourceMetaDataScope(const ibSourceMetaDataScope&) = delete;
	ibSourceMetaDataScope& operator=(const ibSourceMetaDataScope&) = delete;

private:
	const ibMetaData* m_prev;
};

// The shared stateless computed (RAM) provider, as a base reference — so a computed queryable
// here can vend it WITHOUT this header naming the concrete ibComputedProvider (which lives in
// the L2-aware queryProvider.h). Defined in queryProvider.cpp. (breaks the include cycle.)
BACKEND_API ibBackendQueryProvider& ibComputedProviderInstance();

// ==========================================================================
// query-native vocabulary — the language between L3 and a queryable. NOT the
// dynamic-list UI types (ibFilterRow / ibSortOrder): the list layer translates
// those into the conditions / sorts below. Field identification is BY
// ATTRIBUTE (already resolved; the L4 text parser converts a name string into
// the attribute / metaID).
// ==========================================================================

// A query-native WHERE condition: a resolved COLUMN, a comparison, a value.
// The field is the L3 COLUMN abstraction (ibBackendQueryColumn), not a concrete
// attribute — the DB provider resolves it back to an attribute (ResolveAttribute by
// name) only when it needs the field-machinery, so a future virtual/temp column with no
// attribute behind it fits the same shape. L3-native filter operators beyond the
// list layer's Equal / NotEqual. Kept here (not L2-1's ibQueryBinOp) so the metadata
// side carries no L2-1 dependency — the query builder translates these to physical IR
// operators. (L3 doesn't pull L2-1 includes; see docs/query-language-arc.md §20, §22.4b.)
// The L3 comparison/filter operator — ONE L3-native enum covering equality AND the ordered/LIKE ops, so the
// crippled 2-value ibComparisonType (Eq/Ne, a leftover from the legacy ibFilterRow) is GONE from the query
// path. Equal/NotEqual are the common case; the rest are the former WhereCompare/WhereLike ops. (Max: "why do you
// drag that clunky ibComparisonType everywhere".)
// `In` is the ONE set-valued op: it reads m_values, NOT m_value (every other op is the reverse). It exists
// for the SEMI-JOIN key reduction — the RAM stitch materialises the cheap side of a join first, collects that
// side's DISTINCT key values, and pushes them as an `In` condition into the other leaf's read, so the leaf
// fetches only rows that can possibly join instead of the whole table. Appended LAST: the enum is runtime-only
// (never serialised), but keeping the existing ordinals stable costs nothing.
enum class ibQueryFilterOp { Equal, NotEqual, Like, Less, LessEqual, Greater, GreaterEqual, In };

struct ibQueryCondition
{
	const ibBackendQueryColumn* m_col = nullptr;   // null = the row-key column; with m_path = the path LEAF
	// THE comparison/filter operator — ONE field now (the former m_comparison Eq/Ne + the m_explicitOp toggle
	// + the m_op Like/Less/... are collapsed). Equal is the default; Where/WhereCompare/WhereLike all just set
	// this. Providers read it directly (FilterOpToBinOp / the RAM switch); no branch.
	ibQueryFilterOp             m_op = ibQueryFilterOp::Equal;
	ibValue                     m_value;

	// The `In` operand set — the ONLY op that reads this instead of m_value. An EMPTY set with m_op == In is
	// the empty IN, i.e. matches nothing (SQL `x IN ()` has no legal spelling, so both renderers short-circuit
	// to FALSE rather than emitting it). NULL never belongs here: a NULL key matches nothing in an equi-join,
	// and `IN (…, NULL)` is the classic SQL trap — the producer strips nulls before filling this.
	std::vector<ibValue>        m_values;

	// Reference DOT-WALK: when non-empty, this condition filters the LEAF attribute of a reference
	// path (Producer.Region -> {Producer, Region}). Every non-leaf segment is a single-target
	// reference column the provider auto-joins (the SAME join SelectPath builds, deduped by prefix);
	// the leaf (== m_col) is compared on the joined target. Empty = a plain column on the main source.
	std::vector<const ibBackendQueryColumn*> m_path;

	// SEMI-JOIN render tag (behavioural, like m_path — NOT provenance): render this dot-walk condition as a
	// correlated EXISTS instead of a projection JOIN, so it FILTERS (once/zero per row) without multiplying.
	// Raised by the RLS decorator (ibValueQueryDecorator) on every condition it folds — so an RLS restriction
	// is a semi-join on READS too, not just writes; a user's own dot-walk stays a JOIN. The provider ORs it
	// with the call-level pathAsExists: (pathAsExists || m_asExists). See access-policy-rls.md (read→EXISTS).
	bool m_asExists = false;

	// COMPUTED left-hand side (WHERE Qty * Price > value, a CASE …): when set, the provider lowers
	// the expression via BuildColumnExpr and compares it to m_value — m_col stays null, m_path empty.
	// Single-source DB reads / aggregates only (the lowering gates it); the RAM stitch and computed
	// sources do not evaluate it. Declared after m_path so the flat struct stays POD-ordered.
	std::shared_ptr<struct ibQueryColumnExpr> m_expr;

	// RLS `restrict … join …` SEMI-JOIN payload: when set, this condition IS a correlated EXISTS over the
	// inner permission source (m_col / m_value / m_path all unused). The provider renders it FIRST in
	// BuildConditionExpr, so it rides EVERY WHERE path (read single / co-located / write / aggregate) — no
	// missed site can leave a write or a join-query unrestricted. See ibSemiJoinExists + access-policy-rls.md.
	std::shared_ptr<struct ibSemiJoinExists> m_semiJoin;
};

// A query-native ORDER BY item: a COLUMN (null = the row-identity / reference
// sort, i.e. the queryable's row-key column) + direction. m_path (non-empty) sorts on the LEAF of a
// reference dot-walk path (== m_col), joined like a dot-walk filter / projection.
struct ibQuerySortItem
{
	const ibBackendQueryColumn* m_col = nullptr;   // null = row-key (reference/PK) sort; with m_path = the path LEAF
	bool                        m_ascending = true;
	std::vector<const ibBackendQueryColumn*> m_path;     // reference dot-walk path (empty = plain column)

	// COMPUTED sort (ORDER BY <expression> — a CASE, arithmetic, or a bare value/&parameter): when set, the
	// provider lowers it via BuildColumnExpr and sorts on the resulting expression — m_col stays null, m_path
	// empty. Single-source DB reads only (the L4 lowering gates it), like the computed WHERE side; a computed
	// sort cannot be a keyset key, so a paged read cannot page by it (the text-query full read is the user).
	std::shared_ptr<struct ibQueryColumnExpr> m_expr;
};

// ==========================================================================
// ibQueryPredicate — an L3 WHERE predicate TREE (still L2-free: columns + values).
// The flat ibQueryCondition list AND-folds; this tree adds OR / NOT / IS NULL so L4 can
// express full boolean WHERE. The provider lowers it to the L2-1 IR (ibBinOp Or/And, ibNot,
// ibIsNull) — L3 stays L2-blind. IN expands to Or(Eq …) and BETWEEN to And(>=, <=) at the
// L4 lowering, so the tree needs no dedicated IN / BETWEEN node. (docs §23: door Where via L2-1.)
// ==========================================================================
enum class ibQueryPredicateKind { Leaf, And, Or, Not, IsNull };

struct ibQueryPredicate;
using ibQueryPredicatePtr = std::shared_ptr<ibQueryPredicate>;

struct ibQueryPredicate
{
	ibQueryPredicateKind m_kind = ibQueryPredicateKind::Leaf;

	ibQueryCondition            m_leaf;                 // Leaf — one col / op / value (Eq/Ne/ordered/LIKE)
	const ibBackendQueryColumn* m_col = nullptr;    // IsNull — the column (the path LEAF when m_path set)
	bool                        m_negated = false;      // IsNull — IS NOT NULL
	std::vector<const ibBackendQueryColumn*> m_path;    // IsNull — reference dot-walk path (empty = plain column)

	std::vector<ibQueryPredicatePtr> m_children;        // And / Or (N) · Not (1)

	static ibQueryPredicatePtr Leaf(const ibQueryCondition& cond) {
		auto p = std::make_shared<ibQueryPredicate>();
		p->m_kind = ibQueryPredicateKind::Leaf; p->m_leaf = cond; return p;
	}
	static ibQueryPredicatePtr Compose(ibQueryPredicateKind kind, ibQueryPredicatePtr a, ibQueryPredicatePtr b) {
		auto p = std::make_shared<ibQueryPredicate>();
		p->m_kind = kind; p->m_children = { a, b }; return p;
	}
	static ibQueryPredicatePtr Not(ibQueryPredicatePtr a) {
		auto p = std::make_shared<ibQueryPredicate>();
		p->m_kind = ibQueryPredicateKind::Not; p->m_children = { a }; return p;
	}
	static ibQueryPredicatePtr Null(const ibBackendQueryColumn* col, bool negated,
		const std::vector<const ibBackendQueryColumn*>& path = {}) {
		auto p = std::make_shared<ibQueryPredicate>();
		p->m_kind = ibQueryPredicateKind::IsNull; p->m_col = col; p->m_negated = negated;
		if (path.size() > 1) p->m_path = path;
		return p;
	}
};

// A SEMI-JOIN restriction — RLS `restrict s in Source join a in Inner on s.k <op> a.k`: the outer row
// passes IFF a permitting row EXISTS in the inner. Rendered as a CORRELATED EXISTS (a FILTER — once/zero
// per row — never a multiplying JOIN). The inner is a FULL query: its own WHERE / dot-walk / captured
// runtime Params ride along via m_where (lowered vs m_inner). The outer+inner key columns + op form the
// correlation. Held on the builder (parallel to the flat conditions) and appended to the WHERE by the
// provider, so it goes SERVER-SIDE in the one main statement. (docs/access-policy-rls.md — semi-join.)
struct ibSemiJoinExists
{
	const ibBackendQueryable*   m_inner    = nullptr;   // the permission source (a real register / table)
	ibQueryPredicatePtr         m_where;                // the inner's OWN conditions, lowered vs m_inner (null = none)
	const ibBackendQueryColumn* m_outerKey = nullptr;   // s.k — correlation column on the OUTER (main) source
	const ibBackendQueryColumn* m_innerKey = nullptr;   // a.k — correlation column on the INNER
	ibQueryFilterOp             m_op = ibQueryFilterOp::Equal;   // correlation comparison (Equal default; FilterOpToBinOp at render)
	bool                        m_negated  = false;      // NOT EXISTS — a deny-if-present rule (future)
};

// ==========================================================================
// ibQueryColumnExpr — an L3 COMPUTED-COLUMN expression TREE (L2-free: columns + values + ops). A plain
// projection is a single column; this lets a projection COMPUTE — arithmetic (a * b - c) and a searched
// CASE (CASE WHEN <predicate> THEN <expr> … ELSE <expr> END). The provider lowers it to the L2-1 IR
// (ibBinOp Add/Sub/Mul/Div/Mod, the L2-1 Case node) and projects it AS the output alias; the column-based
// door stays L2-blind. WHEN conditions reuse the ibQueryPredicate tree. (docs §23 — computed columns.)
// ==========================================================================
// PeriodTrunc — "the start of the <unit> containing m_lhs". Lowers to the L2-1 node of the same
// name, which the dialect's truncation map spells. It is here so a query can GROUP BY month
// without its author naming an engine — and so a totals REBUILD can group the movements by
// exactly the expression the maintenance trigger keys rows with. Two paths, one definition; a
// second notion of "start of the month" would silently split rows the trigger had merged.
enum class ibQueryColumnExprKind { Column, Const, Arith, Case, PeriodTrunc };
enum class ibQueryColumnArithOp { Add, Sub, Mul, Div, Mod };

struct ibQueryColumnExpr;
using ibQueryColumnExprPtr = std::shared_ptr<ibQueryColumnExpr>;

struct ibQueryColumnExpr
{
	ibQueryColumnExprKind       m_kind = ibQueryColumnExprKind::Column;

	const ibBackendQueryColumn* m_col = nullptr;          // Column — the source column (its FIRST sql field)
	ibValue                     m_const;                  // Const — a literal value

	ibQueryColumnArithOp        m_arith = ibQueryColumnArithOp::Add;   // Arith
	ibQueryColumnExprPtr        m_lhs, m_rhs;                          // Arith

	// Case — searched WHEN(predicate) -> THEN(expr) pairs in order, + optional ELSE.
	std::vector<std::pair<ibQueryPredicatePtr, ibQueryColumnExprPtr>> m_cases;
	ibQueryColumnExprPtr        m_else;

	// PeriodTrunc — the unit m_lhs is truncated to. Same principle as everything else here: the
	// expression names the CONCEPT, the dialect owns the spelling.
	ibTotalsPeriod              m_periodUnit = ibTotalsPeriod::Month;

	static ibQueryColumnExprPtr Col(const ibBackendQueryColumn* col) {
		auto e = std::make_shared<ibQueryColumnExpr>();
		e->m_kind = ibQueryColumnExprKind::Column; e->m_col = col; return e;
	}
	static ibQueryColumnExprPtr Const(const ibValue& v) {
		auto e = std::make_shared<ibQueryColumnExpr>();
		e->m_kind = ibQueryColumnExprKind::Const; e->m_const = v; return e;
	}
	static ibQueryColumnExprPtr Arith(ibQueryColumnArithOp op, ibQueryColumnExprPtr a, ibQueryColumnExprPtr b) {
		auto e = std::make_shared<ibQueryColumnExpr>();
		e->m_kind = ibQueryColumnExprKind::Arith; e->m_arith = op; e->m_lhs = a; e->m_rhs = b; return e;
	}
	static ibQueryColumnExprPtr PeriodTrunc(ibQueryColumnExprPtr expr, ibTotalsPeriod unit) {
		auto e = std::make_shared<ibQueryColumnExpr>();
		e->m_kind = ibQueryColumnExprKind::PeriodTrunc; e->m_lhs = expr; e->m_periodUnit = unit; return e;
	}
	static ibQueryColumnExprPtr Case(std::vector<std::pair<ibQueryPredicatePtr, ibQueryColumnExprPtr>> cases,
	                                 ibQueryColumnExprPtr otherwise) {
		auto e = std::make_shared<ibQueryColumnExpr>();
		e->m_kind = ibQueryColumnExprKind::Case; e->m_cases = std::move(cases); e->m_else = otherwise; return e;
	}
};

// One computed projection: an expression + its output alias (read back by GetColumn(alias)).
struct ibQueryColumnSelect
{
	ibQueryColumnExprPtr m_expr;
	wxString             m_alias;
};

// ==========================================================================

class BACKEND_API ibBackendQueryable
{
public:

	virtual ~ibBackendQueryable() = default;

	// --- the queryable VENDS its provider ('the table generates its engine') ----
	// L3 (the door) is metadata-BLIND: it pulls a provider from here and runs the
	// query through the ibBackendQueryProvider ABSTRACTION — never naming a concrete
	// provider, never touching L2-1. The provider holds ALL the L2-1 + metadata->physical
	// lowering. The DB families share one default (a stateless static DB provider);
	// computed queryables (slice / balance / turnover) OVERRIDE this to vend their own
	// (a static computed provider). The default impl lives in queryProvider.cpp where
	// the concrete provider is complete. (docs/query-language-arc.md §22.4)
	virtual ibBackendQueryProvider& GetProvider() const;

	// --- name resolution (COLUMNS — L3-clean) ---------------------------
	// The queryable resolves a reference to a COLUMN, never a metaobject attribute: the
	// attribute type belongs to ibBackendRowReader (the DB materialisation contract), not to
	// this L3 interface. The L4 text parser feeds a NAME; ResolveColumnByName yields the
	// source's own column object. Attribute-level resolution (the DB field machinery) is
	// reached, when needed, via AsRowReader(). (docs/query-language-arc.md §22.4b)

	// Does THIS source own the column? — column->leaf routing for multi-source composition
	// (which leaf a Where / join-key / output column belongs to). Default: the column
	// resolves to one of OUR columns (by name). (docs §22.1)
	virtual bool OwnsColumn(const ibBackendQueryColumn* col) const {
		// Identity by model-id, NOT merely by name: two joined sources can each expose an attribute with
		// the SAME name (e.g. "Reference"), and a name-only check would let BOTH claim the other's column —
		// the RAM stitch would then read a foreign field off this leaf's own SELECT * (fld<id>_TYPE not
		// found). The model-id (attribute metaID) is config-unique, so each column belongs to exactly one.
		if (col == nullptr) return false;
		const ibBackendQueryColumn* mine = ResolveColumnByName(col->GetName());
		return mine != nullptr && mine->GetColumnId() == col->GetColumnId();
	}

	// ALL columns this source exposes — for SELECT * (a nested subquery over `From(src)`
	// with no explicit Select list derives its columns from here). Default: none; a record
	// source returns its attributes, a temp source its temp columns. (docs §22 nested subquery)
	virtual std::vector<const ibBackendQueryColumn*> GetColumns() const { return {}; }

	// The source's UNIQUENESS-KEY COLUMNS — the ONE authority for the write UPSERT match AND the
	// dot-walk self-reference key. The source owns its key: a record (catalog / document) returns
	// its DATA-REFERENCE attribute (the row's own _RRRef reference — unique; the provider reads
	// its Reference field for the join and its fields for the match); a register returns recorder
	// + line number + period (recorder-based) or period + dimensions (information register); a
	// constant its single RECORD_KEY column. The uuid is NOT here — it stays the read keyset /
	// DELETE key (GetIdentitySort), a second link key until cleaned. Default: none. (docs §22.1)
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const { return {}; }

	// THIS source's own column with the given name — for UNION, where each branch supplies
	// the shared output columns BY NAME (catalog ∪ temp: each has its own "Code" column
	// object). A non-attribute source (temp / subquery) overrides with its name lookup; null
	// if absent. The DEFAULT (a record source) forwards to the row reader's attribute-by-name
	// (an attribute IS a column) — defined out-of-line (queryProvider.cpp) so this header
	// names no attribute / row-reader type.
	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const;

	// --- physical layout -------------------------------------------------
	// The real backing table for the main row scan.
	virtual wxString GetQueryTableName() const = 0;

	// What this source IS inside a FROM clause, when it is not a plain table scan. Returning a
	// relation makes the provider emit `FROM (<that>) AS alias` instead of `FROM <table> alias` —
	// so the source becomes a DERIVED TABLE and everything around it (JOIN, WHERE, ORDER BY,
	// paging, RLS) stays ordinary SQL handled by the engine.
	//
	// Null (the default) = an ordinary table, scanned by GetQueryTableName().
	//
	// This is what keeps a parameterised reading on the server. A register's balance "as of a
	// date" is an aggregate over the totals view — the date cannot live in a view, so it lives in
	// this subquery; without the hook the whole result would have to be materialised into RAM
	// before it could be joined to anything, turning the most latency-critical reading in the
	// system into a transfer. The pattern is not new here: the information register's slice
	// already joins a nested MAX(period) aggregate through ibSubquery — this only lets a queryable
	// declare the same thing for itself.
	virtual ibQueryRelPtr GetSourceRelation(const wxString& alias) const { return nullptr; }
	virtual ibGuid GetQueryTableGuid() const = 0;

	// The USER-facing name (as in the metadata tree, e.g. "Enumeration3") — for the restructure change
	// ledger, NOT for SQL. A metaobject-backed source returns its metaobject's name; the default is the
	// physical table name (a temp / computed source has no friendlier name).
	virtual wxString GetQueryName() const { return GetQueryTableName(); }

	// This queryable's metaID — the parent-reference blob (tree filter) needs it.
	virtual ibMetaID GetQueryTableId() const = 0;
	// The metadata context the DB provider needs to reconstruct a column's value WITHOUT the
	// attribute: a reference column rebuilds its ibValueReferenceDataObject from (clsid, blob) via
	// metaData->GetTypeCtor, an enum its variant via metaData->Create*. A metaobject-backed source
	// returns its own metadata; a temp / subquery / computed source has none (column-based reads
	// over them are raw / primitive, no reference reconstruction). (docs/query-language-arc.md §22.4b)
	virtual const ibMetaData* GetMetaData() const { return nullptr; }

	// The metaobject BEHIND this queryable (a catalog / document / register …) — a metadata-backed queryable holds
	// it; a temp / subquery / computed source has none. The dynamic list forwards its GetSourceMetaObject THROUGH
	// here so the front reads the source's icon / presentation off the metaobject (as every source object does).
	virtual const class ibValueMetaObjectGenericData* GetSourceMetaObject() const { return nullptr; }

	// --- row identity / keyset tail --------------------------------------
	// Identity columns that give a cursor a TOTAL order; L3 appends them after
	// the user sort. The LAST one is unique. ALL real columns now — no null sentinel:
	//   catalog/document : { uuid }                          -> the uuid column
	//   register         : recorder+line  OR  period?+dimensions  (real attrs)
	// The dot-walk self-reference key AND the UPSERT match both derive from
	// GetPrimaryKeyColumns (a record's data-reference / a register's composite) — so
	// there is no GetRowKeyColumn / IsReferenceAttribute / GetReferenceKeyColumn: one
	// key authority. The uuid stays a second link key (this identity tail) until cleaned.
	virtual std::vector<ibQuerySortItem> GetIdentitySort() const = 0;

	// Reference dot-walk target resolution moved to the PROVIDER (ibDbTableProvider — the one metadata
	// owner): callers use queryable->GetProvider().ResolveReferenceTarget(queryable, col). The queryable
	// names no metadata; it only vends GetMetaData(), which the provider reads. (docs §22 dot-walk)

	// The PARENT-reference column of a hierarchical record source (the parent attribute) — paired with
	// GetPrimaryKeyColumns().front() (the self-reference) it gives the source's own parent-ref hierarchy.
	// Null for a flat / non-record source. Used to unfold a TotalBy(refField, Hierarchy) dimension: the
	// target catalog's parent-map is read through ITS GetHierarchyColumn. (docs/query-language-arc.md §22.1b)
	virtual const ibBackendQueryColumn* GetHierarchyColumn() const { return nullptr; }
	// (GetFolderColumn REMOVED — folders are a folder-first SORT / IsFolder FILTER set at list creation, not a
	//  structural queryable column; the special engine mechanism kept is the HIERARCHY, not folders. — Max.)

	// Auto-join support (Join(b) without explicit columns) needs NO dedicated virtuals: the
	// composer derives a null-key join from the COLUMNS — a referencing column (one whose
	// ResolveReferenceTarget is the other source) on one side, matched to the other side's
	// SELF-REFERENCE column (the front of its GetPrimaryKeyColumns — its data-reference, own
	// _RRRef). A non-reference source exposes no key, so a Join over it needs explicit keys.
	// (docs/query-language-arc.md §22.1)

	// --- DB-row materialisation: NOT here ---------------------------------
	// Materialising a value from a physical DB row needs the metaobject attribute + the L1
	// result set — neither of which this L3 interface may name. The whole concern lives in
	// the PROVIDER (the L3<->L2-1 layer): it receives the COLUMN, static_casts it to the
	// metaobject attribute internally, and calls the existing GetValueAttribute /
	// SetValueAttribute. L3 just names columns; the provider knows how. (docs §22.4b)

	// --- computed (RAM) queryables ---------------------------------------
	// The main queryable (and the record families) is read by a physical DB scan.
	// A COMPUTED virtual table — a register slice / balance / turnover — is a
	// SELF-CONTAINED relation: its own filters (period, dimensions) are baked into
	// the instance's constructor, and it produces its rows in RAM. You construct
	// one, hand it to From(), and L3 reads it like any source — it checks
	// IsComputedInRam() and, when true, builds the selection from ComputeRows()
	// instead of the DB provider. So the same door reads physical and computed
	// sources alike, and the computed result is a RAM relation L3 can join. The
	// instance is call-scoped (it carries one query's filters). `extra` is any
	// further door conditions to compose on top (post-filter) — ADVISORY: an
	// implementation may push them into its own compute to build fewer rows, but
	// the caller (ibComputedProvider) applies them over the result regardless, so
	// ignoring them costs speed, never correctness. It is no longer "empty for
	// now": the semi-join key reduction pushes an `In` filter through it.
	// Default: physical. (docs/query-language-arc.md §22.4d, §22.6 — RAM-set.)
	virtual bool IsComputedInRam() const { return false; }
	
	// Produces the computed rows as L3's OWN table (ibQueryRamTable) — NOT a runtime
	// ibValueModelTable. The register's Compute* builds it directly; a runtime-sourced temp
	// table converts its model into one at this boundary. (docs/query-language-arc.md §22.6)
	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const { return ibQueryRamTable(); }
	// (The cell-UPSERT write path was removed: a RAM list now edits its LIVE storage rows directly — the node
	// IS the storage row — so there is no display-copy to write back through the queryable. See ibDataRamComposer.)
};

// ==========================================================================
// ibBackendQueryableHolder — the common interface for anything that VENDS an
// ibBackendQueryable. Catalogs, documents, charts, enumerations, registers,
// constants AND tabular sections all implement it (a metaobject HAS-A queryable;
// it no longer IS one). L4 and the door reach a metaobject's data uniformly through
// holder->GetQueryable(), without knowing the concrete metaclass — null when the
// metaobject has no queryable. (docs/query-language-arc.md §22.4e — decouple)
// ==========================================================================
class BACKEND_API ibBackendQueryableHolder
{
public:
	virtual ~ibBackendQueryableHolder() = default;
	// The MAIN TABLE — the source the dynamic list builds on (composer.FromSource,
	// columns / identity / parent all come from it; family-blind, register ≡ ref).
	virtual const ibBackendQueryable* GetQueryable() const = 0;

	// (`UseCustomQuery` / `GetQueryText` / `GetKeyFields` REMOVED 2026-08-07 — never overridden,
	// never called. They came from an earlier reading in which an arbitrary query REPLACED the main
	// table, so the holder had to be asked which of the two it was and, having no table, to be told
	// its key by hand. That reading is gone: the main table is ALWAYS there and the query lives over
	// it, so the key is the main table's PK by construction and there is no second case to ask about.
	// The arbitrary query itself lives on the LIST, as its own properties, which is where the thing
	// the user edits belongs. See docs/query-constructor.md §7c.)
};

// ==========================================================================
// ibSubqueryQueryable — a SYSTEM queryable (built-in, not metaobject-backed, like the temp
// table): a NESTED QUERY as a first-class source —
//     From( ibSubquery(inner), "sub" )  ->  SELECT * FROM (SELECT * FROM name) AS sub
// The inner query is RUN and its rows materialised into an ibQueryRamTable, so the outer
// query reads / filters / joins / unions / totals over it like any computed source. It is a
// pure L3 construct — it names no attribute, no L1 cursor (it is a computed/RAM source, so
// AsRowReader() stays null). Columns = the inner explicit Select(col, alias) list, or — for
// SELECT * — the inner PRIMARY source's full column set (GetColumns). The bodies that touch
// ibDataQueryBuilder live out-of-line (queryProvider.cpp), so this header forward-declares
// the inner door only. (docs/query-language-arc.md §22 nested subquery)
// ==========================================================================
class BACKEND_API ibSubqueryQueryable : public ibBackendQueryable
{
public:
	// Copies the inner query (owned). topCount > 0 limits the materialised rows (SELECT TOP n in the
	// branch / subquery text). An AGGREGATE inner query (GroupBy / aggregates) is detected from the
	// builder: the exposed columns become its GROUP BY keys + one owned SYNTHETIC numeric column per
	// aggregate alias, ComputeRows runs SelectAggregate, and the outer's pushed-down conditions apply
	// as a RAM post-filter (they reference POST-aggregation output — HAVING semantics).
	explicit ibSubqueryQueryable(const ibDataQueryBuilder& inner, long topCount = 0);
	~ibSubqueryQueryable() override;                                 // out-of-line — unique_ptr of an incomplete type

	const ibBackendQueryColumn* Column(const wxString& name) const {
		for (const ibBackendQueryColumn* c : m_columns)
			if (c != nullptr && c->GetName() == name) return c;
		return nullptr;
	}

	bool OwnsColumn(const ibBackendQueryColumn* col) const override {
		for (const ibBackendQueryColumn* c : m_columns)
			if (c == col) return true;
		return false;
	}
	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override { return Column(name); }
	std::vector<const ibBackendQueryColumn*> GetColumns() const override { return m_columns; }

	ibBackendQueryProvider& GetProvider() const override;            // out-of-line — vends the computed provider
	bool IsComputedInRam() const override { return true; }
	ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;   // out-of-line — runs the inner query

	// trivial L3 surface for a non-metaobject (derived) source — no metadata, so no
	// metadata guid (matches the RAM temp-table queryable).
	wxString GetQueryTableName() const override { return wxEmptyString; }
	ibGuid   GetQueryTableGuid() const override { return wxNullGuid; }
	ibMetaID GetQueryTableId()    const override { return 0; }
	std::vector<ibQuerySortItem> GetIdentitySort() const override { return {}; }

private:
	std::unique_ptr<ibDataQueryBuilder>      m_inner;     // the nested query (owned by value via the heap)
	std::vector<const ibBackendQueryColumn*> m_columns;   // exposed columns (select list / SELECT * / group keys + agg aliases)
	// AGGREGATE inner query support: owned synthetic columns for the aggregate aliases (raw numeric,
	// unique model ids), the mode flag, and the optional row limit (SELECT TOP n in the branch).
	std::vector<std::shared_ptr<ibBackendQueryColumn>> m_ownedColumns;
	bool m_aggregate = false;
	long m_top = 0;
};

// ibBackendQueryColumn — the column counterpart, lives in queryColumn.h (included
// above) so the fundamental attribute metaobject can derive from it lightly.

// ==========================================================================
// ibComputedRegisterQueryable<TReg> — the shared base for a register's call-scoped,
// RAM-computed virtual table (the information-register SLICE, the accumulation-register
// BALANCE / TURNOVER). All of them are the same shape: IsComputedInRam() is true and the
// navigation methods FORWARD to the register's OWN queryable (a computed table returns the
// register's columns), so a concrete subclass adds only its call-scoped filters (ctor) +
// ComputeRows (the register's compute). TReg = ibValueMetaObjectInformationRegister /
// ibValueMetaObjectAccumulationRegister — both vend GetQueryable(). It is a template over an
// unknown TReg, so it lives in this header alongside the interface; GetProvider() vends the
// shared computed provider through ibComputedProviderInstance() (so this header still names no
// concrete provider / L2-1 type). (docs/query-language-arc.md §22.4)
// ==========================================================================
template <typename TReg>
class ibComputedRegisterQueryable : public ibBackendQueryable
{
public:
	explicit ibComputedRegisterQueryable(const TReg* reg) : m_reg(reg) {}

	// Vends the shared stateless computed (RAM) provider; the door pulls it via GetProvider()
	// and runs the read through it — the rows come from ComputeRows(), no physical scan, no L2-1.
	virtual ibBackendQueryProvider& GetProvider() const override { return ibComputedProviderInstance(); }

	// Computed (RAM) virtual table — the door builds the selection from ComputeRows(), never a
	// physical scan (the concrete subclass overrides ComputeRows).
	virtual bool IsComputedInRam() const override { return true; }

	// L3 navigation forwards to ONE source, named here so a subclass can redirect the whole set at
	// once. By default that is the register's own queryable: a computed table carries the
	// register's columns, so it navigates exactly like the register.
	//
	// A subclass backed by a materialised VIEW overrides this single method and every forward below
	// follows — the columns it exposes then describe the VIEW (opening / receipt / turnover …),
	// which is what its rows actually contain. Without one point of substitution each forward would
	// have to be overridden separately, and the one missed would report the movement table's
	// columns for a totals row.
	virtual const ibBackendQueryable* NavigationSource() const { return m_reg->GetQueryable(); }

	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override { return NavigationSource()->ResolveColumnByName(name); }
	virtual std::vector<const ibBackendQueryColumn*> GetColumns() const override { return NavigationSource()->GetColumns(); }
	virtual wxString GetQueryTableName() const override { return NavigationSource()->GetQueryTableName(); }
	virtual ibGuid GetQueryTableGuid() const override { return NavigationSource()->GetQueryTableGuid(); }
	virtual wxString GetQueryName()       const override { return NavigationSource()->GetQueryName(); }
	virtual ibMetaID GetQueryTableId()    const override { return NavigationSource()->GetQueryTableId(); }
	virtual const ibMetaData* GetMetaData() const override { return m_reg->GetQueryable()->GetMetaData(); }
	virtual std::vector<ibQuerySortItem> GetIdentitySort() const override { return NavigationSource()->GetIdentitySort(); }
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override { return NavigationSource()->GetPrimaryKeyColumns(); }

protected:
	const TReg* m_reg;
};

#endif
