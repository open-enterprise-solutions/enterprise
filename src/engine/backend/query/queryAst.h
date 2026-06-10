#ifndef __QUERY_AST_H__
#define __QUERY_AST_H__

// L4-1 — text query language AST.
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

#include <vector>
#include <memory>

struct ibQueryAstExpr;
using ibQueryAstExprPtr = std::shared_ptr<ibQueryAstExpr>;

struct ibQuerySelect;   // an Expr may hold a nested SELECT (IN (subquery)); defined in full below

// What an expression node IS.
enum class ibQueryAstExprKind
{
	Column,    // dotted path a.b.c  (m_path)
	Literal,   // ibValue constant   (m_literal)
	Param,     // &Name              (m_paramName)
	Func,      // SUM/MIN/MAX/AVG(arg) | COUNT(*|arg)   (m_func, m_arg / m_star)
	Arith,     // lhs <+ - * / %> rhs   (m_arith)
	Case,      // CASE WHEN p THEN e … [ELSE e] END   (m_cases, m_else)
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
	ibQueryAstExprPtr        m_arg;                          // Func argument

	ibQueryCompareOp      m_cmp = ibQueryCompareOp::Eq;   // Compare
	ibQueryArithOp        m_arith = ibQueryArithOp::Add;  // Arith
	bool                  m_negated = false;              // Like/In/IsNull/Between negation
	bool                  m_isOr = false;                 // Logical: true=OR, false=AND

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
};

enum class ibQueryJoinKindAst { Inner, Left, Right, Full };

struct ibQueryAstJoin
{
	ibQuerySource      m_source;
	ibQueryJoinKindAst m_kind = ibQueryJoinKindAst::Inner;
	ibQueryAstExprPtr     m_on;     // ON predicate (null = auto-join by reference)
};

struct ibQueryOrderItem
{
	ibQueryAstExprPtr m_expr;        // column path (or a reference to a projection alias)
	bool           m_ascending = true;
};

// How a TOTALS-BY dimension unfolds (mirrors the L3 door's ibDimensionKind, kept
// here so the AST stays L3-free; the lowering maps it across).
enum class ibQueryDimUnfold { Elements, Hierarchy, HierarchyOnly };

// One TOTALS-BY level: the dimension column + how it unfolds. Levels apply IN ORDER
// (each yields a subtotal node; the root is the grand total). -> door TotalBy(col, dim).
struct ibQueryTotalDim
{
	ibQueryAstExprPtr   m_expr;                                  // dimension column path
	ibQueryDimUnfold m_unfold = ibQueryDimUnfold::Elements;
};

// The whole SELECT statement.
struct ibQuerySelect
{
	bool                           m_distinct = false;
	bool                           m_selectAll = false;     // SELECT *
	std::vector<ibQueryProjection> m_projections;           // empty + m_selectAll => SELECT *
	ibQuerySource                  m_from;
	std::vector<ibQueryAstJoin>       m_joins;
	ibQueryAstExprPtr                 m_where;                 // null = no filter
	std::vector<ibQueryAstExprPtr>    m_groupBy;               // column-path expressions (flat GROUP BY)
	ibQueryAstExprPtr                 m_having;                // null = none
	std::vector<ibQueryOrderItem>  m_orderBy;

	// TOTALS — hierarchical subtotals (1С-style "ИТОГИ … ПО …"). When m_hasTotals,
	// the result is a TREE (door SelectTotals): m_totalsAggregates roll IN-PLACE at
	// every level, m_totalsBy are the dimension levels in order. Distinct from the
	// flat GROUP BY above. (docs/query-language-arc.md §22.1b)
	bool                           m_hasTotals = false;
	std::vector<ibQueryAstExprPtr>    m_totalsAggregates;      // SUM(x), MAX(y), … (aggregate Func nodes)
	std::vector<ibQueryTotalDim>   m_totalsBy;              // dimension levels (in order)

	// UNION — additional SELECT branches stacked vertically (same projection). The first branch is THIS
	// select (its core); each entry here is a further branch. ORDER BY / TOTALS on this select apply to
	// the whole union. (docs §23 — UNION; the composer already stacks leaves.)
	std::vector<std::shared_ptr<ibQuerySelect>> m_unions;
};

using ibQuerySelectPtr = std::shared_ptr<ibQuerySelect>;

#endif
