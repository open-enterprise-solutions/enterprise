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

#include "backend/tabularModelView.h"   // ibMetaID (via backend.h) — NOT tabularModel.h, so that one can hold
                                        // ibDataDBComposer BY VALUE (breaks the dataComposer→queryLowering→queryable→
                                        // model include cycle). (ibComparisonType is no longer used here — the
                                        // L3 condition op is ibQueryFilterOp now, defined below.)
#include "backend/compiler/value.h"     // ibValue
#include "queryColumn.h"                // ibBackendQueryColumn (the column counterpart)
#include "queryUnfold.h"                // ibQueryDimUnfold — a condition may carry the word, so a source can FOLD by it
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

struct ibQuerySelect;   // an AST select — a named result IS one, kept by pointer only

// ==========================================================================
// ibNamedResultScope — THE RESULTS THIS PACKAGE HAS NAMED, for the statements that read them.
//
// `SELECT … ONTO Sales` names a result; a later statement reads it by writing `FROM Sales`. That
// name resolves to no metaobject and no temp table — it is a QUERY, and the reader may either take
// its rows (the old road: substitute it as a nested source, which materialises in RAM) or declare
// it to the DBMS (`WITH Sales AS (…)`) and let the server do the join.
//
// So the lowering has to know which bare names are named results, exactly as ibTempSourceScope tells
// it which are transient sources. Same shape, same lifetime, same reason: thread-local, RAII-scoped
// to ONE package execution, nothing left behind.
//
// It carries the AST rather than a queryable BECAUSE the choice is not made here: the reader's own
// lowering decides between the two roads (see BuildSourceTree — the engine must have WITH, and the
// named query must be one the server can read).
class BACKEND_API ibNamedResultScope
{
public:
	explicit ibNamedResultScope(const std::map<wxString, const ibQuerySelect*>& results);
	~ibNamedResultScope();

	// The named result for `name` (matched without regard to case), or null.
	static const ibQuerySelect* Find(const wxString& name);

	ibNamedResultScope(const ibNamedResultScope&) = delete;
	ibNamedResultScope& operator=(const ibNamedResultScope&) = delete;

private:
	const std::map<wxString, const ibQuerySelect*>* m_prev;
};

class ibMetaData;   // the config a query runs on behalf of — its factory resolves by-name metaobject sources
class ibQueryableFactory;

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

	// THE factory a by-name source resolves through: this scope, else the active configuration, else the global
	// base one. Everything that resolves a source asks HERE — a hand-written query in a module sets no scope and
	// still means the config it lives in.
	static ibQueryableFactory* GetFactory();

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

	// ⭐⭐ HOW FAR DOWN THE VALUES REACH — and, when it is not `Elements`, m_values holds the values AS
	// NAMED rather than the subtree they stand for.
	//
	// A condition that only SELECTS rows never needs this: the lowering resolves the subtree into
	// values and hands over an ordinary `In`, which every provider renders. A condition handed to a
	// SOURCE is the other case — an accounting register asked for accounts «in hierarchy» reports the
	// subordinates UNDER the account that was named, and to fold like that it has to know which
	// account was named and how far down was asked. Expanded first, that question is unanswerable:
	// twenty accounts arrive and nothing says which one they roll into.
	//
	// ⚠ SO A PROVIDER MUST NEVER SEE A LEAF WITH THIS SET. It is filled only for a condition the
	// source consumes itself (ibQuerySourceParameter::m_consumedBySource); everything on the ordinary
	// road is expanded at the lowering and arrives here as `Elements`, which is what the default says.
	ibQueryDimUnfold            m_unfold = ibQueryDimUnfold::Elements;

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

	// ⭐⭐ SORTING BY A FOLD — `ORDER BY SUM(Qty) DESC`, i.e. "the biggest first", which is what a
	// TOP-N report is made of. It cannot be any of the three above: an aggregate is not a column of
	// the source (nothing to point at), and it is not an expression over one either — it exists only
	// AFTER the fold, under the name the projection gave it. So the sort names that OUTPUT.
	//
	// 🛑 Without it neither spelling worked: `ORDER BY SUM(M.Quantity)` answered "expected a column",
	// and `ORDER BY Qty` — the alias the query itself had just written — answered "unknown attribute
	// 'Qty' on source 'M'", because the resolver looks for attributes and an alias is not one
	// (measured 2026-09-04). A person asking for the top three items by turnover had no way to say it.
	//
	// Empty = an ordinary sort; the fields above say which of the three it is.
	wxString                    m_outputAlias;
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
// ⭐⭐ `WindowAgg` — AN AGGREGATE OVER AN AREA OF ITS OWN, computed by the SERVER.
//
// This is the one figure of a report the DBMS can compute even where the FOLD cannot be pushed down:
// `SUM(x) OVER (PARTITION BY <the levels above it>)` returns a value per ROW, so it needs neither
// ROLLUP nor GROUPING SETS — and windows are on in every engine we speak to (Firebird FB3+,
// PostgreSQL, SQLite 3.25+), while ROLLUP is PostgreSQL alone.
//
// Inside its area the value is CONSTANT, which is what makes it usable on a heading: the node folds
// it with MIN and gets the value itself back. (docs/query-language-arc.md §27)
enum class ibQueryColumnExprKind { Column, Const, Arith, Case, PeriodTrunc, WindowAgg };

// ⭐ THE CALL A WINDOW MAKES — named as a CONCEPT here, spelled by the provider, exactly as
// ibTotalsPeriod is. The five folds and the three ranking calls sit in one enum because they differ
// only in whether they take an input, which the expression already says by having one or not.
enum class ibQueryWindowFn { Sum, Count, Min, Max, Avg, RowNumber, Rank, DenseRank };

// …AND WHICH ROWS OF THE PARTITION IT FOLDS. Three answers and no more — the grammar offers exactly
// these, and inventing a fourth here would promise what the engines are not asked for.
//
//   Whole   — the partition entire: the denominator of a share.
//   Rows    — from its start through THIS row, counted one by one: a running total.
//   Range   — the same, but every row sharing this one's sort key contributes, so three movements
//             stamped with one period are one period's worth of stock, in any order.
enum class ibQueryWindowFrame { Whole, Rows, Range };
enum class ibQueryColumnArithOp { Add, Sub, Mul, Div, Mod };

// ⭐⭐ THE PERIOD UNITS, AS WORDS — one table, beside the expression that truncates by them.
//
// Two places in the language say a periodicity: a register's `Turnovers(&From, &To, Month)` and a
// totals level's `BY Period PERIODS(Month, …)`. They are the same question, so they read the same
// vocabulary; a second copy is how two parts of a program come to disagree about what "Quarter"
// means. (It lived under the registers while they were the only ones asking. They are not.)
//
// ⚠ The ORDER matters and is not alphabetical: it is coarseness, ascending. A totals schema offers
// only the projections COARSER than what it stores (an hour cannot be recovered from a day already
// summed), and that comparison is on the enum — so the enum's order IS the meaning.
inline const std::vector<std::pair<ibTotalsPeriod, wxString>>& ibPeriodUnits()
{
	static const std::vector<std::pair<ibTotalsPeriod, wxString>> s_units = {
		{ ibTotalsPeriod::Second,   wxT("Second")   }, { ibTotalsPeriod::Minute,   wxT("Minute")   },
		{ ibTotalsPeriod::Hour,     wxT("Hour")     }, { ibTotalsPeriod::Day,      wxT("Day")      },
		{ ibTotalsPeriod::Week,     wxT("Week")     }, { ibTotalsPeriod::TenDays,  wxT("TenDays")  },
		{ ibTotalsPeriod::Month,    wxT("Month")    }, { ibTotalsPeriod::Quarter,  wxT("Quarter")  },
		{ ibTotalsPeriod::HalfYear, wxT("HalfYear") }, { ibTotalsPeriod::Year,     wxT("Year")     },
	};
	return s_units;
}

// The word a unit is written as — a lookup in that one table, never a second switch over it.
inline wxString ibPeriodUnitWord(ibTotalsPeriod unit)
{
	for (const std::pair<ibTotalsPeriod, wxString>& u : ibPeriodUnits())
		if (u.first == unit)
			return u.second;
	return wxString();
}

// …and back: the word -> the unit. False = not one of them, and the CALLER says so in its own words
// (a query names the level, a register names the argument) rather than this deciding how to complain.
inline bool ibReadPeriodUnit(const wxString& word, ibTotalsPeriod& unit)
{
	for (const std::pair<ibTotalsPeriod, wxString>& u : ibPeriodUnits())
		if (word.IsSameAs(u.second, /*caseSensitive*/ false)) { unit = u.first; return true; }
	return false;
}

struct ibQueryColumnExpr;
using ibQueryColumnExprPtr = std::shared_ptr<ibQueryColumnExpr>;

struct ibQueryColumnExpr
{
	ibQueryColumnExprKind       m_kind = ibQueryColumnExprKind::Column;

	const ibBackendQueryColumn* m_col = nullptr;          // Column — the source column (its FIRST sql field)
	wxString                    m_field;                  // Column — ONE named physical field of it (empty = the first)
	ibValue                     m_const;                  // Const — a literal value

	ibQueryColumnArithOp        m_arith = ibQueryColumnArithOp::Add;   // Arith
	ibQueryColumnExprPtr        m_lhs, m_rhs;                          // Arith

	// Case — searched WHEN(predicate) -> THEN(expr) pairs in order, + optional ELSE.
	std::vector<std::pair<ibQueryPredicatePtr, ibQueryColumnExprPtr>> m_cases;
	ibQueryColumnExprPtr        m_else;

	// PeriodTrunc — the unit m_lhs is truncated to. Same principle as everything else here: the
	// expression names the CONCEPT, the dialect owns the spelling.
	ibTotalsPeriod              m_periodUnit = ibTotalsPeriod::Month;

	// WindowAgg — the call applied to m_lhs, partitioned by m_partition, ordered by m_windowOrder.
	//
	// ⭐ THE CALL, AS A CONCEPT — the provider spells it, exactly as it spells a period truncation.
	// A ranking call takes no input, and that is said by m_lhs being null rather than by a flag.
	//
	// 🛑 It was a wxString holding "SUM" / "ROW_NUMBER" for an hour, and that was a leak upward: the
	// word happened to be the same in the language and in SQL, which is a coincidence and not a
	// licence for this tier to carry the DBMS's spelling (Max, 2026-08-27: "no leaks between the
	// storeys").
	ibQueryWindowFn                       m_windowFn = ibQueryWindowFn::Sum;
	// An EMPTY partition is legitimate and means the whole result — the figure over everything, which
	// is what "the share of the report" is measured against.
	std::vector<ibQueryColumnExprPtr>     m_partition;
	// …and the ORDER within the partition, which is what makes "up to this row" mean anything. Empty
	// = the fold takes the partition whole, which is what a plain total over an area is.
	std::vector<std::pair<ibQueryColumnExprPtr, bool>> m_windowOrder;   // expression + ascending
	// The frame — this tier's own enum, mapped to the engine's by the provider (same leak, same fix).
	ibQueryWindowFrame                    m_windowFrame = ibQueryWindowFrame::Whole;

	// ⭐⭐ ONE FIELD OF A COMPOSITE COLUMN, NAMED. A plain Col() reduces to the column's FIRST value
	// field, which is the right answer for everything that has one value — a number, a date, a
	// reference read as a whole. It is the wrong answer for a column whose value is spread across
	// several fields and has to be REBUILT on the other side: a CASE over an accounting register's
	// dimension slot must be written once PER FIELD (the type tag, each admissible type's field,
	// a reference's pair), all projected under one prefix, so the reader can reassemble the value
	// through GetColumnObject(prefix, col). Reduced to the first field it would carry the type tag
	// and nothing else — a column that plainly reports a reference and returns a number.
	//
	// Empty name = the first value field, i.e. exactly what Col() has always meant.
	static ibQueryColumnExprPtr ColField(const ibBackendQueryColumn* col, const wxString& field) {
		auto e = std::make_shared<ibQueryColumnExpr>();
		e->m_kind = ibQueryColumnExprKind::Column; e->m_col = col; e->m_field = field; return e;
	}
	static ibQueryColumnExprPtr Col(const ibBackendQueryColumn* col) {
		auto e = std::make_shared<ibQueryColumnExpr>();
		e->m_kind = ibQueryColumnExprKind::Column; e->m_col = col; return e;
	}
	// ⭐ `SUM(<arg>) OVER (PARTITION BY <partition>)` — see the note on the kind. The function is the
	// door's own enum, so nothing here re-spells what an aggregate is called; the partition is the
	// PREFIX of levels the figure belongs to, which the lowering derives from the area's ADDRESS.
	static ibQueryColumnExprPtr WindowAgg(ibQueryWindowFn fn, ibQueryColumnExprPtr arg,
	                                      std::vector<ibQueryColumnExprPtr> partition,
	                                      std::vector<std::pair<ibQueryColumnExprPtr, bool>> order = {},
	                                      ibQueryWindowFrame frame = ibQueryWindowFrame::Whole) {
		auto e = std::make_shared<ibQueryColumnExpr>();
		e->m_kind = ibQueryColumnExprKind::WindowAgg;
		e->m_windowFn = fn;
		e->m_lhs = std::move(arg);
		e->m_partition = std::move(partition);
		e->m_windowOrder = std::move(order);
		e->m_windowFrame = frame;
		return e;
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

// ⭐⭐ ONE PUBLISHED COLUMN OF A NESTED QUERY — the name the outer world uses, and how to read it.
//
// A nested table's columns ARE its output, and the lowering already computes that output exactly
// (the schema it hands back from every select). Deriving them a second time from the door's internals
// is what went wrong three times over: a plain column arrives under its own name, a DOT-WALK under an
// alias with no column of its own, a COMPUTED expression the same, and a GROUP BY key without ever
// entering the select list at all. Four shapes, four chances to miss one — and each miss looked like
// "unknown attribute" about a name the inner query plainly declares.
//
// So the wrapper is TOLD its output instead of guessing it.
struct ibSubqueryOutput
{
	wxString                    m_name;            // what the outer query writes
	const ibBackendQueryColumn* m_col = nullptr;   // read through this column…
	wxString                    m_alias;           // …or by this name, when the door has no column for it
	// …or, for a reference / enum / composite leaf, reassembled from the field spread projected under
	// this prefix. THE SAME THREE-WAY RULE the selection reader uses (queryLowering, MaterialiseInto):
	// prefix first, then alias, then the column. A nested table reads its rows the one way its own
	// schema is read, or the value arrives as one field of an object instead of the object.
	wxString                    m_objectPrefix;
	ibTypeDescription           m_type;            // empty = unknown (a computed expression)
	// ⭐ AND WHO KEEPS `m_col` ALIVE, when it is not the metadata. The inner SELECT's schema owns the
	// columns IT minted (a dot-walk leaf, a synthetic measure) through OutputColumn::m_ownedCol, and
	// that schema is a LOCAL of the function that builds this list — so a wrapper holding the bare
	// pointer outlived the storage and read through freed memory (2026-08-19: the row values came
	// back EMPTY, which folded 56 rows into one group and printed a blank report).
	//
	// Carried as a share: empty for a metadata column (it outlives everything), set for a minted one.
	std::shared_ptr<ibBackendQueryColumn> m_owned;
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

	// ⭐ DID THIS SOURCE MINT THAT COLUMN — and if so, here is its STORAGE, so whoever still needs
	// the column past the run keeps it by raising a refcount instead of copying it (a copy answers
	// only the questions it copied; identity and everything the real column knows stop matching).
	//
	// Default EMPTY, which is the honest answer for a metadata-backed source: its columns are the
	// configuration's and outlive every query, so there is nothing to keep. Only the sources built
	// FOR one query — a nested subquery, a named query (`WITH`) — mint columns of their own and
	// answer here. (docs/query-language-arc.md §22 / §24.4)
	virtual std::shared_ptr<ibBackendQueryColumn> ShareColumn(const ibBackendQueryColumn* /*col*/) const {
		return nullptr;
	}

	// The source's UNIQUENESS-KEY COLUMNS — the ONE authority for the write UPSERT match AND the
	// dot-walk self-reference key. The source owns its key: a record (catalog / document) returns
	// its DATA-REFERENCE attribute (the row's own _RRRef reference — unique; the provider reads
	// its Reference field for the join and its fields for the match); a register returns recorder
	// + line number + period (recorder-based) or period + dimensions (information register); a
	// constant its single RECORD_KEY column. It is ALSO the read keyset and the DELETE key: those
	// used to ask a second question (GetIdentitySort) that answered with a sort, and a sort stops
	// being a key the moment something else sorts first. Default: none. (docs §22.1)
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

	// This source's guid — DERIVED from GetSourceMetaObject by default, so a metaobject-backed source
	// says nothing: it already said which metaobject it is. Still virtual, because a source can own an
	// identity WITHOUT a metaobject — a temp table has a guid of its own and nothing to read it off.
	// (Body in queryProvider.cpp, where the metaobject type is complete.)
	virtual ibGuid GetQueryTableGuid() const;

	// The USER-facing name (as in the metadata tree, e.g. "Enumeration3") — for the restructure change
	// ledger, NOT for SQL. A metaobject-backed source returns its metaobject's name; the default is the
	// physical table name (a temp / computed source has no friendlier name).
	virtual wxString GetQueryName() const { return GetQueryTableName(); }

	// This queryable's metaID (the parent-reference blob / tree filter needs it) — the same projection
	// of the same metaobject, with the same escape for a source that has none.
	virtual ibMetaID GetQueryTableId() const;
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

	// --- row identity ------------------------------------------------------
	// ⭐⭐ ONE KEY AUTHORITY — GetPrimaryKeyColumns below, and nothing beside it.
	//
	// There used to be a second answer here (GetIdentitySort: the columns that give a cursor a total
	// order, "the last one is unique"). Both described the same fact — what identifies a row — and the
	// duplicate did the damage a duplicate does: consumers wanting a KEY reached for the tail of a
	// SORT, which is the same thing only while nothing else sorts first. An enumeration ordering itself
	// by Order put a number there, and every reader of that tail read a number as identity.
	//
	// The keyset tail is now taken from the primary key directly (ibDataQueryBuilder::EffectiveSort),
	// which is where it always belonged: the key IS the tiebreaker, so there was never a second
	// question to ask. Likewise no GetRowKeyColumn / IsReferenceAttribute / GetReferenceKeyColumn.


	// Reference dot-walk target resolution moved to the PROVIDER (ibDbTableProvider — the one metadata
	// owner): callers use queryable->GetProvider().ResolveReferenceTarget(queryable, col). The queryable
	// names no metadata; it only vends GetMetaData(), which the provider reads. (docs §22 dot-walk)

	// The PARENT-reference column of a hierarchical record source (the parent attribute) — paired with
	// GetPrimaryKeyColumns().front() (the self-reference) it gives the source's own parent-ref hierarchy.
	// Null for a flat / non-record source. Used to unfold a TotalBy(refField, Hierarchy) dimension: the
	// target catalog's parent-map is read through ITS GetHierarchyColumn. (docs/query-language-arc.md §22.1b)
	virtual const ibBackendQueryColumn* GetHierarchyColumn() const { return nullptr; }

	// ⭐⭐ THE ARRANGEMENT ITSELF — one value, four states (`ibHierarchyType`, backend_core.h), and
	// every caller reads off it the distinction it actually needs:
	//
	//   is there a parent to walk        — anything but `eNone`      (grouping, IN HIERARCHY)
	//   does a LIST walk it as a tree    — `eItems` / `eFoldersAndItems`
	//   may an ITEM hold items           — `eItems`
	//
	// It used to be handed out as booleans, one per question, and a boolean is a PROJECTION: it
	// answers what its author needed and silently answers something else for the next caller. Both
	// mistakes that came of it were the same mistake — a chart of accounts declares `eSubordination`
	// (a recorded parent, a flat list), and asked through `GetHierarchyColumn() != nullptr` it first
	// said "no parent" to the grouping and later "yes, drill" to the list. Nothing to decide here:
	// the source states its arrangement and the caller names the state it cares about.
	// (Asked once per fetch, not per row.)
	virtual ibHierarchyType GetHierarchyType() const { return ibHierarchyType::eNone; }
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
	// …AND THE HONEST ONE: built from the inner select's OUTPUT SCHEMA, which is what this table
	// publishes. The derivation above stays for callers that have no schema at hand.
	ibSubqueryQueryable(const ibDataQueryBuilder& inner, long topCount,
	                    const std::vector<ibSubqueryOutput>& outputs);
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
	// …and the narrower question: did this wrapper ALLOCATE the column, so that it DIES with the
	// wrapper? A published column may equally be the inner source's own — owned by the metadata and
	// outliving everything. Anything that keeps a column pointer past the run must tell the two apart.
	bool OwnsColumnStorage(const ibBackendQueryColumn* col) const {
		for (const std::shared_ptr<ibBackendQueryColumn>& c : m_ownedColumns)
			if (c.get() == col) return true;
		return false;
	}
	// …and the answer that lets a consumer KEEP it: the storage itself, so holding on to a published
	// column is a matter of raising its refcount rather than copying it. A wrapper dies at the end of
	// the run; whoever still needs the column simply shares its ownership and the column lives on —
	// the SAME column, same id, same type. Empty when this wrapper did not allocate it (a metadata
	// column outlives everything and needs no keeping).
	std::shared_ptr<ibBackendQueryColumn> ShareColumn(const ibBackendQueryColumn* col) const override {
		for (const std::shared_ptr<ibBackendQueryColumn>& c : m_ownedColumns)
			if (c.get() == col) return c;
		return nullptr;
	}
	// Everything this wrapper allocated, for a consumer that keeps the WHOLE run alive rather than
	// hand-picking (the query result: it holds columns in four separate lists, and a list added later
	// would silently be the one nobody remembered to keep).
	const std::vector<std::shared_ptr<ibBackendQueryColumn>>& SharedColumns() const { return m_ownedColumns; }
	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override { return Column(name); }
	std::vector<const ibBackendQueryColumn*> GetColumns() const override { return m_columns; }

	ibBackendQueryProvider& GetProvider() const override;            // out-of-line — vends the computed provider
	bool IsComputedInRam() const override { return true; }
	ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;   // out-of-line — runs the inner query

	// trivial L3 surface for a non-metaobject (derived) source — no metadata guid, no table name
	// (matches the RAM temp-table queryable).
	wxString GetQueryTableName() const override { return wxEmptyString; }
	ibGuid   GetQueryTableGuid() const override { return wxNullGuid; }
	ibMetaID GetQueryTableId()    const override { return 0; }

	// ⭐ BUT IT DOES KNOW WHICH CONFIGURATION IT READS — the inner query's own. Answering "no
	// metadata" made every reference published by a subquery UNWALKABLE: resolving a reference to its
	// target needs the configuration the target lives in, so `Ref.Attribute1` over
	// `SELECT * FROM (SELECT Document1.Ref …)` came back as "'Ref' is not a single-target reference
	// (cannot walk)" — about a column whose type says exactly which document it points at
	// (2026-08-20). A wrapper is not a metadata-free source: it is a query over one.
	const ibMetaData* GetMetaData() const override;

private:
	std::unique_ptr<ibDataQueryBuilder>      m_inner;     // the nested query (owned by value via the heap)
	std::vector<const ibBackendQueryColumn*> m_columns;   // exposed columns (select list / SELECT * / group keys + agg aliases)
	// WHERE EACH EXPOSED COLUMN'S VALUE COMES FROM — parallel to m_columns. They differ exactly where
	// the inner query gave an ALIAS: the outer world sees the alias, the row is read through the real
	// column. (Empty on the aggregate path, which reads by group key / alias itself.)
	std::vector<const ibBackendQueryColumn*> m_readFrom;
	// …and the ones read BY ALIAS instead: a dot-walked selection has no column of its own in the
	// door, only an output name. Non-empty here means "ask the result for this name".
	std::vector<wxString>                    m_readAlias;
	// …and the ones REASSEMBLED from a field spread: a reference / enum / composite leaf is not one
	// field but several, projected under this prefix. Non-empty wins over both of the above.
	std::vector<wxString>                    m_readPrefix;
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
	virtual wxString GetQueryName()       const override { return NavigationSource()->GetQueryName(); }
	virtual const ibMetaData* GetMetaData() const override { return m_reg->GetQueryable()->GetMetaData(); }
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override { return NavigationSource()->GetPrimaryKeyColumns(); }

protected:
	const TReg* m_reg;
};

#endif
