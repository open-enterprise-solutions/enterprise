// L4-1 text query language — parser tests (queryParser.{h,cpp}).
//
// Golden "text -> AST": pure parse, no metadata, no database. Confirms the
// recursive-descent grammar (SELECT list / FROM / JOIN / WHERE precedence /
// GROUP BY / HAVING / ORDER BY / aggregates / &params / LIKE-IN-BETWEEN-IS NULL).

#include <gtest/gtest.h>

#include <functional>
#include <vector>

#include "backend/query/queryParser.h"

namespace {

ibQuerySelectPtr Parse(const wxString& text)
{
	ibQueryParser parser;
	return parser.Parse(text);
}

} // namespace

TEST(QueryL4Parser, SimpleSelectList)
{
	auto sel = Parse(wxT("SELECT Code, Name FROM Catalog.Products"));
	ASSERT_TRUE(sel != nullptr);
	EXPECT_FALSE(sel->m_selectAll);
	ASSERT_EQ(sel->m_projections.size(), 2u);
	ASSERT_EQ(sel->m_from.m_name.size(), 2u);
	EXPECT_EQ(sel->m_from.m_name[0], wxT("Catalog"));
	EXPECT_EQ(sel->m_from.m_name[1], wxT("Products"));
	EXPECT_EQ(sel->m_projections[0].m_expr->m_kind, ibQueryAstExprKind::Column);
	EXPECT_EQ(sel->m_projections[0].m_expr->m_path[0], wxT("Code"));
	EXPECT_EQ(sel->m_where, nullptr);
}

TEST(QueryL4Parser, SelectStar)
{
	auto sel = Parse(wxT("SELECT * FROM Document.Orders"));
	EXPECT_TRUE(sel->m_selectAll);
	EXPECT_TRUE(sel->m_projections.empty());
}

TEST(QueryL4Parser, WherePrecedence_AndComparesWithParam)
{
	auto sel = Parse(wxT("SELECT * FROM Catalog.Products WHERE Code = &C AND Price >= 100"));
	ASSERT_TRUE(sel->m_where != nullptr);
	// top node is AND
	EXPECT_EQ(sel->m_where->m_kind, ibQueryAstExprKind::Logical);
	EXPECT_FALSE(sel->m_where->m_isOr);

	const ibQueryAstExprPtr& left  = sel->m_where->m_lhs;   // Code = &C
	const ibQueryAstExprPtr& right = sel->m_where->m_rhs;   // Price >= 100
	ASSERT_EQ(left->m_kind, ibQueryAstExprKind::Compare);
	EXPECT_EQ(left->m_cmp, ibQueryCompareOp::Eq);
	EXPECT_EQ(left->m_lhs->m_kind, ibQueryAstExprKind::Column);
	EXPECT_EQ(left->m_lhs->m_path[0], wxT("Code"));
	EXPECT_EQ(left->m_rhs->m_kind, ibQueryAstExprKind::Param);
	EXPECT_EQ(left->m_rhs->m_paramName, wxT("C"));

	ASSERT_EQ(right->m_kind, ibQueryAstExprKind::Compare);
	EXPECT_EQ(right->m_cmp, ibQueryCompareOp::Ge);
	EXPECT_EQ(right->m_rhs->m_kind, ibQueryAstExprKind::Literal);
}

TEST(QueryL4Parser, OrBindsLooserThanAnd)
{
	// a AND b OR c  ==  (a AND b) OR c
	auto sel = Parse(wxT("SELECT * FROM Catalog.X WHERE A = 1 AND B = 2 OR C = 3"));
	ASSERT_TRUE(sel->m_where != nullptr);
	EXPECT_EQ(sel->m_where->m_kind, ibQueryAstExprKind::Logical);
	EXPECT_TRUE(sel->m_where->m_isOr);                              // OR at the root
	EXPECT_EQ(sel->m_where->m_lhs->m_kind, ibQueryAstExprKind::Logical);
	EXPECT_FALSE(sel->m_where->m_lhs->m_isOr);                      // AND on the left
	EXPECT_EQ(sel->m_where->m_rhs->m_kind, ibQueryAstExprKind::Compare); // C = 3 on the right
}

TEST(QueryL4Parser, AliasesAndDottedColumns)
{
	auto sel = Parse(wxT("SELECT p.Name AS n FROM Catalog.Products AS p ORDER BY p.Name DESC"));
	EXPECT_EQ(sel->m_from.m_alias, wxT("p"));
	ASSERT_EQ(sel->m_projections.size(), 1u);
	EXPECT_EQ(sel->m_projections[0].m_alias, wxT("n"));
	ASSERT_EQ(sel->m_projections[0].m_expr->m_path.size(), 2u);
	EXPECT_EQ(sel->m_projections[0].m_expr->m_path[0], wxT("p"));
	EXPECT_EQ(sel->m_projections[0].m_expr->m_path[1], wxT("Name"));
	ASSERT_EQ(sel->m_orderBy.size(), 1u);
	EXPECT_FALSE(sel->m_orderBy[0].m_ascending);
}

TEST(QueryL4Parser, AggregateGroupByHaving)
{
	auto sel = Parse(wxT("SELECT Manufacturer, SUM(Price) AS s FROM Catalog.Products "
	                     "GROUP BY Manufacturer HAVING SUM(Price) > 100"));
	ASSERT_EQ(sel->m_projections.size(), 2u);
	const ibQueryAstExprPtr& agg = sel->m_projections[1].m_expr;
	ASSERT_EQ(agg->m_kind, ibQueryAstExprKind::Func);
	EXPECT_EQ(agg->m_func, ibQueryKeyword::Sum);
	EXPECT_EQ(agg->m_arg->m_path[0], wxT("Price"));
	EXPECT_EQ(sel->m_projections[1].m_alias, wxT("s"));
	ASSERT_EQ(sel->m_groupBy.size(), 1u);
	EXPECT_EQ(sel->m_groupBy[0]->m_path[0], wxT("Manufacturer"));
	ASSERT_TRUE(sel->m_having != nullptr);
	EXPECT_EQ(sel->m_having->m_kind, ibQueryAstExprKind::Compare);
	EXPECT_EQ(sel->m_having->m_lhs->m_kind, ibQueryAstExprKind::Func);
}

TEST(QueryL4Parser, CountStar)
{
	auto sel = Parse(wxT("SELECT COUNT(*) AS n FROM Document.Orders"));
	const ibQueryAstExprPtr& agg = sel->m_projections[0].m_expr;
	ASSERT_EQ(agg->m_kind, ibQueryAstExprKind::Func);
	EXPECT_EQ(agg->m_func, ibQueryKeyword::Count);
	EXPECT_TRUE(agg->m_star);
}

TEST(QueryL4Parser, LikeInBetweenIsNull)
{
	auto sel = Parse(wxT("SELECT * FROM Catalog.X WHERE Name LIKE \"A%\" "
	                     "AND Code IN (\"1\", \"2\") AND Qty BETWEEN 1 AND 10 AND Ref IS NULL"));
	// the WHERE is a left-deep AND chain of 4 predicates; check each kind appears
	ASSERT_TRUE(sel->m_where != nullptr);
	// flatten the AND chain
	std::vector<ibQueryAstExprKind> kinds;
	std::function<void(const ibQueryAstExprPtr&)> walk = [&](const ibQueryAstExprPtr& n) {
		if (n->m_kind == ibQueryAstExprKind::Logical && !n->m_isOr) { walk(n->m_lhs); walk(n->m_rhs); }
		else kinds.push_back(n->m_kind);
	};
	walk(sel->m_where);
	ASSERT_EQ(kinds.size(), 4u);
	EXPECT_EQ(kinds[0], ibQueryAstExprKind::Like);
	EXPECT_EQ(kinds[1], ibQueryAstExprKind::In);
	EXPECT_EQ(kinds[2], ibQueryAstExprKind::Between);
	EXPECT_EQ(kinds[3], ibQueryAstExprKind::IsNull);
}

TEST(QueryL4Parser, NotInAndNotLike)
{
	auto sel = Parse(wxT("SELECT * FROM Catalog.X WHERE Code NOT IN (\"1\") AND Name NOT LIKE \"Z%\""));
	std::vector<const ibQueryAstExpr*> leaves;
	std::function<void(const ibQueryAstExprPtr&)> walk = [&](const ibQueryAstExprPtr& n) {
		if (n->m_kind == ibQueryAstExprKind::Logical && !n->m_isOr) { walk(n->m_lhs); walk(n->m_rhs); }
		else leaves.push_back(n.get());
	};
	walk(sel->m_where);
	ASSERT_EQ(leaves.size(), 2u);
	EXPECT_EQ(leaves[0]->m_kind, ibQueryAstExprKind::In);
	EXPECT_TRUE(leaves[0]->m_negated);
	EXPECT_EQ(leaves[1]->m_kind, ibQueryAstExprKind::Like);
	EXPECT_TRUE(leaves[1]->m_negated);
}

TEST(QueryL4Parser, JoinInnerAndLeft)
{
	auto sel = Parse(wxT("SELECT * FROM Document.Orders AS o "
	                     "INNER JOIN Catalog.Clients AS c ON o.Client = c.Ref "
	                     "LEFT JOIN Catalog.Stores AS s ON o.Store = s.Ref"));
	ASSERT_EQ(sel->m_joins.size(), 2u);
	EXPECT_EQ(sel->m_joins[0].m_kind, ibQueryJoinKindAst::Inner);
	EXPECT_EQ(sel->m_joins[0].m_source.m_alias, wxT("c"));
	ASSERT_TRUE(sel->m_joins[0].m_on != nullptr);
	EXPECT_EQ(sel->m_joins[0].m_on->m_kind, ibQueryAstExprKind::Compare);
	EXPECT_EQ(sel->m_joins[1].m_kind, ibQueryJoinKindAst::Left);
}

TEST(QueryL4Parser, TotalsByDimensions)
{
	// TOTALS <aggregates> BY <dimension levels> — hierarchical subtotals (1С ИТОГИ … ПО …).
	auto sel = Parse(wxT("SELECT Client, Amount FROM Document.Sales "
	                     "ORDER BY Client "
	                     "TOTALS SUM(Amount), MAX(Date) BY Client, Store Hierarchy"));
	EXPECT_TRUE(sel->m_hasTotals);
	ASSERT_EQ(sel->m_totalsAggregates.size(), 2u);
	EXPECT_EQ(sel->m_totalsAggregates[0]->m_func, ibQueryKeyword::Sum);
	EXPECT_EQ(sel->m_totalsAggregates[0]->m_arg->m_path[0], wxT("Amount"));
	EXPECT_EQ(sel->m_totalsAggregates[1]->m_func, ibQueryKeyword::Max);
	ASSERT_EQ(sel->m_totalsBy.size(), 2u);
	EXPECT_EQ(sel->m_totalsBy[0].m_expr->m_path[0], wxT("Client"));
	EXPECT_EQ(sel->m_totalsBy[0].m_unfold, ibQueryDimUnfold::Elements);   // no keyword => Elements
	EXPECT_EQ(sel->m_totalsBy[1].m_expr->m_path[0], wxT("Store"));
	EXPECT_EQ(sel->m_totalsBy[1].m_unfold, ibQueryDimUnfold::Hierarchy);  // explicit HIERARCHY
}

TEST(QueryL4Parser, SyntaxErrorThrows)
{
	EXPECT_THROW(Parse(wxT("SELECT FROM Catalog.X")), ibBackendException);          // empty select list
	EXPECT_THROW(Parse(wxT("SELECT Code Catalog.X")),  ibBackendException);         // missing FROM
	EXPECT_THROW(Parse(wxT("SELECT Code FROM")),       ibBackendException);         // missing source
}

// --- the extended grammar: arithmetic, CASE, UNION, IN-subquery, source-args, subquery source -------

TEST(QueryL4Parser, ArithmeticPrecedence)
{
	// Qty * Price + 1  ==  (Qty * Price) + 1   (* binds tighter than +)
	auto sel = Parse(wxT("SELECT Qty * Price + 1 AS total FROM Catalog.X"));
	ASSERT_EQ(sel->m_projections.size(), 1u);
	const ibQueryAstExprPtr& e = sel->m_projections[0].m_expr;
	ASSERT_EQ(e->m_kind, ibQueryAstExprKind::Arith);
	EXPECT_EQ(e->m_arith, ibQueryArithOp::Add);                 // top is +
	EXPECT_EQ(e->m_rhs->m_kind, ibQueryAstExprKind::Literal);      // ... + 1
	ASSERT_EQ(e->m_lhs->m_kind, ibQueryAstExprKind::Arith);        // (Qty * Price)
	EXPECT_EQ(e->m_lhs->m_arith, ibQueryArithOp::Mul);
	EXPECT_EQ(e->m_lhs->m_lhs->m_path[0], wxT("Qty"));
	EXPECT_EQ(e->m_lhs->m_rhs->m_path[0], wxT("Price"));
	EXPECT_EQ(sel->m_projections[0].m_alias, wxT("total"));
}

TEST(QueryL4Parser, CaseExpression)
{
	auto sel = Parse(wxT("SELECT CASE WHEN Qty > 100 THEN \"bulk\" ELSE \"unit\" END AS kind FROM Catalog.X"));
	const ibQueryAstExprPtr& e = sel->m_projections[0].m_expr;
	ASSERT_EQ(e->m_kind, ibQueryAstExprKind::Case);
	ASSERT_EQ(e->m_cases.size(), 1u);
	EXPECT_EQ(e->m_cases[0].first->m_kind, ibQueryAstExprKind::Compare);   // WHEN Qty > 100
	EXPECT_EQ(e->m_cases[0].second->m_kind, ibQueryAstExprKind::Literal);  // THEN "bulk"
	ASSERT_TRUE(e->m_else != nullptr);                                  // ELSE "unit"
	EXPECT_EQ(sel->m_projections[0].m_alias, wxT("kind"));
}

TEST(QueryL4Parser, UnionBranchesAndTrailingOrder)
{
	auto sel = Parse(wxT("SELECT Code FROM Catalog.X UNION ALL SELECT Code FROM Catalog.Y ORDER BY Code"));
	ASSERT_EQ(sel->m_unions.size(), 1u);                       // one extra branch
	EXPECT_EQ(sel->m_from.m_name[1], wxT("X"));                // first branch
	EXPECT_EQ(sel->m_unions[0]->m_from.m_name[1], wxT("Y"));   // union branch
	ASSERT_EQ(sel->m_orderBy.size(), 1u);                      // ORDER applies to the whole union
	EXPECT_TRUE(sel->m_unions[0]->m_orderBy.empty());          // not to the branch
}

TEST(QueryL4Parser, InSubquery)
{
	auto sel = Parse(wxT("SELECT * FROM Catalog.X WHERE Ref IN (SELECT Owner FROM Catalog.Y)"));
	ASSERT_TRUE(sel->m_where != nullptr);
	ASSERT_EQ(sel->m_where->m_kind, ibQueryAstExprKind::In);
	EXPECT_TRUE(sel->m_where->m_list.empty());                 // no value list
	ASSERT_TRUE(sel->m_where->m_subquery != nullptr);          // a nested SELECT instead
	EXPECT_EQ(sel->m_where->m_subquery->m_from.m_name[1], wxT("Y"));
}

TEST(QueryL4Parser, SourceCallArgs)
{
	auto sel = Parse(wxT("SELECT * FROM AccumulationRegister.Goods.Balance(&Period, &Filter) AS b"));
	ASSERT_EQ(sel->m_from.m_name.size(), 3u);                  // AccumulationRegister.Goods.Balance
	EXPECT_EQ(sel->m_from.m_name[2], wxT("Balance"));
	ASSERT_EQ(sel->m_from.m_args.size(), 2u);                  // (&Period, &Filter)
	EXPECT_EQ(sel->m_from.m_args[0]->m_kind, ibQueryAstExprKind::Param);
	EXPECT_EQ(sel->m_from.m_args[0]->m_paramName, wxT("Period"));
	EXPECT_EQ(sel->m_from.m_alias, wxT("b"));
}

TEST(QueryL4Parser, SubquerySource)
{
	auto sel = Parse(wxT("SELECT s.Code FROM (SELECT Code FROM Catalog.X WHERE Price > 0) AS s"));
	EXPECT_TRUE(sel->m_from.m_name.empty());
	ASSERT_TRUE(sel->m_from.m_subquery != nullptr);
	EXPECT_EQ(sel->m_from.m_alias, wxT("s"));
	EXPECT_EQ(sel->m_from.m_subquery->m_from.m_name[1], wxT("X"));
	ASSERT_TRUE(sel->m_from.m_subquery->m_where != nullptr);
}

TEST(QueryL4Parser, ModuloIsArithmetic)
{
	auto sel = Parse(wxT("SELECT Id % 2 AS r FROM Catalog.X"));
	const ibQueryAstExprPtr& e = sel->m_projections[0].m_expr;
	ASSERT_EQ(e->m_kind, ibQueryAstExprKind::Arith);
	EXPECT_EQ(e->m_arith, ibQueryArithOp::Mod);
}
