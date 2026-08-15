#ifndef __QUERY_AST_H__
#define __QUERY_AST_H__

// L4-1 — text query language AST. (Its `ibQueryAstExpr` expression sub-tree is SHARED with
// L4-2: the LINQ lambda recorder builds the SAME expr nodes — compiler/lambdaQueryAst — and
// lowers them through the same builders. `ibQuerySelect` below is L4-1-only.)
//
// A POD tree, L2/L3-FREE (it names ibValue + wxString only, like the L2 IR names
// no metadata). The parser (queryParser.{h,cpp}) builds it; names are NOT yet
// resolved against metadata — resolution happens in lowering (queryLowering.{h,cpp})
// at Execute time, when &parameter values are known. So one parse → many executes
// (the parse is AOT-cacheable on the Query value object).
//
// Expressions are modelled richly (the parser accepts a full SQL-ish predicate);
// the lowering realizes the subset the L3 door supports and throws a clear
// "unsupported" otherwise — keeping the surface honest without bloating the door.
//
// See docs/query-language-arc.md §14 / §23.

#include "backend/compiler/value.h"   // ibValue
#include "queryKeywords.h"            // ibQueryKeyword (aggregate func tag)
#include "queryUnfold.h"              // ibQueryDimUnfold — the three unfold words, shared with L3

#include <vector>
#include <memory>

struct ibQueryAstExpr;
using ibQueryAstExprPtr = std::shared_ptr<ibQueryAstExpr>;

struct ibQuerySelect;   // an Expr may hold a nested SELECT (IN (subquery)); defined in full below

// (ibQueryDimUnfold — the three unfold words — moved to queryUnfold.h, included above: an L3
//  condition may now carry the word too, and this header stays L2/L3-free.)

// What an expression node IS.
enum class ibQueryAstExprKind
{
	Column,    // dotted path a.b.c  (m_path)
	Literal,   // ibValue constant   (m_literal)
	Param,     // &Name              (m_paramName)
	Value,     // value(<Kind>.<Name>.<Member>) — a literal reference / enum / predefined constant, resolved AT
	           // LOWERING against the config (m_path = the dotted meta-path). Behaves like a Param once resolved:
	           // EvalValue turns it into an ibValue that flows as a bound value. (Enum members land here too.)
	Func,      // SUM/MIN/MAX/AVG(arg) | COUNT(*|arg)   (m_func, m_arg / m_star)
	Arith,     // lhs <+ - * / %> rhs   (m_arith)
	Case,      // CASE WHEN p THEN e … [ELSE e] END   (m_cases, m_else)
	// ⭐ CAST(<expr> AS <Kind>.<Name>) — the expression in m_arg, the TARGET TYPE in m_path.
	//
	// It exists for one job, and it is not conversion: a COMPOSITE reference (a register's recorder,
	// a characteristic's value) names several types, so the engine refuses to walk THROUGH it —
	// there is no one set of fields behind it. Naming which type is meant makes the walk possible:
	//
	//     CAST(Recorder AS Document.Order).Number
	//
	// ⚠ A WALK AFTER A CAST IS A COLUMN, not a Cast: `CAST(x AS T).A.B` parses as a Column whose
	// m_path is {A, B} and whose m_arg is the Cast. That is deliberate — the resolver already walks
	// a Column's path from a starting queryable, and the cast only says WHICH queryable to start on.
	// One shape, and every dot-walk mechanism downstream (SelectPath, ExpandDotWalkJoins, the RAM
	// join) works on it unchanged.
	Cast,
	Compare,   // lhs <cmp> rhs      (m_cmp)
	Like,      // lhs [NOT] LIKE rhs (m_negated)
	In,        // lhs [NOT] IN (list | subquery)  (m_negated, m_list / m_subquery)
	IsNull,    // lhs IS [NOT] NULL  (m_negated)
	Between,   // lhs [NOT] BETWEEN low AND high
	Logical,   // lhs (AND|OR) rhs   (m_isOr)
	Not,       // NOT lhs
};

// Comparison operator of a Compare node.
enum class ibQueryCompareOp { Eq, Ne, Lt, Le, Gt, Ge };

// Arithmetic operator of an Arith node (parsed with standard precedence; the column-based L3 door does
// not execute computed expressions yet, so the lowering rejects these — the parser stays complete).
enum class ibQueryArithOp { Add, Sub, Mul, Div, Mod };

// One expression node. A flat tagged struct (only the fields its kind needs are
// populated) — simpler than a class hierarchy for a small AST the lowering walks once.
struct ibQueryAstExpr
{
	ibQueryAstExprKind m_kind = ibQueryAstExprKind::Literal;

	std::vector<wxString> m_path;        // Column: dotted segments
	ibValue               m_literal;     // Literal
	wxString              m_paramName;   // Param

	ibQueryKeyword        m_func = ibQueryKeyword::None;  // Func: Sum/Count/Min/Max/Avg
	bool                  m_star = false;                 // Func: COUNT(*)
	// Func: fold over DISTINCT values of the argument — `COUNT(DISTINCT Board)` asks how many
	// different boards, not how many rows have one.
	//
	// ⚠ NOT the SELECT's own DISTINCT (ibQuerySelect::m_distinct). That one asks whether two whole
	// ROWS are the same; this asks whether two VALUES of one column are — different questions, and
	// a query may perfectly well want one, the other, or both.
	bool                  m_distinctArg = false;
	ibQueryAstExprPtr        m_arg;                          // Func argument

	ibQueryCompareOp      m_cmp = ibQueryCompareOp::Eq;   // Compare
	ibQueryArithOp        m_arith = ibQueryArithOp::Add;  // Arith
	bool                  m_negated = false;              // Like/In/IsNull/Between negation
	bool                  m_isOr = false;                 // Logical: true=OR, false=AND

	// In: HOW FAR DOWN the operand reaches — the same three words a TOTALS-BY dimension unfolds by,
	// here as a modifier of the set-valued operator (`Account IN HIERARCHY (&Accounts)`). Elements —
	// which is a plain IN — for every other node kind and for every IN written without a word.
	//
	// ⚠ Nothing below L4 sees it: the lowering RESOLVES the subtree into the values it stands for and
	// emits the ordinary IN, so the door and all four drivers keep the one set-valued operator they
	// already render. The word lives where the question is asked, not where the rows are read.
	ibQueryDimUnfold      m_unfold = ibQueryDimUnfold::Elements;

	ibQueryAstExprPtr        m_lhs, m_rhs;                   // Compare/Like/Logical/Not/Arith
	ibQueryAstExprPtr        m_low, m_high;                  // Between
	std::vector<ibQueryAstExprPtr> m_list;                  // In list (value list form)
	std::shared_ptr<ibQuerySelect> m_subquery;           // In (subquery form): lhs IN (SELECT …)

	// CASE: searched WHEN -> THEN pairs (in order) + optional ELSE.
	std::vector<std::pair<ibQueryAstExprPtr, ibQueryAstExprPtr>> m_cases;
	ibQueryAstExprPtr        m_else;

	unsigned int          m_line = 0, m_col = 0;          // source span (diagnostics)

	static ibQueryAstExprPtr Make(ibQueryAstExprKind kind) {
		auto e = std::make_shared<ibQueryAstExpr>();
		e->m_kind = kind;
		return e;
	}
};

// One SELECT output column: an expression (column path or aggregate) + an optional
// alias. SELECT * sets m_star (whole-row, no expr).
struct ibQueryProjection
{
	ibQueryAstExprPtr m_expr;    // column path or aggregate func (null when m_star)
	wxString       m_alias;   // AS alias (empty => derived in lowering)
	bool           m_star = false;
};

// A FROM / JOIN source: EITHER a dotted metaobject path (m_name) OR a nested SELECT
// subquery (m_subquery, set => m_name is empty). "Catalog.Products" -> {Catalog, Products};
// "AccumulationRegister.Goods.Balance" -> {…, Goods, Balance} (the trailing segment may name a
// virtual table). A subquery source: FROM (SELECT …) AS s — lowered via ibSubqueryQueryable.
struct ibQuerySelect;
struct ibQuerySource
{
	std::vector<wxString>            m_name;       // dotted metaobject path (empty when m_subquery set)
	std::shared_ptr<ibQuerySelect>  m_subquery;   // nested SELECT (set => this source is a subquery)
	std::vector<ibQueryAstExprPtr>     m_args;        // source-call args: Balance(&Period, &Filter) — value exprs (param / literal)
	wxString                        m_alias;

	// ⭐ A TABLE HANDED IN FROM OUTSIDE: `FROM &Goods`, where `Goods` is a bound parameter holding a
	// value table. m_name still carries the name (one segment), and THIS says where it comes from.
	//
	// Why a sigil rather than "look it up and see": `&` already means "this came from outside" in
	// every other position in this language, so it means the same thing here — one mark, one
	// meaning. Without it a bare `FROM Goods` could be a temp table the package made, a bound table,
	// or a metaobject, and WHICH ONE would be decided by the order the resolver happens to look. A
	// query has to read as what it is.
	bool                            m_parameter = false;
};

enum class ibQueryJoinKindAst { Inner, Left, Right, Full };

struct ibQueryAstJoin
{
	ibQuerySource      m_source;
	ibQueryJoinKindAst m_kind = ibQueryJoinKindAst::Inner;
	// ON predicate. NULL = no link — the two tables are multiplied (a cross join), which is what
	// "two tables and nothing said about how they meet" means. The one exception is a named
	// reference-path source (`JOIN root.ref AS x`), which carries the relation in its own name.
	ibQueryAstExprPtr     m_on;
};

struct ibQueryOrderItem
{
	ibQueryAstExprPtr m_expr;        // column path (or a reference to a projection alias)
	bool           m_ascending = true;
};

// (ibQueryDimUnfold — the three unfold words — is declared above the expression node, which now
//  needs it too: the same words are a FILTER modifier as well as a grouping one.)

// One TOTALS-BY level: the dimension column + how it unfolds. Levels apply IN ORDER
// (each yields a subtotal node; the root is the grand total). -> door TotalBy(col, dim).
struct ibQueryTotalDim
{
	ibQueryAstExprPtr   m_expr;                                  // dimension column path
	ibQueryDimUnfold m_unfold = ibQueryDimUnfold::Elements;
	// The name this LEVEL is read back under. A totals tree is walked by level and each level's
	// value is asked for by name, so a dimension needs one of its own — without it two levels over
	// the same column (Date by month, Date by day) answer to the same name and one of them wins.
	// Empty = the column's own name, which is right for the ordinary single-level case.
	wxString         m_alias;
};

// The whole SELECT statement.
struct ibQuerySelect
{
	bool                           m_distinct = false;
	bool                           m_selectAll = false;     // SELECT *
	// SELECT ALLOWED — "give me what I may see, do not refuse me". Where the access policy says NO
	// outright, the read yields nothing for that source instead of raising ibBackendAccessException.
	// DELIBERATELY not the default: a refusal is normally TOLD, never mimed as an empty selection, and
	// only the author of a list / a choice form / a composite-type report knows the quiet form is honest
	// here. (docs/query-constructor.md §5 — ALLOWED.)
	bool                           m_allowed = false;
	// FOR UPDATE — this SELECT does not merely read, it HOLDS the rows it returned until the transaction
	// ends. Rendered by the driver dialect (m_rowLockSuffix: FOR UPDATE / WITH LOCK); reaches it as
	// ibReadPageRequest::m_lockForUpdate. The read-then-write pair inside one transaction is what it buys.
	bool                           m_forUpdate = false;
	// INTO <name> — MATERIALISE this result under a name instead of returning it. The statement then
	// yields a ROW COUNT (there is no table to hand back — it went into the temp), and later statements
	// of the same package select FROM that name. Empty = an ordinary select. (docs §5 — query batch.)
	wxString                       m_intoTemp;
	// INDEX BY … — the columns the temp table this statement makes is indexed by, so the statements
	// that read it find rows instead of scanning for them. Meaningless without INTO (a table nobody
	// keeps cannot be looked things up in), and the parser says so rather than accepting it quietly.
	std::vector<ibQueryAstExprPtr> m_indexBy;
	long                           m_top = 0;               // SELECT TOP n — row limit on THIS core (0 = none).
	                                                        // On the first core of a UNION it limits the WHOLE
	                                                        // union (like the trailing ORDER BY); on a later
	                                                        // branch / a subquery it limits that branch.
	bool                           m_unionAll = false;      // set on a BRANCH select: attached with UNION ALL
	                                                        // (keep duplicates); plain UNION dedupes the
	                                                        // accumulated rows at that operator (SQL semantics)
	std::vector<ibQueryProjection> m_projections;           // empty + m_selectAll => SELECT *
	ibQuerySource                  m_from;
	std::vector<ibQueryAstJoin>       m_joins;
	ibQueryAstExprPtr                 m_where;                 // null = no filter
	std::vector<ibQueryAstExprPtr>    m_groupBy;               // column-path expressions (flat GROUP BY)
	ibQueryAstExprPtr                 m_having;                // null = none
	std::vector<ibQueryOrderItem>  m_orderBy;

	// TOTALS — hierarchical subtotals (report-style "TOTALS … BY …"). When m_hasTotals,
	// the result is a TREE (door SelectTotals): m_totalsAggregates roll IN-PLACE at
	// every level, m_totalsBy are the dimension levels in order. Distinct from the
	// flat GROUP BY above. (docs/query-language-arc.md §22.1b)
	bool                           m_hasTotals = false;
	std::vector<ibQueryAstExprPtr>    m_totalsAggregates;      // SUM(x), MAX(y), … (aggregate Func nodes)
	std::vector<ibQueryTotalDim>   m_totalsBy;              // dimension levels (in order)
	// OVERALL — the level above every dimension: ONE row folding the whole result.
	//
	// ⚠ A FLAG, NOT A LEVEL IN THE LIST ABOVE, and deliberately. A dimension level is a column and a
	// place in an order; the overall is neither — it has no column, and its position is fixed (there
	// is nothing above "everything"). Putting it in the list would add a member whose expression is
	// absent and whose order carries no meaning, and every walk of the dimensions would have to step
	// around it.
	//
	// It asks the runtime for nothing new: the fold already computes the whole-snapshot aggregates
	// into the tree's ROOT (ibQueryComposer::BuildDimensionTree — "grand total in-place"). What this
	// says is whether that root is WALKED as a row, instead of only being reachable by GetTotal().
	bool                           m_totalsOverall = false;

	// UNION — additional SELECT branches stacked vertically (same projection). The first branch is THIS
	// select (its core); each entry here is a further branch. ORDER BY / TOTALS on this select apply to
	// the whole union. (docs §23 — UNION; the composer already stacks leaves.)
	std::vector<std::shared_ptr<ibQuerySelect>> m_unions;
};

using ibQuerySelectPtr = std::shared_ptr<ibQuerySelect>;

// ==========================================================================
// A PACKAGE — several statements written together, sent in ONE trip and answered by an ARRAY of
// results addressed by position (docs/query-constructor.md §5 — query batch). Three properties make
// it more than a transport saving:
//
//   * statements run IN WRITTEN ORDER;
//   * each sees what the previous ones left — in practice a temp table (statement 2 selects INTO
//     `Sales`, statement 3 selects FROM `Sales`);
//   * the returned array is HETEROGENEOUS by position: a plain select yields its table, a
//     select-into-temp yields the row count.
//
// A statement is EITHER a select (possibly INTO a temp table) OR a drop of one. The drop exists
// because a package's temp tables live for the WHOLE package: releasing early inside a long package
// has to be sayable out loud rather than left to scope.
// ==========================================================================
struct ibQueryAstStatement
{
	ibQuerySelectPtr m_select;     // the SELECT (null on a drop statement)
	wxString         m_dropTemp;   // DROP <name> — release a temp table early (empty on a select)

	bool IsDrop() const { return !m_dropTemp.IsEmpty(); }
};

struct ibQueryPackage
{
	std::vector<ibQueryAstStatement> m_statements;

	// A package of one plain select is what an ordinary query IS — so a caller that
	// only ever wanted a single statement asks for it here rather than indexing.
	ibQuerySelectPtr SingleSelect() const {
		return m_statements.size() == 1 ? m_statements.front().m_select : nullptr;
	}
};

#endif
