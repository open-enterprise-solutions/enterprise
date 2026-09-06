#ifndef __IB_DATABASE_QUERY_BUILDER_H__
#define __IB_DATABASE_QUERY_BUILDER_H__

// The single Level 2 header — the WHOLE L2-1 vocabulary lives here so any L2-1 work
// is one include (no reaching into databaseLayer/* pieces; no separate
// queryIR / queryDDL / queryDML / queryRenderer / queryStatement headers):
//
//   * ibQueryIR        — the structured query the layer accepts (its currency)
//   * ibDdlStatement   — DDL family (CreateTable / DropTable / AddColumn)
//   * ibDmlStatement   — write family (Insert / Update / Delete / Upsert)
//   * ibQueryRenderer  — generic IR -> dialect SQL renderer (+ bind plan)
//   * ibQueryResult    — RAII cursor
//   * ibDatabaseQueryBuilder — the fluent build + terminal execute door
//   * ibQueryStatement — deferred-execution statement (SetParam capture)
//
// The tier ladder (all flagships share the ibDatabase* prefix):
//     L3  ibDataQueryBuilder      — what the developer WRITES (metadata query); builds IR
//     L2-1  ibDatabaseQueryBuilder  — fluent build + terminal execute, dialect-indifferent
//     L1  ibDatabaseLayer         — WHERE it runs: the physical driver (×5)
//
// The currency is a structured ibQueryIR (never raw SQL), so SQL injection is
// impossible by construction: values travel as Const/Param nodes that become
// bound parameters. The dialect difference (FIRST vs LIMIT, $n vs ?, type map,
// UPSERT spelling) is closed entirely by ibDialectDictionary; there is no
// per-DBMS fork in this layer (docs/query-language-arc.md §6, §18, §20).

#include "backend/backend.h"
#include "backend/compiler/value.h"                  // ibValue (Const node payload)
#include "backend/databaseLayer/connectionScope.h"
#include "backend/databaseLayer/databaseLayer.h"      // ibDatabaseLayer + ibDialectDictionary (merged in)
#include "backend/databaseLayer/preparedStatement.h" // ibPreparedStatement (ibQueryStatement base)
#include "backend/databaseLayer/columnType.h"        // ibColumnType — Cast target + ibDdlColumn type (dialect TYPE-MAP renders it)

#include <memory>
#include <vector>

class ibDatabaseConnectionHolder;
class ibDatabaseResultSet;
class ibResultSetMetaData;

// ==========================================================================
// Query IR — the universal, structured query L2-1 accepts. Plain data (tagged
// structs, shared_ptr children) over PHYSICAL names; carries no metadata
// knowledge (the metadata->physical mapping is a Level 3 concern).
// ==========================================================================

// Source provenance — line:col in the originating L3 query text. Zero = unknown.
struct ibQuerySpan
{
	int m_line = 0;
	int m_col  = 0;
};

struct ibQueryRel;      // fwd — the EXISTS subquery source (relation tree, defined below)
struct ibQueryWindow;   // fwd — the OVER (…) spec a Func may carry (defined below, beside the sort key it holds)

enum class ibQueryExprKind
{
	Column,   // a physical column reference (m_name)
	Const,    // a literal value (m_const) — emitted as a BOUND parameter, not inlined
	Param,    // a runtime-supplied value, addressed by m_paramIndex into the bind plan
	BinOp,    // m_binOp applied to m_lhs / m_rhs
	Func,     // m_name ( m_args... )
	Case,     // CASE WHEN m_cases[i].first THEN m_cases[i].second ... [ELSE m_else] END
	In,       // m_lhs [NOT m_negated] IN ( m_args... )
	IsNull,   // m_lhs IS [NOT m_negated] NULL
	Not,      // NOT m_lhs
	Cast,     // CAST( m_lhs AS <m_castType, spelled per-DBMS via the dialect TYPE-MAP> ) — pin an expr's type
	Exists,   // [NOT m_negated] EXISTS ( m_subquery ) — a CORRELATED subquery test; the write-path lowering of a
	          // dot-walk RLS predicate (a write cannot JOIN, so the reference path rides as a correlated EXISTS)
	// ⭐ THE REST OF THE CALENDAR — the same arrangement PeriodTrunc introduced, extended to the
	// operations a query language actually offers. Each says WHAT and lets the dictionary say HOW:
	//   PeriodEnd  — the last instant of m_periodUnit containing m_lhs
	//   DateAdd    — m_lhs moved by m_args[0] whole m_periodUnit
	//   DateDiff   — whole m_periodUnit between m_lhs and m_rhs
	//   DatePart   — one piece of m_lhs (m_datePart) as a number
	//   Substring  — m_lhs from m_args[0] for m_args[1] characters
	// They are separate kinds rather than a Func with a name, because a Func's name is SQL text and
	// this tier does not carry SQL text: that is the leak the window-function note above records.
	PeriodEnd,
	DateAdd,
	DateDiff,
	DatePart,
	Substring,
	PeriodTrunc  // start of the m_periodUnit containing m_lhs, spelled per-DBMS via the dialect's
	             // m_periodTrunc map. A SEMANTIC node, not a raw-SQL hatch: the IR says "truncate to
	             // month", never how, so L2-1 keeps its no-SQL invariant while engines that disagree
	             // structurally (strftime mask vs date_trunc vs EXTRACT+DATEADD arithmetic) each
	             // spell it their own way. Grouping by month is wanted far beyond totals — reports,
	             // the composer, user queries — which is why it is an IR node and not a private trick
	             // of the totals generator.
};

enum class ibQueryBinOp
{
	// arithmetic
	Add, Sub, Mul, Div, Mod,
	// comparison
	Eq, Ne, Lt, Le, Gt, Ge,
	// pattern match (FindByCode / FindByDescription)
	Like,
	// logical
	And, Or
};

struct ibQueryExpr
{
	ibQueryExprKind m_kind;

	// Column / Func: identifier name (column name, or function name).
	wxString m_name;

	// Column: optional table / alias qualifier — "<qualifier>.<name>". Empty =
	// unqualified. Needed once joins make a bare column ambiguous.
	wxString m_qualifier;

	// Const: the literal value (bound as a parameter at execute time).
	ibValue m_const;

	// Const (blob form): raw bytes bound via SetParamBlob — OPAQUE to L2-1. The
	// metadata layer encodes a reference (ibReference) / binary key here; L2-1
	// never interprets it, so it stays metadata-blind. Empty = not a blob.
	wxMemoryBuffer m_blob;

	// Param: index into the externally-supplied bind values (>= 0).
	int m_paramIndex = -1;

	// Func: fold over DISTINCT values of the argument — `COUNT(DISTINCT col)`. Standard SQL and
	// spelled identically by every dialect this layer writes for, so it rides the generic renderer
	// rather than becoming a per-driver branch.
	bool m_distinct = false;

	// Func: the OVER (…) that turns this call into a WINDOW function — `SUM(x) OVER (PARTITION BY
	// k ORDER BY p)`. A MODIFIER of the call, exactly like m_distinct above, which is why it is a
	// field here and not a node kind of its own: the functions are the same SUM / COUNT / MIN /
	// MAX, and only what they fold over changes. Null = an ordinary call.
	//
	// Held by pointer for the reason m_subquery is: the spec names types declared further down,
	// and a window is rare enough that every expression node should not carry two vectors.
	std::shared_ptr<ibQueryWindow> m_over;

	// BinOp: operator + operands.
	ibQueryBinOp m_binOp = ibQueryBinOp::Eq;
	std::shared_ptr<ibQueryExpr> m_lhs;
	std::shared_ptr<ibQueryExpr> m_rhs;

	// Func: argument list. Also the IN value list (In).
	std::vector<std::shared_ptr<ibQueryExpr>> m_args;

	// Case: searched WHEN -> THEN pairs, in order, + optional ELSE (m_else).
	std::vector<std::pair<std::shared_ptr<ibQueryExpr>, std::shared_ptr<ibQueryExpr>>> m_cases;
	std::shared_ptr<ibQueryExpr> m_else;

	// Cast: the CANONICAL target type. The renderer spells it per-DBMS through the dialect TYPE-MAP
	// (ibQueryRenderer::MapType) — the IR bakes no SQL type string, so it stays dialect-neutral.
	ibColumnType m_castType;

	// PeriodTrunc: the unit m_lhs is truncated to. Same principle as m_castType — the IR names the
	// CONCEPT and the dialect owns the spelling. PeriodEnd / DateAdd / DateDiff read it too — they
	// are the same question asked from the other side.
	ibTotalsPeriod m_periodUnit = ibTotalsPeriod::Month;

	// DatePart: WHICH piece of the date is wanted. A separate enum from the unit above for the reason
	// stated where it is declared — "truncate to month" and "which month is it" share a word and no
	// meaning.
	ibDatePart m_datePart = ibDatePart::Year;

	// In / IsNull / Exists: negation (NOT IN / IS NOT NULL / NOT EXISTS).
	bool m_negated = false;

	// Exists: the correlated subquery. A shared_ptr to the (fwd-declared) relation tree — the subquery
	// references the OUTER write row's columns, so it renders as a self-contained `EXISTS ( SELECT … )`.
	std::shared_ptr<ibQueryRel> m_subquery;

	ibQuerySpan m_span;

	explicit ibQueryExpr(ibQueryExprKind kind) : m_kind(kind) {}
};

using ibQueryExprPtr = std::shared_ptr<ibQueryExpr>;

// --- expression factories (readable IR construction) ----------------------

inline ibQueryExprPtr ibCol(const wxString& name)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::Column);
	e->m_name = name;
	return e;
}

// Qualified column: "<table>.<column>" — for joins (disambiguates).
inline ibQueryExprPtr ibCol(const wxString& qualifier, const wxString& name)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::Column);
	e->m_qualifier = qualifier;
	e->m_name      = name;
	return e;
}

inline ibQueryExprPtr ibConst(const ibValue& value)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::Const);
	e->m_const = value;
	return e;
}

// Opaque blob constant — bound via SetParamBlob. The metadata layer uses this
// for a reference (ibReference) / binary key; L2-1 binds the bytes without
// interpreting them.
inline ibQueryExprPtr ibConstBlob(const void* data, size_t len)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::Const);
	e->m_blob.AppendData(data, len);
	return e;
}

inline ibQueryExprPtr ibParam(int index)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::Param);
	e->m_paramIndex = index;
	return e;
}

inline ibQueryExprPtr ibBinOp(ibQueryBinOp op, ibQueryExprPtr lhs, ibQueryExprPtr rhs)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::BinOp);
	e->m_binOp = op;
	e->m_lhs   = std::move(lhs);
	e->m_rhs   = std::move(rhs);
	return e;
}

inline ibQueryExprPtr ibFunc(const wxString& name, std::vector<ibQueryExprPtr> args = {},
	bool distinct = false)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::Func);
	e->m_name = name;
	e->m_args = std::move(args);
	e->m_distinct = distinct;
	return e;
}

// Searched CASE: CASE WHEN c1 THEN v1 ... [ELSE e] END. `cases` = WHEN/THEN pairs in
// order; `elseExpr` null = no ELSE. Usable anywhere an expression is (projection /
// predicate / sort key) — e.g. enum ordering by a CASE over the row key.
inline ibQueryExprPtr ibCase(std::vector<std::pair<ibQueryExprPtr, ibQueryExprPtr>> cases,
                             ibQueryExprPtr elseExpr = nullptr)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::Case);
	e->m_cases = std::move(cases);
	e->m_else  = std::move(elseExpr);
	return e;
}

// lhs [NOT] IN (values...). Each value is a Const / Param (bound). An empty list
// renders as a constant predicate (1=0 / 1=1) — the lhs is not evaluated, so pass a
// column lhs (no bind) for that edge.
inline ibQueryExprPtr ibIn(ibQueryExprPtr lhs, std::vector<ibQueryExprPtr> values, bool negated = false)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::In);
	e->m_lhs     = std::move(lhs);
	e->m_args    = std::move(values);
	e->m_negated = negated;
	return e;
}

// lhs IS NULL / IS NOT NULL.
inline ibQueryExprPtr ibIsNull(ibQueryExprPtr lhs, bool negated = false)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::IsNull);
	e->m_lhs     = std::move(lhs);
	e->m_negated = negated;
	return e;
}

// NOT operand (logical negation).
inline ibQueryExprPtr ibNot(ibQueryExprPtr operand)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::Not);
	e->m_lhs = std::move(operand);
	return e;
}

// CAST(expr AS <type>). `castType` is the CANONICAL target type; the renderer spells it per-DBMS
// through the dialect TYPE-MAP (ibQueryRenderer::MapType), so the IR bakes no dialect SQL string.
// Used where an expression's result type must be pinned (aggregate results — register balances /
// turnovers, whose SUM(CASE …) the driver may otherwise type loosely or too narrow: FB// defaults a bare NUMERIC / DECIMAL to (9,0) and truncates the fraction).
inline ibQueryExprPtr ibCast(ibQueryExprPtr expr, const ibColumnType& castType)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::Cast);
	e->m_lhs      = std::move(expr);
	e->m_castType = castType;
	return e;
}

// Start of the period containing `expr` — "the month this timestamp falls in". The unit is the
// concept; the dialect owns the spelling (ibDialectDictionary::m_periodTrunc). Group by the result
// to roll rows up to that grain: one definition of "start of the month" per engine, shared by
// every caller, so a totals key and a report's grouping cannot silently differ.
inline ibQueryExprPtr ibPeriodTrunc(ibQueryExprPtr expr, ibTotalsPeriod unit)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::PeriodTrunc);
	e->m_lhs        = std::move(expr);
	e->m_periodUnit = unit;
	return e;
}

// The calendar's other four, built the same way and for the same reason: the caller names the
// operation, the dictionary spells it, and nothing above this line carries SQL text.
inline ibQueryExprPtr ibPeriodEnd(ibQueryExprPtr expr, ibTotalsPeriod unit)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::PeriodEnd);
	e->m_lhs        = std::move(expr);
	e->m_periodUnit = unit;
	return e;
}

inline ibQueryExprPtr ibDateAdd(ibQueryExprPtr expr, ibTotalsPeriod unit, ibQueryExprPtr count)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::DateAdd);
	e->m_lhs        = std::move(expr);
	e->m_periodUnit = unit;
	if (count) e->m_args.push_back(std::move(count));
	return e;
}

inline ibQueryExprPtr ibDateDiff(ibQueryExprPtr from, ibQueryExprPtr to, ibTotalsPeriod unit)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::DateDiff);
	e->m_lhs        = std::move(from);
	e->m_rhs        = std::move(to);
	e->m_periodUnit = unit;
	return e;
}

inline ibQueryExprPtr ibDatePartOf(ibQueryExprPtr expr, ibDatePart part)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::DatePart);
	e->m_lhs      = std::move(expr);
	e->m_datePart = part;
	return e;
}

inline ibQueryExprPtr ibSubstring(ibQueryExprPtr expr, ibQueryExprPtr from, ibQueryExprPtr len)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::Substring);
	e->m_lhs = std::move(expr);
	if (from) e->m_args.push_back(std::move(from));
	if (len)  e->m_args.push_back(std::move(len));
	return e;
}

// BETWEEN is sugar — desugars to (x >= lo AND x <= hi), no dedicated node. Pass a
// column x (it appears on both sides; a column has no bind, so no double-binding).
inline ibQueryExprPtr ibBetween(ibQueryExprPtr x, ibQueryExprPtr lo, ibQueryExprPtr hi)
{
	return ibBinOp(ibQueryBinOp::And,
	               ibBinOp(ibQueryBinOp::Ge, x, lo),
	               ibBinOp(ibQueryBinOp::Le, x, hi));
}

enum class ibQueryRelKind
{
	Scan,      // a physical table (m_table); no input
	Filter,    // input WHERE m_predicate
	Project,   // SELECT m_projection FROM input  (empty projection = SELECT *)
	Sort,      // input ORDER BY m_sortKeys
	Limit,     // input LIMIT m_limitCount OFFSET m_limitOffset
	Join,      // m_input <type> JOIN m_right ON m_joinPredicate  (a FROM-tree source)
	Aggregate, // SELECT m_projection FROM input GROUP BY m_groupKeys [HAVING m_having]
	Subquery,  // ( SELECT ... from m_input ) AS m_alias — a derived-table FROM source
	Distinct,  // SELECT DISTINCT over m_input
	Union,     // m_input UNION m_right
	UnionAll   // m_input UNION ALL m_right
};

// Join flavour. FULL is the only one with real dialect divergence — gated by
// ibDialectDictionary::m_features.m_fullOuterJoin (emulation is a later step).
enum class ibQueryJoinType { Inner, Left, Right, Full };

enum class ibQuerySortDir { Asc, Desc };

struct ibQuerySortKey
{
	ibQueryExprPtr m_expr;
	ibQuerySortDir m_dir = ibQuerySortDir::Asc;
};

// ⭐⭐ WHICH ROWS OF THE PARTITION THE FUNCTION FOLDS — and this is a DECISION, not a detail.
//
// There is deliberately no `Default` member. The three engines' unstated defaults are not required
// to agree with each other, and a running balance that quietly changed shape between Firebird and
// PostgreSQL would be wrong in the way nobody audits: plausible numbers, reconciling to nothing.
// Every aggregate window says which of the two it means.
enum class ibQueryFrame
{
	// RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW — PEERS INCLUDED. Every row whose sort key
	// is EQUAL to this one contributes, so the answer does not depend on the order rows happen to
	// arrive in within one period. This is what a running BALANCE wants: three movements stamped
	// with the same period are one period's worth of stock, in any order.
	RangeThroughPeers,

	// ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW — strictly row by row. Correct ONLY where the
	// sort key is unique BY CONSTRUCTION; with ties the figures move between runs, which is a defect
	// that reports itself as "the numbers changed" long after the query that caused it.
	RowsThroughCurrent,

	// No frame clause at all. RANKING functions (ROW_NUMBER / RANK / DENSE_RANK / LAG / LEAD) take
	// none and SQL forbids one there, and a window with no ORDER BY folds the whole partition by
	// definition — "the total of the group", the denominator of a share-of-total.
	//
	// ⚠ NOT a way to say "whatever the engine does" for an ordered aggregate. That is the case the
	// two members above exist to make explicit.
	NoFrame
};

// The OVER (…) spec. Empty partition = one partition (the whole result); empty order = the function
// folds the partition unordered, which is what a share-of-total denominator is.
struct ibQueryWindow
{
	std::vector<ibQueryExprPtr> m_partitionBy;
	std::vector<ibQuerySortKey> m_orderBy;
	ibQueryFrame                m_frame = ibQueryFrame::NoFrame;
};

// Attach a window to a call: `ibWindowed(ibFunc("SUM", {ibCol("qty")}), { {ibCol("wh")}, {…}, frame })`.
// Returns the SAME expression so it composes inline. Deliberately a separate step rather than more
// arguments on ibFunc: an extra argument travels into every projection-building call site (m_distinct
// went that way once and surfaced in six), a field travels nowhere.
inline ibQueryExprPtr ibWindowed(ibQueryExprPtr func, ibQueryWindow window)
{
	func->m_over = std::make_shared<ibQueryWindow>(std::move(window));
	return func;
}

struct ibQueryProjItem
{
	ibQueryExprPtr m_expr;
	wxString       m_alias;   // optional output alias; empty = derive
};

struct ibQueryRel
{
	ibQueryRelKind m_kind;

	// Child relation (null for Scan, set for the rest).
	std::shared_ptr<ibQueryRel> m_input;

	// Scan
	wxString m_table;

	// Subquery: the alias the derived table is exposed under (FROM (...) AS m_alias).
	// The inner relation is m_input.
	wxString m_alias;

	// Filter
	ibQueryExprPtr m_predicate;

	// Project (empty = SELECT *)
	std::vector<ibQueryProjItem> m_projection;

	// Sort
	std::vector<ibQuerySortKey> m_sortKeys;

	// Limit ( m_limitCount < 0 = no row cap )
	long m_limitCount  = -1;
	long m_limitOffset = 0;

	// Join: m_input is the LEFT source, m_right the RIGHT source,
	// m_joinPredicate the ON condition, m_joinType the flavour.
	std::shared_ptr<ibQueryRel> m_right;
	ibQueryExprPtr              m_joinPredicate;
	ibQueryJoinType             m_joinType = ibQueryJoinType::Inner;

	// Aggregate: GROUP BY keys + optional HAVING. m_projection (above) holds the
	// SELECT list — the group columns plus aggregate expressions (SUM(x) AS bal,
	// rendered as ibFunc projection items). HAVING filters POST-aggregation, so it
	// is distinct from Filter's WHERE m_predicate.
	std::vector<ibQueryExprPtr> m_groupKeys;
	ibQueryExprPtr              m_having;

	// Aggregate: GROUP BY ROLLUP(keys) — the DBMS computes every subtotal LEVEL (each from raw
	// detail, so correct for COUNT / AVG, unlike re-aggregating leaf sums) + a grand total in ONE
	// pass. The renderer wraps the keys with the dialect's rollup prefix/suffix; only set by L3's
	// totals push-down when the dialect advertises m_features.m_rollup. (docs/query-language-arc.md §22.1b)
	bool m_rollup = false;

	ibQuerySpan m_span;

	explicit ibQueryRel(ibQueryRelKind kind) : m_kind(kind) {}
};

using ibQueryRelPtr = std::shared_ptr<ibQueryRel>;

// --- relation factories ----------------------------------------------------

// Scan a physical table, optionally under an ALIAS (FROM table AS alias). The alias
// lets the same table be joined more than once in one query (a dot-walk that reaches
// two different reference columns pointing at the same catalog) and disambiguates
// shared column names — qualify the aliased side's columns with ibCol(alias, name).
inline ibQueryRelPtr ibScan(const wxString& table, const wxString& alias = wxEmptyString)
{
	auto r = std::make_shared<ibQueryRel>(ibQueryRelKind::Scan);
	r->m_table = table;
	r->m_alias = alias;
	return r;
}

// Join two sources on a predicate. left / right are themselves Scan or Join.
inline ibQueryRelPtr ibJoin(ibQueryRelPtr left, ibQueryRelPtr right,
                            ibQueryExprPtr on, ibQueryJoinType type = ibQueryJoinType::Inner)
{
	auto r = std::make_shared<ibQueryRel>(ibQueryRelKind::Join);
	r->m_input         = std::move(left);
	r->m_right         = std::move(right);
	r->m_joinPredicate = std::move(on);
	r->m_joinType      = type;
	return r;
}

// Derived table: use a whole relation as a FROM source, aliased. The inner tree
// renders as its own (SELECT ...); the outer query references it as `alias`. This is
// what lets a nested aggregate / filtered subquery be a join input — e.g. the register
// slice's "MAX(period) GROUP BY dims" inner select joined back to the table:
//   ibJoin(ibSubquery(aggInner, "T1"), ibScan("Reg"), onPredicate)
inline ibQueryRelPtr ibSubquery(ibQueryRelPtr inner, const wxString& alias)
{
	auto r = std::make_shared<ibQueryRel>(ibQueryRelKind::Subquery);
	r->m_input = std::move(inner);
	r->m_alias = alias;
	return r;
}

// SELECT DISTINCT over the input chain.
inline ibQueryRelPtr ibDistinct(ibQueryRelPtr input)
{
	auto r = std::make_shared<ibQueryRel>(ibQueryRelKind::Distinct);
	r->m_input = std::move(input);
	return r;
}

// left UNION right (set; dedups) / left UNION ALL right (bag; keeps duplicates). For
// an ORDER BY / LIMIT over a union, wrap the union in ibSubquery (SQL requires it too).
inline ibQueryRelPtr ibUnion(ibQueryRelPtr left, ibQueryRelPtr right)
{
	auto r = std::make_shared<ibQueryRel>(ibQueryRelKind::Union);
	r->m_input = std::move(left);
	r->m_right = std::move(right);
	return r;
}
inline ibQueryRelPtr ibUnionAll(ibQueryRelPtr left, ibQueryRelPtr right)
{
	auto r = std::make_shared<ibQueryRel>(ibQueryRelKind::UnionAll);
	r->m_input = std::move(left);
	r->m_right = std::move(right);
	return r;
}

inline ibQueryRelPtr ibFilter(ibQueryRelPtr input, ibQueryExprPtr predicate)
{
	auto r = std::make_shared<ibQueryRel>(ibQueryRelKind::Filter);
	r->m_input     = std::move(input);
	r->m_predicate = std::move(predicate);
	return r;
}

inline ibQueryRelPtr ibProject(ibQueryRelPtr input, std::vector<ibQueryProjItem> projection = {})
{
	auto r = std::make_shared<ibQueryRel>(ibQueryRelKind::Project);
	r->m_input      = std::move(input);
	r->m_projection = std::move(projection);
	return r;
}

inline ibQueryRelPtr ibSort(ibQueryRelPtr input, std::vector<ibQuerySortKey> keys)
{
	auto r = std::make_shared<ibQueryRel>(ibQueryRelKind::Sort);
	r->m_input    = std::move(input);
	r->m_sortKeys = std::move(keys);
	return r;
}

inline ibQueryRelPtr ibLimit(ibQueryRelPtr input, long count, long offset = 0)
{
	auto r = std::make_shared<ibQueryRel>(ibQueryRelKind::Limit);
	r->m_input       = std::move(input);
	r->m_limitCount  = count;
	r->m_limitOffset = offset;
	return r;
}

// Aggregate: SELECT projection FROM input GROUP BY groupKeys [HAVING having]. The
// projection holds group columns + aggregate items (e.g. { ibFunc("SUM",{ibCol("q")}),
// "balance" }); groupKeys are the GROUP BY expressions; having is an optional
// post-aggregation predicate. Virtual-table providers (register balances /
// turnovers) build their compute query through this node.
inline ibQueryRelPtr ibAggregate(ibQueryRelPtr input,
                                 std::vector<ibQueryProjItem> projection,
                                 std::vector<ibQueryExprPtr> groupKeys,
                                 ibQueryExprPtr having = nullptr,
                                 bool rollup = false)
{
	auto r = std::make_shared<ibQueryRel>(ibQueryRelKind::Aggregate);
	r->m_input      = std::move(input);
	r->m_projection = std::move(projection);
	r->m_groupKeys  = std::move(groupKeys);
	r->m_having     = std::move(having);
	r->m_rollup     = rollup;
	return r;
}

// EXISTS ( <subquery> ) as an ibQueryExpr (not a relation). The write path lowers a dot-walk RLS predicate
// to this: the subquery scans the referenced target(s) and correlates back to the OUTER write row's
// reference column (a write cannot JOIN). `negated` -> NOT EXISTS. Defined here — needs ibQueryRelPtr.
inline ibQueryExprPtr ibExists(ibQueryRelPtr subquery, bool negated = false)
{
	auto e = std::make_shared<ibQueryExpr>(ibQueryExprKind::Exists);
	e->m_subquery = std::move(subquery);
	e->m_negated  = negated;
	return e;
}

// ⭐ ONE NAMED QUERY, WRITTEN ONCE AND READ BY NAME — a common table expression.
//
// It exists for the thing a package already says: a statement NAMES its result (`ONTO`) and later
// statements read it. Substituting that statement's text at every mention is correct and costs a
// second execution the moment there are two mentions; `WITH Sales AS (…)` says it once and every
// mention is a name the engine resolves.
//
// The relation is an ORDINARY one — the same tree a FROM subquery holds — so nothing about building
// a query changes; only where it is written.
struct ibQueryCte
{
	wxString      m_name;
	ibQueryRelPtr m_query;
};

struct ibQueryIR
{
	ibQueryRelPtr m_root;

	// THE NAMED QUERIES THIS STATEMENT READS, in the order they must be declared — an engine reads
	// them top to bottom, so a CTE that mentions an earlier one has to come after it.
	//
	// ⚠ Only a dialect that HAS `WITH` may be handed these (ibSqlFeatures::m_cte). The renderer
	// refuses rather than inlining them: an engine without CTEs needs the SUBQUERY form, and that is
	// a different tree — deciding it here would be L2 quietly rewriting the query it was given.
	std::vector<ibQueryCte> m_with;

	// Pessimistic read-for-update: the renderer appends the dialect's row-lock clause
	// (m_rowLockSuffix) to the TOP-level SELECT. Used by the register set lock + ibLockManager.
	// m_lockNoWait additionally appends m_rowLockNoWaitSuffix (e.g. " NOWAIT") so a non-blocking
	// acquire fails fast instead of queueing on a held row. (docs/record-locks.md)
	bool m_lockForUpdate = false;
	bool m_lockNoWait    = false;

	ibQueryIR() = default;
	explicit ibQueryIR(ibQueryRelPtr root) : m_root(std::move(root)) {}
};

// ==========================================================================
// DDL — the CreateTable / DropTable / AddColumn family. The Dialect
// Dictionary's TYPE-MAP closes the per-DBMS type forks (BLOB vs BYTEA, DATE vs
// TIMESTAMP, boolean-as-SMALLINT).
// ==========================================================================

// The canonical column type + factories (ibColumnType / ibTypeNumber / …) live in a shared low-tier
// vocabulary used by both L2-1 and the L3 layout tier (included at the top). ibDdlColumn below carries one.

struct ibDdlColumn
{
	wxString     m_name;
	ibColumnType m_type;
	bool         m_notNull    = false;
	bool         m_primaryKey = false;   // PRIMARY KEY implies NOT NULL
	wxString     m_default;              // DEFAULT clause value (literal SQL), empty = none
};

// A single clause inside a batched ALTER TABLE. The structure builder coalesces consecutive
// same-op column deltas of one table into one statement (`ALTER TABLE t ADD c1, ADD c2`) — far
// fewer round-trips than one ALTER per column. Modify is NOT batched (its per-DBMS spelling is a
// whole-statement template); it stays a standalone ibAlterColumn.
enum class ibAlterOp { Add, Drop };

struct ibAlterClause
{
	ibAlterOp   m_op;
	ibDdlColumn m_column;   // Add: the full column; Drop: only m_column.m_name is used
};

enum class ibDdlKind { CreateTable, DropTable, AddColumn, DropColumn, AlterColumn, AlterTable, CreateIndex, DropIndex, Analyze };

struct ibDdlStatement
{
	ibDdlKind m_kind;

	wxString m_table;

	// CreateTable: all columns. AddColumn / DropColumn: one column (m_columns[0];
	// DropColumn uses only its m_name).
	std::vector<ibDdlColumn> m_columns;

	// AlterTable: the coalesced column clauses, rendered as one multi-clause ALTER.
	std::vector<ibAlterClause> m_alterClauses;

	// CreateIndex / DropIndex.
	wxString              m_indexName;
	std::vector<wxString> m_indexColumns;   // CreateIndex: the indexed columns
	bool                  m_unique = false; // CreateIndex: UNIQUE

	bool m_ifExists    = false;   // DropTable
	bool m_ifNotExists = false;   // CreateTable

	// CreateTable as a TEMPORARY table — the lexical bits come from the L1 ibTempTableDialect (the
	// renderer stays a pure function of DDL + main dialect; the caller, the temp-table manager,
	// supplies these from the driver's temp facts, so there is NO per-driver fork in the renderer).
	bool     m_temporary    = false;   // emit m_createPrefix instead of "CREATE TABLE" + append m_createSuffix
	wxString m_createPrefix;           // e.g. "CREATE TEMPORARY TABLE"
	wxString m_createSuffix;           // appended after the column list, e.g. " ON COMMIT DROP" (empty = none)

	explicit ibDdlStatement(ibDdlKind kind) : m_kind(kind) {}
};

inline ibDdlStatement ibCreateTable(const wxString& table, std::vector<ibDdlColumn> columns, bool ifNotExists = false)
{
	ibDdlStatement s(ibDdlKind::CreateTable);
	s.m_table        = table;
	s.m_columns      = std::move(columns);
	s.m_ifNotExists  = ifNotExists;
	return s;
}

inline ibDdlStatement ibDropTable(const wxString& table, bool ifExists = false)
{
	ibDdlStatement s(ibDdlKind::DropTable);
	s.m_table     = table;
	s.m_ifExists  = ifExists;
	return s;
}

// ANALYZE a table — refresh the optimiser's statistics so it plans against real cardinality
// (after a temp materialise, a bulk load, or a restructure). The per-driver form lives in the
// dialect (m_analyzePrefix: PG/SQLite "ANALYZE", FB empty); a driver with
// no ANALYZE renders to empty and Execute no-ops. (docs/temp-db.md)
inline ibDdlStatement ibAnalyzeTable(const wxString& table)
{
	ibDdlStatement s(ibDdlKind::Analyze);
	s.m_table = table;
	return s;
}

// CREATE a TEMPORARY table. The temp lexical bits (createPrefix / createSuffix) come from the L1
// ibTempTableDialect, so this stays driver-agnostic — the temp-table manager fills them from the
// connected driver's facts. (docs/temp-db.md)
inline ibDdlStatement ibCreateTempTable(const wxString& table, std::vector<ibDdlColumn> columns,
                                        const wxString& createPrefix, const wxString& createSuffix = wxEmptyString)
{
	ibDdlStatement s(ibDdlKind::CreateTable);
	s.m_table        = table;
	s.m_columns      = std::move(columns);
	s.m_temporary    = true;
	s.m_createPrefix = createPrefix;
	s.m_createSuffix = createSuffix;
	return s;
}

inline ibDdlStatement ibAddColumn(const wxString& table, ibDdlColumn column)
{
	ibDdlStatement s(ibDdlKind::AddColumn);
	s.m_table = table;
	s.m_columns.push_back(std::move(column));
	return s;
}

// ALTER TABLE <table> DROP COLUMN <column>. (AlterColumn / type change is NOT here —
// SQLite cannot alter a column's type, it needs a table rebuild; a separate concern.)
inline ibDdlStatement ibDropColumn(const wxString& table, const wxString& column)
{
	ibDdlStatement s(ibDdlKind::DropColumn);
	s.m_table = table;
	ibDdlColumn c; c.m_name = column;
	s.m_columns.push_back(std::move(c));
	return s;
}

// The same DROP COLUMN carrying the FULL column description, not just its name. The renderer
// still spells only the name; the extra shape is for the barrier's compensation ledger, which
// re-adds the column (EMPTY — the data died with the first commit) if the second phase fails.
inline ibDdlStatement ibDropColumn(const wxString& table, ibDdlColumn column)
{
	ibDdlStatement s(ibDdlKind::DropColumn);
	s.m_table = table;
	s.m_columns.push_back(std::move(column));
	return s;
}

// ALTER TABLE <table> ALTER/MODIFY COLUMN <column> <new-type> — change a column's
// type. The per-DBMS spelling is the dialect's m_alterColumnTemplate; SQLite cannot do
// it in place and its (empty) template makes the renderer throw.
inline ibDdlStatement ibAlterColumn(const wxString& table, ibDdlColumn column)
{
	ibDdlStatement s(ibDdlKind::AlterColumn);
	s.m_table = table;
	s.m_columns.push_back(std::move(column));
	return s;
}

// The same ALTER with the PREVIOUS shape riding second (m_columns[1]). Renderers read only the
// front; the barrier's compensation ledger reads the tail to restore the column's old type when
// the second phase fails. Without it the alter is irreversible and the compensation says so.
inline ibDdlStatement ibAlterColumn(const wxString& table, ibDdlColumn column, ibDdlColumn prev)
{
	ibDdlStatement s(ibDdlKind::AlterColumn);
	s.m_table = table;
	s.m_columns.push_back(std::move(column));
	s.m_columns.push_back(std::move(prev));
	return s;
}

// ALTER TABLE <table> <clause>, <clause>, … — a batched column delta (ADD / DROP COLUMN folded into
// one statement). The structure builder coalesces consecutive same-op clauses of one table into this;
// dialects without multi-clause ALTER (SQLite) get one statement per clause from the builder instead.
inline ibDdlStatement ibAlterTable(const wxString& table, std::vector<ibAlterClause> clauses)
{
	ibDdlStatement s(ibDdlKind::AlterTable);
	s.m_table        = table;
	s.m_alterClauses = std::move(clauses);
	return s;
}

// CREATE [UNIQUE] INDEX <name> ON <table> (<columns>).
inline ibDdlStatement ibCreateIndex(const wxString& table, const wxString& indexName,
                                    std::vector<wxString> columns, bool unique = false)
{
	ibDdlStatement s(ibDdlKind::CreateIndex);
	s.m_table        = table;
	s.m_indexName    = indexName;
	s.m_indexColumns = std::move(columns);
	s.m_unique       = unique;
	return s;
}

// DROP INDEX <name> [ON <table>]. The table is needed only by dialects that require it
// (MSSQL — see ibDialectDictionary::m_dropIndexNeedsTable); pass it for portability.
inline ibDdlStatement ibDropIndex(const wxString& indexName, const wxString& table = wxEmptyString)
{
	ibDdlStatement s(ibDdlKind::DropIndex);
	s.m_indexName = indexName;
	s.m_table     = table;
	return s;
}

// ==========================================================================
// DML — Insert / Update / Delete / Upsert. Values + predicates are ordinary
// ibQueryExpr (Const / Param become bound parameters), so injection is
// impossible by construction here too.
// ==========================================================================

// A column assignment: <column> = <value-expr>.
struct ibDmlAssign
{
	wxString       m_column;
	ibQueryExprPtr m_value;
};

enum class ibDmlKind { Insert, Update, Delete, Upsert };

struct ibDmlStatement
{
	ibDmlKind m_kind;

	wxString m_table;

	// Insert + Update + Upsert.
	std::vector<ibDmlAssign> m_assignments;

	// Insert ONLY — extra VALUES tuples for a multi-row INSERT: m_assignments is row 0 (it carries the
	// column list), each m_extraRows[i] is one further row's values IN THE SAME COLUMN ORDER. Empty =
	// a plain single-row INSERT. Used by the temp-table manager to bulk-fill in chunks. (docs/temp-db.md)
	std::vector<std::vector<ibQueryExprPtr>> m_extraRows;

	// Update + Delete (null = no WHERE — affects all rows).
	ibQueryExprPtr m_where;

	// Upsert: the conflict / match columns (the PK, e.g. "uuid"). The dialect
	// renders them as FB MATCHING(...) / PG-SQLite ON CONFLICT(...); they are	// excluded from the UPDATE SET (the PK never changes).
	std::vector<wxString> m_matchKeys;

	// Insert ONLY — INSERT … SELECT. When set, the row source is this relation tree instead of a
	// VALUES list (m_assignments is then ignored). m_insertColumns is the optional target column list
	// (empty => `INSERT INTO t SELECT …`). Standard SQL on every driver — no dialect fork.
	ibQueryRelPtr         m_selectSource;
	std::vector<wxString> m_insertColumns;

	// RETURNING — columns the statement hands back from the rows it wrote. Empty (default) =
	// a plain write with no cursor. Set it through ibReturning() and run the statement with
	// ibDatabaseQueryBuilder::ExecuteReturning, which is the overload that yields a cursor.
	// The spelling lives in the dialect (m_returningClause); a driver without one throws.
	std::vector<wxString> m_returning;

	explicit ibDmlStatement(ibDmlKind kind) : m_kind(kind) {}
};

inline ibDmlStatement ibInsert(const wxString& table, std::vector<ibDmlAssign> assignments)
{
	ibDmlStatement s(ibDmlKind::Insert);
	s.m_table       = table;
	s.m_assignments = std::move(assignments);
	return s;
}

// INSERT INTO <table> [(columns)] <SELECT …>. The source is any relation tree (ibScan / ibProject /
// ibFilter / Join). Empty `columns` emits `INSERT INTO t SELECT …` (column-position match, same
// contract as raw SQL). Standard SQL — rendered identically on all drivers.
inline ibDmlStatement ibInsertSelect(const wxString& table, std::vector<wxString> columns, ibQueryRelPtr source)
{
	ibDmlStatement s(ibDmlKind::Insert);
	s.m_table         = table;
	s.m_insertColumns = std::move(columns);
	s.m_selectSource  = std::move(source);
	return s;
}

inline ibDmlStatement ibUpdate(const wxString& table, std::vector<ibDmlAssign> assignments, ibQueryExprPtr where = nullptr)
{
	ibDmlStatement s(ibDmlKind::Update);
	s.m_table       = table;
	s.m_assignments = std::move(assignments);
	s.m_where       = std::move(where);
	return s;
}

inline ibDmlStatement ibDelete(const wxString& table, ibQueryExprPtr where = nullptr)
{
	ibDmlStatement s(ibDmlKind::Delete);
	s.m_table = table;
	s.m_where = std::move(where);
	return s;
}

// Ask a write to hand back columns from the rows it wrote:
//
//     ibReturning(ibUpdate(table, { { wxT("number"), … } }, where), { wxT("number") })
//
// A modifier rather than a parameter on every factory: RETURNING is orthogonal to WHICH
// write it is, and threading it through ibInsert / ibUpdate / ibDelete / ibUpsert would
// have put an empty vector in every existing call site. Run it with ExecuteReturning.
inline ibDmlStatement ibReturning(ibDmlStatement dml, std::vector<wxString> columns)
{
	dml.m_returning = std::move(columns);
	return dml;
}

// UPSERT: INSERT the row, or UPDATE it in place when a row with the same
// matchKeys (PK) already exists. The per-DBMS spelling is closed by the
// dialect's UPSERT template; matchKeys are excluded from the UPDATE half.
inline ibDmlStatement ibUpsert(const wxString& table, std::vector<ibDmlAssign> assignments,
                               std::vector<wxString> matchKeys)
{
	ibDmlStatement s(ibDmlKind::Upsert);
	s.m_table       = table;
	s.m_assignments = std::move(assignments);
	s.m_matchKeys   = std::move(matchKeys);
	return s;
}

// ==========================================================================
// Renderer — generic, DBMS-indifferent IR -> SQL. PURE: no connection, no DB.
// The same IR through two dictionaries yields two dialect-correct SQL strings.
// ==========================================================================

// One entry of the bind plan, in placeholder order.
struct ibQueryParam
{
	bool    m_external      = false;  // true = bind from the caller-supplied vector
	int     m_externalIndex = -1;     // valid when m_external
	ibValue m_value;                  // valid when !m_external && !m_isBlob (Const literal)

	bool           m_isBlob = false;  // true = bind m_blob via SetParamBlob (opaque bytes)
	wxMemoryBuffer m_blob;            // valid when m_isBlob
};

struct ibRenderedQuery
{
	wxString                  m_sql;
	std::vector<ibQueryParam> m_params;  // in placeholder order — bind 1:1, left to right
};

// Canonical column type -> this dialect's SQL type name. THE map, used by the DDL renderer below
// and by the view renderer at L2-2 (databaseMaterializeBuilder), which holds a dictionary rather
// than a builder and would otherwise need a copy of the same switch.
BACKEND_API wxString ibMapColumnType(const ibDialectDictionary& dialect, const ibColumnType& type);

// THE spelling of an OVER (…) clause — one place, two doors, for the same reason ibMapColumnType is
// one place: the IR renderer below reaches it with rendered expressions, and L2-2's view generator
// reaches it with the SQL text it already holds. Both hand over FINISHED operand strings, so this
// function owns the clause and nothing else.
//
// It is also where the capability is CHECKED. An engine without window functions is refused here
// with UnsupportedNode — the same answer RETURNING gives — because every emulation of a running
// total (a self-join per row, a correlated subquery) silently changes the cost of a report from
// linear to quadratic, and a query that merely got slower is not a failure anyone reports.
BACKEND_API wxString ibRenderOverClause(const ibDialectDictionary& dialect,
                                        const std::vector<wxString>& partitionBy,
                                        const std::vector<wxString>& orderBy,
                                        ibQueryFrame frame);

class BACKEND_API ibQueryRenderer
{
public:
	explicit ibQueryRenderer(const ibDialectDictionary& dialect) : m_dialect(dialect) {}

	ibRenderedQuery Render(const ibQueryIR& ir);

	// DDL has no result set and no bind params (MVP) — returns SQL text only.
	wxString RenderDDL(const ibDdlStatement& ddl);

	// DML (Insert/Update/Delete/Upsert) — SQL + bind plan, like Render().
	ibRenderedQuery RenderDML(const ibDmlStatement& dml);

	// (No view-body renderer here. A VIEW's body is produced by L2-2 — see
	//  databaseMaterializeBuilder — because the bodies that matter carry shard folds, which the
	//  query IR has no nodes for. Adding an inline-literal mode to THIS renderer would have meant
	//  owning per-dialect quote escaping for a caller that does not exist.
	//  Window functions used to be on that list and no longer are: they are a field on Func
	//  (m_over), and both renderers now spell the clause through the one ibRenderOverClause.)

private:

	wxString RenderSelect(const ibQueryRel* root);  // flatten a relation chain into one SELECT (appends binds)
	wxString RenderExpr(const ibQueryExprPtr& expr);
	// Fill EVERY occurrence of a template placeholder, rendering the operand once PER occurrence — a
	// dialect template may name its operand several times, and a bound operand has to bind as often
	// as it is spelled. See the body for what a single render cost.
	wxString FillRepeated(const wxString& tpl, const wxString& key, const ibQueryExprPtr& operand);
	wxString RenderOver(const ibQueryWindow& window);  // renders the operands, spells the clause via ibRenderOverClause
	wxString RenderSource(const ibQueryRel* rel);   // FROM source: Scan / Join-tree / Subquery (recursive)
	wxString RenderPlaceholder();              // spells the next placeholder, bumps the counter
	wxString QuoteIdent(const wxString& name) const;
	static wxString BinOpText(ibQueryBinOp op);
	static wxString JoinTypeText(ibQueryJoinType type);

	wxString RenderColumn(const ibDdlColumn& col);      // "<name> <sqltype> [PRIMARY KEY|NOT NULL]"
	wxString MapType(const ibColumnType& type) const;   // canonical type -> dialect SQL type (thin wrapper over ibMapColumnType)

	ibDialectDictionary m_dialect;             // BY VALUE — a renderer built from a dialect TEMPORARY
	                                           // (`ibQueryRenderer r(FbDialect()); … r.RenderDDL()`) outlives it;
	                                           // a reference member would dangle. The dictionary is small + copyable.
	ibRenderedQuery            m_out;          // accumulates during a Render() call
	int                        m_paramPos = 0; // running 1-based placeholder count

};

// ==========================================================================
// WHAT THE CONNECTED DRIVER CAN DO — asked of L2, never of the dictionary
// ==========================================================================
//
// ⭐⭐ A CAPABILITY IS A QUESTION L2 ANSWERS, NOT A FIELD ITS CALLERS READ.
//
// The dialect dictionary is L2's own vocabulary. Every tier above that reached into it
// (`layer->GetDialect().m_features.m_rollup`, `…m_indexListQuery`) was spelling an L2 fact in its
// own words, and the day the fact changes shape — a feature that becomes two, a template that grows
// an argument — the compiler finds the definition and not the readers. So the door vends the
// QUESTION and keeps the field to itself.
//
// Each of these has exactly one thing to say and says it about a CONNECTION, because that is what
// the answer depends on: not "does this product support X" but "can the driver I am holding do X".
// A null layer answers NO / EMPTY rather than crashing — a passive pool is an ordinary state.

// GROUP BY ROLLUP — can the subtotal levels be folded by the SERVER, or must the result tier build
// them? (FB5, PG yes; SQLite no.)
BACKEND_API bool ibCanPushRollup(const ibDatabaseLayer* layer);
BACKEND_API bool ibCanUseGrouping(const ibDatabaseLayer* layer);   // GROUPING(expr) — see ibSqlFeatures

// OVER (…) — can this driver rank and run totals ITSELF? A reading that folds periods, or picks the
// row nearest a moment, is one pass with windows and a stack of self-joins without. (FB3+, PG,
// SQLite 3.25+ yes; the ANSI baseline no.) Asked before choosing the SHAPE of the query — unlike
// ibRenderOverClause, which refuses once a window has already been built.
BACKEND_API bool ibCanPushWindow(const ibDatabaseLayer* layer);

// WITH … AS (…) — can this driver read a query the statement NAMES? It is what lets a named result
// of a package stay INSIDE the SQL: without it the named query has to come back as rows and be
// joined here. (FB 2.1+, PG, SQLite 3.8.3+ yes; the ANSI baseline — ODBC — no.) Asked before
// choosing the shape, unlike the renderer, which refuses once a WITH has already been built.
BACKEND_API bool ibCanUseCte(const ibDatabaseLayer* layer);

// (No `ibCanUseTempTables` here, and the reason is worth keeping: the per-driver temp facts'
//  PRESENCE already IS that capability, and the one caller — the temp-table manager — needs the
//  facts themselves for the CREATE's lexical bits. An accessor beside them would be a second
//  spelling of one null test, removing no knowledge from the caller. `GetTempTableDialect()` is
//  already the question.)

// DDL THAT CHANGES A TABLE'S SHAPE, RUN. Render through this driver's dialect and execute — the two
// halves were spelled at the callsite, which meant the caller built a renderer out of a dictionary
// it had no other reason to hold. Returns the driver's own row count / status.
BACKEND_API int ibExecuteDdl(ibDatabaseLayer* layer, const ibDdlStatement& ddl);

// DDL DURABILITY — does this engine keep DDL in the transaction, so a statement against a table
// whose SHAPE this transaction just changed has to wait for the commit? (FB / PG yes, SQLite no.)
// The barrier the schema builder runs on.
BACKEND_API bool ibDdlCommitsBeforeData(const ibDatabaseLayer* layer);

// Can several column clauses ride ONE `ALTER TABLE`, or does each need a statement of its own?
BACKEND_API bool ibAlterTableMultiClause(const ibDatabaseLayer* layer);

// ==========================================================================
// ibQueryResult — RAII cursor. Move-only. Holds a shared_ptr to its
// connection for its whole lifetime (§9).
// ==========================================================================
class BACKEND_API ibQueryResult
{
public:
	ibQueryResult(std::shared_ptr<ibDatabaseLayer> conn,
	              ibPreparedStatement* stmt,
	              ibDatabaseResultSet* rs);
	~ibQueryResult();

	ibQueryResult(ibQueryResult&& other) noexcept;
	ibQueryResult& operator=(ibQueryResult&& other) noexcept;
	ibQueryResult(const ibQueryResult&)            = delete;
	ibQueryResult& operator=(const ibQueryResult&) = delete;

	// Advance to the next row; false past the last row.
	bool Next();

	// Read the current row's column as a normalized ibValue (1-based index, or
	// by name). A NULL column yields a TYPE_NULL value.
	ibValue GetValue(int column);
	ibValue GetValue(const wxString& name);

	// Typed field reads by name — the dialect-NORMALISED form of the row's physical fields
	// (the "same shape as L1, minus the dialect"). The provider's value-assembly reads the
	// physical _N/_S/_RRRef columns through THESE, so it never touches the raw L1 result set — which
	// is no longer reachable at all (see below). Delegate to the borrowed driver cursor.
	wxString    GetResultString(const wxString& name);
	int         GetResultInt(const wxString& name);
	long long   GetResultLong(const wxString& name);
	bool        GetResultBool(const wxString& name);
	wxDateTime  GetResultDate(const wxString& name);
	double      GetResultDouble(const wxString& name);
	ibNumber    GetResultNumber(const wxString& name);
	void*       GetResultBlob(const wxString& name, wxMemoryBuffer& buffer);

	// ⭐⭐ IS THIS FIELD NULL — the one question the typed reads above cannot answer.
	//
	// Each of them returns a VALUE, and every type has one that means "nothing was there": zero, an
	// empty string, an invalid date. A stored column never needs this — its `_TYPE` tag says what it
	// holds, NULL included. A field projected UNDER AN ALIAS has no tag beside it: a computed output
	// and an aggregate are one field and nothing else, so an empty fold (`MIN(x)` over a group that
	// matched nothing) comes back as SQL NULL with no way to say so.
	//
	// 🛑 Asked as a date, that NULL is an INVALID wxDateTime, and assigning one into an ibValue trips
	// its assertion and stops the program — which is how a self-join's `MIN(Period)`, the moment it
	// began folding on the server, came back as a debug alert instead of a blank cell (2026-09-06).
	// The RAM fold answers the same case with a real NULL (ibAggAcc::Result); this is what lets the
	// server road say it too, instead of guessing from a value that has no way to be absent.
	bool        IsResultNull(const wxString& name);

	int      ColumnCount();
	wxString ColumnName(int column);

	// (RawResultSet REMOVED 2026-08-13 — the Phase-1 bridge that handed out the borrowed driver
	//  cursor for callers still materialising rows by hand. It had ONE left, the bytecode cache
	//  reading its blob by field number, and it now reads it by name through GetResultBlob above.
	//  A hatch with one user is a hatch: the typed accessors are the surface, and a caller that
	//  needs something they do not have should grow one here rather than reach past them.)

private:
	void                 Release();
	ibResultSetMetaData* Meta();

	std::shared_ptr<ibDatabaseLayer> m_conn;   // keeps the connection pinned while the cursor lives
	ibPreparedStatement* m_stmt = nullptr;
	ibDatabaseResultSet* m_rs   = nullptr;
	ibResultSetMetaData* m_meta = nullptr;      // lazy, owned by m_rs
};

// ==========================================================================
// ibDatabaseQueryBuilder — the single L2-1 door. Move-only.
//
//     ibDatabaseQueryBuilder(holder)
//         .From("Reference17")
//         .Where(ibBinOp(ibQueryBinOp::Eq, ibCol("Code_S"), ibParam(0)))
//         .OrderBy("Code_S").Limit(50)
//         .Execute({ codeValue });           // -> ibQueryResult
//
// It borrows a holder (the current session holder by default), NEVER owns the
// connection/transaction. ibDatabaseQueryBuilder + ibQueryResult are the RAII
// door that makes "forgot to close" impossible (§12).
// ==========================================================================
class BACKEND_API ibDatabaseQueryBuilder
{
public:
	ibDatabaseQueryBuilder();                                            // current session holder (or build-only)
	explicit ibDatabaseQueryBuilder(ibDatabaseConnectionHolder* holder); // explicit holder

	ibDatabaseQueryBuilder(ibDatabaseQueryBuilder&&) noexcept            = default;
	ibDatabaseQueryBuilder& operator=(ibDatabaseQueryBuilder&&) noexcept = default;
	ibDatabaseQueryBuilder(const ibDatabaseQueryBuilder&)                = delete;
	ibDatabaseQueryBuilder& operator=(const ibDatabaseQueryBuilder&)     = delete;

	// --- fluent DQL construction (chainable, returns *this) ---------------
	// Physical names only — the metadata->physical mapping is a Level 3 job.
	ibDatabaseQueryBuilder& From(const wxString& table);
	// FROM a pre-built source tree (Scan / Join / Subquery) — the flexible form that
	// lets the metadata layer hand down an arbitrary join/derived-table source. Wins
	// over From(table) when both are set.
	ibDatabaseQueryBuilder& From(ibQueryRelPtr source);
	// Chain a JOIN onto the current FROM source. The current source (From(table) scan,
	// or a prior Join) becomes the LEFT; `table` / `right` is the RIGHT; `on` the ON
	// predicate (qualify its columns with ibCol(table, name) to disambiguate). Several
	// Join() nest left-deep. The ibQueryRelPtr overload joins a subquery / slice source.
	ibDatabaseQueryBuilder& Join(const wxString& table, ibQueryExprPtr on,
	                             ibQueryJoinType type = ibQueryJoinType::Inner);
	ibDatabaseQueryBuilder& Join(ibQueryRelPtr right, ibQueryExprPtr on,
	                             ibQueryJoinType type = ibQueryJoinType::Inner);
	ibDatabaseQueryBuilder& Select(std::vector<wxString> columns);   // empty / unset = SELECT *
	ibDatabaseQueryBuilder& Where(ibQueryExprPtr predicate);         // AND-folded with prior Where()
	ibDatabaseQueryBuilder& OrderBy(const wxString& column, ibQuerySortDir dir = ibQuerySortDir::Asc);
	ibDatabaseQueryBuilder& AddSortKey(ibQuerySortKey key);   // prebuilt key (composite / metadata-derived)
	ibDatabaseQueryBuilder& Limit(long count, long offset = 0);

	// --- aggregation (GROUP BY + explicit projection + HAVING) ------------
	// When any GroupBy() key is set, Build() emits an Aggregate node: the projection
	// (group columns + aggregate exprs like ibFunc("SUM",{...}) AS alias) becomes the
	// SELECT list, HAVING filters post-aggregation. Without group keys the projection
	// is unused (Select(columns) drives the column list). L3 lowers metadata grouping
	// (GroupBy(attr) + Sum(attr)) to these physical verbs.
	ibDatabaseQueryBuilder& Project(std::vector<ibQueryProjItem> items);   // explicit SELECT list
	ibDatabaseQueryBuilder& GroupBy(ibQueryExprPtr key);                   // add a GROUP BY key
	ibDatabaseQueryBuilder& Having(ibQueryExprPtr predicate);             // post-aggregation filter

	// --- terminals --------------------------------------------------------
	// Build the IR from the fluent state. Pure: no connection, no dialect.
	ibQueryIR Build() const;

	// Build + render through the connected driver's dialect + run.
	[[nodiscard]] ibQueryResult Execute(const std::vector<ibValue>& externalParams = {});

	// --- direct paths (prebuilt IR / DDL / DML) ---------------------------
	[[nodiscard]] ibQueryResult ExecuteIR(const ibQueryIR& ir,
	                                       const std::vector<ibValue>& externalParams = {});
	int Execute(const ibDdlStatement& ddl);
	int Execute(const ibDmlStatement& dml, const std::vector<ibValue>& externalParams = {});

	// A write that hands back what it wrote — the statement must carry a RETURNING list
	// (ibReturning), and it yields a cursor over the affected rows, exactly like a SELECT.
	// Empty cursor = the write matched nothing, which is how a caller distinguishes
	// "updated it" from "there was no such row" WITHOUT a second round trip.
	[[nodiscard]] ibQueryResult ExecuteReturning(const ibDmlStatement& dml,
	                                              const std::vector<ibValue>& externalParams = {});

	// --- render-once / execute-many (build-once perf path, §19/§20) -------
	// Render an IR to dialect SQL + bind plan WITHOUT running. Borrows the
	// connection only for its dialect.
	ibRenderedQuery Render(const ibQueryIR& ir);
	// Run a previously-rendered query.
	[[nodiscard]] ibQueryResult ExecuteRendered(const ibRenderedQuery& rendered,
	                                             const std::vector<ibValue>& externalParams = {});

	// --- transaction verbs ------------------------------------------------
	void BeginTransaction(const ibDbTxOptions& opts = ibDbTxOptions()) { m_scope.SafeBeginTransaction(opts); }
	void Commit()   { m_scope.SafeCommitTransaction(); }
	void RollBack() { m_scope.SafeRollBackTransaction(); }

	// --- connection / schema introspection --------------------------------
	// Predicates that delegate to the borrowed connection, so a DAO can stay on this one L2-1 door
	// for its whole job — gate a CREATE on TableExists, read a live column set, check open /
	// in-transaction state — without reaching for the raw ibDatabaseLayer. A scope that holds no
	// connection reports false / empty (these never throw — they are pre-flight checks).
	bool          TableExists(const wxString& table);
	wxArrayString GetColumns(const wxString& table);
	bool          IsOpen();
	bool          IsActiveTransaction();

private:
	// fluent state — assembled into an ibQueryIR by Build()
	wxString                    m_table;
	ibQueryRelPtr               m_source;       // pre-built FROM tree (Join/Subquery); null = ibScan(m_table)
	std::vector<wxString>       m_select;
	std::vector<ibQueryExprPtr> m_predicates;   // AND-folded
	std::vector<ibQuerySortKey> m_sortKeys;
	bool                        m_hasLimit   = false;
	long                        m_limitCount = -1;
	long                        m_limitOffset = 0;

	std::vector<ibQueryProjItem> m_projection;   // .Project() — explicit SELECT list (aggregation)
	std::vector<ibQueryExprPtr>  m_groupKeys;    // .GroupBy()
	ibQueryExprPtr               m_having;        // .Having()

	ibConnectionScope m_scope;
};

// ==========================================================================
// ibQueryStatement — an ibPreparedStatement that executes NOTHING at the driver
// when you bind. Each SetParam* is captured as an L2-1 value node; the object is a
// deferred-execution TEMPLATE — a structured L2-1 DML over physical columns.
// RunQuery() renders it through the connected dialect and runs it ONCE.
//
// The statement-level migration seam: a writer that used to do
//   PrepareStatement(text); SetParam*(...); RunQuery();
// keeps its binding code UNCHANGED — only the prepare switches from raw SQL
// text to a structured (kind, table, columns, matchKeys) form. Values flow in
// POSITIONALLY: SetParam(pos, v) fills column[pos-1]. A blob rides as an opaque
// ibConstBlob (metadata-blind); every other type as a bound ibConst.
// ==========================================================================
class BACKEND_API ibQueryStatement : public ibPreparedStatement
{
public:
	enum class Kind { Insert, Upsert, Delete, Update };

	// `columns` are the physical column names in bind order. For Insert/Upsert
	// they are the assignment columns; for Delete they are the WHERE-equality
	// columns (DELETE ... WHERE col0 = ? AND col1 = ? ...). `matchKeys` are the
	// PK columns for Upsert (ignored otherwise). `holder` null = session default.
	ibQueryStatement(Kind kind, const wxString& table, std::vector<wxString> columns,
	                 std::vector<wxString> matchKeys = {},
	                 ibDatabaseConnectionHolder* holder = nullptr);
	~ibQueryStatement() override = default;

	// Keep the base's non-pure overloads visible alongside our overrides.
	using ibPreparedStatement::SetParamDate;
	using ibPreparedStatement::SetParamBlob;

	// --- ibPreparedStatement: bind -> capture as an L2-1 value node ----------
	void Close() override {}
	void SetParamInt(int nPosition, int nValue) override;
	void SetParamDouble(int nPosition, double dblValue) override;
	void SetParamNumber(int nPosition, const ibNumber& numValue) override;
	void SetParamString(int nPosition, const wxString& strValue) override;
	void SetParamNull(int nPosition) override;
	void SetParamBlob(int nPosition, const void* pData, long nDataLength) override;
	void SetParamDate(int nPosition, const wxDateTime& dateValue) override;
	void SetParamBool(int nPosition, bool bValue) override;
	int  GetParameterCount() override { return static_cast<int>(m_columns.size()); }

	// --- terminal: render the template through the dialect + run once ------
	int RunQuery() override;
	ibDatabaseResultSet* RunQueryWithResults() override;   // runs; writes have no cursor -> null

	// Captured param nodes, in column order (one per column; null if unbound).
	// Lets a CAPTURE-ONLY statement decompose a value into per-field constants
	// (run SetParam* / SetValueAttribute, never RunQuery) — the metadata layer
	// reuses the write decomposition to build a composite-key read predicate.
	const std::vector<ibQueryExprPtr>& CapturedValues() const { return m_values; }

	// Update-only: an extra WHERE predicate AND-folded with the match-key equality — the
	// RLS-restricted save uses it to fold the access predicate into the UPDATE. Ignored by other kinds.
	void SetWherePredicate(ibQueryExprPtr where) { m_wherePredicate = std::move(where); }

	// ACCUMULATING bind: the column's new value is its CURRENT value plus `delta`, computed by the
	// DB — `col = col + ?`. Update only (an insert has no current value to add to).
	//
	// It is a sibling of the SetParam* family, not an escape hatch: a statement's values are already
	// expression nodes, so this simply binds a different node in the same slot. The column is taken
	// from the bind position, so the caller names nothing the statement does not already know.
	//
	// Why it exists as its own verb: a read-modify-write on the client silently discards whatever
	// landed between the read and the write, while an in-statement addition composes with it. That is
	// what lets a maintenance pass move figures around a table people are writing to. Pass a negative
	// delta to subtract.
	void SetParamAccumulate(int nPosition, const ibNumber& delta);

private:
	void           Put(int position, ibQueryExprPtr expr);   // 1-based -> m_values[pos-1]
	ibDmlStatement BuildDml() const;

	Kind                        m_kind;
	wxString                    m_table;
	std::vector<wxString>       m_columns;
	std::vector<wxString>       m_matchKeys;
	std::vector<ibQueryExprPtr> m_values;     // one expr per column, in column order
	ibDatabaseConnectionHolder* m_holder;
	ibQueryExprPtr              m_wherePredicate;   // Update only: extra WHERE (RLS), AND-folded with the key match
};

#endif  // __IB_DATABASE_QUERY_BUILDER_H__
