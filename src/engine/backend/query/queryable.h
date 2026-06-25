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

#include "backend/tableInfo.h"          // ibComparisonType, ibMetaID
#include "backend/compiler/value.h"     // ibValue
#include "queryColumn.h"                // ibBackendQueryColumn (the column counterpart)
#include "queryRamTable.h"              // ibQueryRamTable — ComputeRows produces the L3 table (no runtime type)

#include <memory>
#include <vector>

class ibBackendQueryProvider;   // the engine the queryable vends — defined in queryProvider.h; it IS the whole L3<->L2 layer
class ibDataQueryBuilder;       // the inner query a system subquery-queryable wraps (defined below) — full type only in its .cpp
class ibMetaData;               // the metadata context — the provider needs it to reconstruct reference / enum values column-based

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
// list layer's Equal / NotEqual. Kept here (not L2's ibQueryBinOp) so the metadata
// side carries no L2 dependency — the query builder translates these to physical IR
// operators. (L3 doesn't pull L2 includes; see docs/query-language-arc.md §20, §22.4b.)
enum class ibQueryFilterOp { Like, Less, LessEqual, Greater, GreaterEqual };

struct ibQueryCondition
{
	const ibBackendQueryColumn* m_col = nullptr;   // null = the row-key column; with m_path = the path LEAF
	ibComparisonType            m_comparison = ibComparisonType::ibComparisonType_Equal;
	ibValue                     m_value;

	// When m_explicitOp is set, m_op is the operator (LIKE / <= / < / > / >=) and
	// m_comparison is ignored. Filled by WhereLike / WhereCompare; the plain
	// Where(col, comparison, value) path leaves it false (Eq / Ne).
	bool            m_explicitOp = false;
	ibQueryFilterOp m_op = ibQueryFilterOp::Like;

	// Reference DOT-WALK: when non-empty, this condition filters the LEAF attribute of a reference
	// path (Producer.Region -> {Producer, Region}). Every non-leaf segment is a single-target
	// reference column the provider auto-joins (the SAME join SelectPath builds, deduped by prefix);
	// the leaf (== m_col) is compared on the joined target. Empty = a plain column on the main source.
	std::vector<const ibBackendQueryColumn*> m_path;

	// COMPUTED left-hand side (WHERE Qty * Price > value, a CASE …): when set, the provider lowers
	// the expression via BuildColumnExpr and compares it to m_value — m_col stays null, m_path empty.
	// Single-source DB reads / aggregates only (the lowering gates it); the RAM stitch and computed
	// sources do not evaluate it. Declared after m_path so the flat struct stays POD-ordered.
	std::shared_ptr<struct ibQueryColumnExpr> m_expr;
};

// A query-native ORDER BY item: a COLUMN (null = the row-identity / reference
// sort, i.e. the queryable's row-key column) + direction. m_path (non-empty) sorts on the LEAF of a
// reference dot-walk path (== m_col), joined like a dot-walk filter / projection.
struct ibQuerySortItem
{
	const ibBackendQueryColumn* m_col = nullptr;   // null = row-key (reference/PK) sort; with m_path = the path LEAF
	bool                        m_ascending = true;
	std::vector<const ibBackendQueryColumn*> m_path;     // reference dot-walk path (empty = plain column)
};

// ==========================================================================
// ibQueryPredicate — an L3 WHERE predicate TREE (still L2-free: columns + values).
// The flat ibQueryCondition list AND-folds; this tree adds OR / NOT / IS NULL so L4 can
// express full boolean WHERE. The provider lowers it to the L2 IR (ibBinOp Or/And, ibNot,
// ibIsNull) — L3 stays L2-blind. IN expands to Or(Eq …) and BETWEEN to And(>=, <=) at the
// L4 lowering, so the tree needs no dedicated IN / BETWEEN node. (docs §23: door Where via L2.)
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

// ==========================================================================
// ibQueryColumnExpr — an L3 COMPUTED-COLUMN expression TREE (L2-free: columns + values + ops). A plain
// projection is a single column; this lets a projection COMPUTE — arithmetic (a * b - c) and a searched
// CASE (CASE WHEN <predicate> THEN <expr> … ELSE <expr> END). The provider lowers it to the L2 IR
// (ibBinOp Add/Sub/Mul/Div/Mod, the L2 Case node) and projects it AS the output alias; the column-based
// door stays L2-blind. WHEN conditions reuse the ibQueryPredicate tree. (docs §23 — computed columns.)
// ==========================================================================
enum class ibQueryColumnExprKind { Column, Const, Arith, Case };
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
	// provider, never touching L2. The provider holds ALL the L2 + metadata->physical
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

	// Resolve a single-target reference COLUMN of this queryable to the queryable of
	// the object it points at — the dot-walk join target. Works off the column's type
	// (ibBackendQueryColumn::GetTypeDesc) + this queryable's metadata. Null if the
	// column is not a reference, is polymorphic (more than one target type), or the
	// target vends no queryable. Navigation lives on the queryable (it owns the
	// metadata context); the door just chains the result. (docs §22 dot-walk)
	virtual const ibBackendQueryable* ResolveReferenceTarget(const ibBackendQueryColumn* refColumn) const { return nullptr; }

	// ALL reference targets of a column — N for a COMPOSITE (multi-type) reference, 1 for a single
	// reference, empty for a non-reference. The composite dot-walk joins one table per target and
	// COALESCEs the leaf across them (the _RRRef[metaID] tag matches at most one). Default: wrap the
	// single-target resolver, so non-record sources need not override. (docs §22 dot-walk)
	virtual std::vector<const ibBackendQueryable*> ResolveReferenceTargets(const ibBackendQueryColumn* refColumn) const {
		const ibBackendQueryable* one = ResolveReferenceTarget(refColumn);
		return one != nullptr ? std::vector<const ibBackendQueryable*>{ one }
		: std::vector<const ibBackendQueryable*>{};
	}

	// The PARENT-reference column of a hierarchical record source (the parent attribute) — paired with
	// GetPrimaryKeyColumns().front() (the self-reference) it gives the source's own parent-ref hierarchy.
	// Null for a flat / non-record source. Used to unfold a TotalBy(refField, Hierarchy) dimension: the
	// target catalog's parent-map is read through ITS GetParentColumn. (docs/query-language-arc.md §22.1b)
	virtual const ibBackendQueryColumn* GetParentColumn() const { return nullptr; }

	// Auto-join support (Join(b) without explicit columns) needs NO dedicated virtuals: the
	// composer derives a null-key join from the COLUMNS — a referencing column (one whose
	// ResolveReferenceTarget is the other source) on one side, matched to the other side's
	// SELF-REFERENCE column (the front of its GetPrimaryKeyColumns — its data-reference, own
	// _RRRef). A non-reference source exposes no key, so a Join over it needs explicit keys.
	// (docs/query-language-arc.md §22.1)

	// --- DB-row materialisation: NOT here ---------------------------------
	// Materialising a value from a physical DB row needs the metaobject attribute + the L1
	// result set — neither of which this L3 interface may name. The whole concern lives in
	// the PROVIDER (the L3<->L2 layer): it receives the COLUMN, static_casts it to the
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
	// further door conditions to compose on top (post-filter); empty for now.
	// Default: physical. (docs/query-language-arc.md §22.4d, §22.6 — RAM-set.)
	virtual bool IsComputedInRam() const { return false; }
	
	// Produces the computed rows as L3's OWN table (ibQueryRamTable) — NOT a runtime
	// ibValueModelTable. The register's Compute* builds it directly; a runtime-sourced temp
	// table converts its model into one at this boundary. (docs/query-language-arc.md §22.6)
	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const { return ibQueryRamTable(); }
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

	// --- dynamic-list source configuration (the "Запрос" surface). A plain holder
	// is a fixed main-table source; a custom-query holder overrides these.
	//   UseCustomQuery → read from QueryText instead of the main table directly.
	//   KeyFields      → the keyset columns when the query has no natural PK
	//                    (empty = Auto: derive from the main table's GetPrimaryKeyColumns).
	virtual bool UseCustomQuery() const { return false; }
	virtual wxString GetQueryText() const { return wxEmptyString; }
	virtual std::vector<wxString> GetKeyFields() const { return {}; }
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
// concrete provider / L2 type). (docs/query-language-arc.md §22.4)
// ==========================================================================
template <typename TReg>
class ibComputedRegisterQueryable : public ibBackendQueryable
{
public:
	explicit ibComputedRegisterQueryable(const TReg* reg) : m_reg(reg) {}

	// Vends the shared stateless computed (RAM) provider; the door pulls it via GetProvider()
	// and runs the read through it — the rows come from ComputeRows(), no physical scan, no L2.
	virtual ibBackendQueryProvider& GetProvider() const override { return ibComputedProviderInstance(); }

	// Computed (RAM) virtual table — the door builds the selection from ComputeRows(), never a
	// physical scan (the concrete subclass overrides ComputeRows).
	virtual bool IsComputedInRam() const override { return true; }

	// L3 navigation forwards to the register's own queryable — a computed table carries the
	// register's columns, so it navigates exactly like the register. It is a COMPUTED (RAM)
	// source: read through ComputeRows, never a DB cursor, so no attribute / L1 materialisation.
	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override { return m_reg->GetQueryable()->ResolveColumnByName(name); }
	virtual std::vector<const ibBackendQueryColumn*> GetColumns() const override { return m_reg->GetQueryable()->GetColumns(); }
	virtual wxString GetQueryTableName() const override { return m_reg->GetQueryable()->GetQueryTableName(); }
	virtual ibGuid GetQueryTableGuid() const override { return m_reg->GetQueryable()->GetQueryTableGuid(); }
	virtual wxString GetQueryName()       const override { return m_reg->GetQueryable()->GetQueryName(); }
	virtual ibMetaID GetQueryTableId()    const override { return m_reg->GetQueryable()->GetQueryTableId(); }
	virtual const ibMetaData* GetMetaData() const override { return m_reg->GetQueryable()->GetMetaData(); }
	virtual std::vector<ibQuerySortItem> GetIdentitySort() const override { return m_reg->GetQueryable()->GetIdentitySort(); }
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override { return m_reg->GetQueryable()->GetPrimaryKeyColumns(); }

protected:
	const TReg* m_reg;
};

#endif
