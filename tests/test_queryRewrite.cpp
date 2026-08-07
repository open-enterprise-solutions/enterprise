// L4 — optimizer rewrite pass tests (queryRewrite.{h,cpp}).
//
// Golden "AST -> AST": parse text, run ibQueryRewrite::Rewrite, assert the rewritten
// tree shape. Pure — no metadata, no database (same harness tier as the parser tests).
//
// Rule 1 — negation normalization: NOT absorbed into comparisons / node-negation
// flags / De Morgan; truthy NOT col -> col = FALSE.
// Rule 2 — FROM-subquery flattening: a plain inner projection merges into the outer
// query (one server-side SELECT instead of a RAM-materialised ibSubqueryQueryable).

#include <gtest/gtest.h>

#include "backend/query/queryParser.h"
#include "backend/query/queryRewrite.h"

namespace {

ibQuerySelectPtr ParseAndRewrite(const wxString& text)
{
	ibQueryParser parser;
	ibQuerySelectPtr ast = parser.Parse(text);
	EXPECT_TRUE(ast != nullptr);
	return ibQueryRewrite::Rewrite(*ast);
}

} // namespace

//////////////////////////////////////////////////////////////////////
// Rule 1 — negation normalization
//////////////////////////////////////////////////////////////////////

TEST(QueryRewrite, NotCompare_BecomesInvertedCompare)
{
	auto sel = ParseAndRewrite(wxT("SELECT * FROM Catalog.Products WHERE NOT (Code = 5)"));
	ASSERT_TRUE(sel->m_where != nullptr);
	EXPECT_EQ(sel->m_where->m_kind, ibQueryAstExprKind::Compare);
	EXPECT_EQ(sel->m_where->m_cmp, ibQueryCompareOp::Ne);
	EXPECT_EQ(sel->m_where->m_lhs->m_path[0], wxT("Code"));
}

TEST(QueryRewrite, NotOrderedCompare_InvertsOperator)
{
	auto sel = ParseAndRewrite(wxT("SELECT * FROM Catalog.Products WHERE NOT (Price < 100)"));
	EXPECT_EQ(sel->m_where->m_kind, ibQueryAstExprKind::Compare);
	EXPECT_EQ(sel->m_where->m_cmp, ibQueryCompareOp::Ge);
}

TEST(QueryRewrite, DoubleNot_Eliminated)
{
	auto sel = ParseAndRewrite(wxT("SELECT * FROM Catalog.Products WHERE NOT NOT (Price < 100)"));
	EXPECT_EQ(sel->m_where->m_kind, ibQueryAstExprKind::Compare);
	EXPECT_EQ(sel->m_where->m_cmp, ibQueryCompareOp::Lt);
}

TEST(QueryRewrite, DeMorgan_NotOverOr_BecomesAndOfInverted)
{
	auto sel = ParseAndRewrite(wxT("SELECT * FROM Catalog.Products WHERE NOT (Code = 1 OR Price = 2)"));
	ASSERT_EQ(sel->m_where->m_kind, ibQueryAstExprKind::Logical);
	EXPECT_FALSE(sel->m_where->m_isOr);   // OR -> AND
	EXPECT_EQ(sel->m_where->m_lhs->m_kind, ibQueryAstExprKind::Compare);
	EXPECT_EQ(sel->m_where->m_lhs->m_cmp, ibQueryCompareOp::Ne);
	EXPECT_EQ(sel->m_where->m_rhs->m_kind, ibQueryAstExprKind::Compare);
	EXPECT_EQ(sel->m_where->m_rhs->m_cmp, ibQueryCompareOp::Ne);
}

TEST(QueryRewrite, NotLike_TogglesNodeNegation)
{
	auto sel = ParseAndRewrite(wxT("SELECT * FROM Catalog.Products WHERE NOT (Name LIKE 'a%')"));
	EXPECT_EQ(sel->m_where->m_kind, ibQueryAstExprKind::Like);
	EXPECT_TRUE(sel->m_where->m_negated);
}

TEST(QueryRewrite, NotTruthyColumn_BecomesEqualsFalse)
{
	auto sel = ParseAndRewrite(wxT("SELECT * FROM Catalog.Products WHERE NOT IsFolder"));
	ASSERT_EQ(sel->m_where->m_kind, ibQueryAstExprKind::Compare);
	EXPECT_EQ(sel->m_where->m_cmp, ibQueryCompareOp::Eq);
	EXPECT_EQ(sel->m_where->m_lhs->m_path[0], wxT("IsFolder"));
	ASSERT_EQ(sel->m_where->m_rhs->m_kind, ibQueryAstExprKind::Literal);
	EXPECT_FALSE(sel->m_where->m_rhs->m_literal.GetBoolean());
}

//////////////////////////////////////////////////////////////////////
// Rule 2 — FROM-subquery flattening
//////////////////////////////////////////////////////////////////////

TEST(QueryRewrite, FlattenSimpleSubquery_MergesFromAndWhere)
{
	auto sel = ParseAndRewrite(
		wxT("SELECT Code FROM (SELECT Code, Price FROM Catalog.Products WHERE Price > 100) AS s WHERE Code = 5"));

	// FROM collapsed onto the real source
	EXPECT_TRUE(sel->m_from.m_subquery == nullptr);
	ASSERT_EQ(sel->m_from.m_name.size(), 2u);
	EXPECT_EQ(sel->m_from.m_name[0], wxT("Catalog"));
	EXPECT_EQ(sel->m_from.m_name[1], wxT("Products"));

	// WHERE = And(outer, inner)
	ASSERT_EQ(sel->m_where->m_kind, ibQueryAstExprKind::Logical);
	EXPECT_FALSE(sel->m_where->m_isOr);
	EXPECT_EQ(sel->m_where->m_lhs->m_kind, ibQueryAstExprKind::Compare);
	EXPECT_EQ(sel->m_where->m_lhs->m_lhs->m_path[0], wxT("Code"));
	EXPECT_EQ(sel->m_where->m_rhs->m_kind, ibQueryAstExprKind::Compare);
	EXPECT_EQ(sel->m_where->m_rhs->m_lhs->m_path[0], wxT("Price"));
	EXPECT_EQ(sel->m_where->m_rhs->m_cmp, ibQueryCompareOp::Gt);
}

TEST(QueryRewrite, Flatten_SubstitutesAliasedDotWalk)
{
	auto sel = ParseAndRewrite(
		wxT("SELECT pn FROM (SELECT Producer.Name AS pn FROM Catalog.Products) AS s WHERE pn LIKE 'a%'"));

	EXPECT_TRUE(sel->m_from.m_subquery == nullptr);

	// projection re-expanded into the dot-walk, output name preserved via the alias
	ASSERT_EQ(sel->m_projections.size(), 1u);
	ASSERT_EQ(sel->m_projections[0].m_expr->m_path.size(), 2u);
	EXPECT_EQ(sel->m_projections[0].m_expr->m_path[0], wxT("Producer"));
	EXPECT_EQ(sel->m_projections[0].m_expr->m_path[1], wxT("Name"));
	EXPECT_EQ(sel->m_projections[0].m_alias, wxT("pn"));

	// WHERE references the substituted path too
	ASSERT_EQ(sel->m_where->m_kind, ibQueryAstExprKind::Like);
	ASSERT_EQ(sel->m_where->m_lhs->m_path.size(), 2u);
	EXPECT_EQ(sel->m_where->m_lhs->m_path[0], wxT("Producer"));
}

TEST(QueryRewrite, Flatten_OuterStarTakesInnerProjection)
{
	auto sel = ParseAndRewrite(wxT("SELECT * FROM (SELECT Code FROM Catalog.Products) AS s"));
	EXPECT_TRUE(sel->m_from.m_subquery == nullptr);
	EXPECT_FALSE(sel->m_selectAll);
	ASSERT_EQ(sel->m_projections.size(), 1u);
	EXPECT_EQ(sel->m_projections[0].m_expr->m_path[0], wxT("Code"));
}

TEST(QueryRewrite, Flatten_StripsOuterAliasQualifier)
{
	auto sel = ParseAndRewrite(
		wxT("SELECT s.Code FROM (SELECT * FROM Catalog.Products) AS s WHERE s.Code = 1"));
	EXPECT_TRUE(sel->m_from.m_subquery == nullptr);
	ASSERT_EQ(sel->m_projections[0].m_expr->m_path.size(), 1u);
	EXPECT_EQ(sel->m_projections[0].m_expr->m_path[0], wxT("Code"));
	ASSERT_EQ(sel->m_where->m_lhs->m_path.size(), 1u);
	EXPECT_EQ(sel->m_where->m_lhs->m_path[0], wxT("Code"));
}

TEST(QueryRewrite, Flatten_NestedSubqueries_CollapseBothLevels)
{
	auto sel = ParseAndRewrite(
		wxT("SELECT Code FROM (SELECT Code FROM (SELECT Code FROM Catalog.Products WHERE Price > 1) AS a WHERE Code <> 0) AS b"));
	EXPECT_TRUE(sel->m_from.m_subquery == nullptr);
	ASSERT_EQ(sel->m_from.m_name.size(), 2u);
	EXPECT_EQ(sel->m_from.m_name[1], wxT("Products"));
	// WHERE = And(middle, innermost)
	ASSERT_EQ(sel->m_where->m_kind, ibQueryAstExprKind::Logical);
	EXPECT_FALSE(sel->m_where->m_isOr);
	EXPECT_EQ(sel->m_where->m_lhs->m_cmp, ibQueryCompareOp::Ne);
	EXPECT_EQ(sel->m_where->m_rhs->m_cmp, ibQueryCompareOp::Gt);
}

TEST(QueryRewrite, NoFlatten_AggregateInner)
{
	auto sel = ParseAndRewrite(
		wxT("SELECT c FROM (SELECT SUM(Price) AS c FROM Catalog.Products) AS s"));
	EXPECT_TRUE(sel->m_from.m_subquery != nullptr);   // wrapped path preserved
}

TEST(QueryRewrite, NoFlatten_DistinctInner)
{
	auto sel = ParseAndRewrite(
		wxT("SELECT Code FROM (SELECT DISTINCT Code FROM Catalog.Products) AS s"));
	EXPECT_TRUE(sel->m_from.m_subquery != nullptr);
}

TEST(QueryRewrite, NoFlatten_OutOfScopeReference)
{
	// The subquery exposes only Code; the outer WHERE names Price. Flattening would
	// silently legalize the reference against the real table — must stay wrapped (and
	// keep failing at resolution with the honest "unknown attribute" error).
	auto sel = ParseAndRewrite(
		wxT("SELECT Code FROM (SELECT Code FROM Catalog.Products) AS s WHERE Price > 5"));
	EXPECT_TRUE(sel->m_from.m_subquery != nullptr);
}

TEST(QueryRewrite, NoFlatten_OuterJoinKeepsSubquery)
{
	auto sel = ParseAndRewrite(
		wxT("SELECT s.Code FROM (SELECT Code FROM Catalog.Products) AS s ")
		wxT("LEFT JOIN Catalog.Units AS u ON s.Code = u.Code"));
	EXPECT_TRUE(sel->m_from.m_subquery != nullptr);   // conservative: multi-source outer
}

//////////////////////////////////////////////////////////////////////
// The pass never mutates the cached parse
//////////////////////////////////////////////////////////////////////

TEST(QueryRewrite, OriginalAstUntouched)
{
	ibQueryParser parser;
	ibQuerySelectPtr ast = parser.Parse(
		wxT("SELECT Code FROM (SELECT Code FROM Catalog.Products) AS s WHERE NOT (Code = 5)"));
	ASSERT_TRUE(ast != nullptr);

	ibQuerySelectPtr rewritten = ibQueryRewrite::Rewrite(*ast);

	// rewritten: flattened + Not absorbed
	EXPECT_TRUE(rewritten->m_from.m_subquery == nullptr);
	EXPECT_EQ(rewritten->m_where->m_kind, ibQueryAstExprKind::Compare);

	// original: still the parsed shape (one parse — many executes)
	EXPECT_TRUE(ast->m_from.m_subquery != nullptr);
	EXPECT_EQ(ast->m_where->m_kind, ibQueryAstExprKind::Not);
}

TEST(QueryRewrite, NoFlatten_InnerTop)
{
	// TOP limits the inner rows; merging the subquery into the outer would lose the limit.
	auto sel = ParseAndRewrite(
		wxT("SELECT Code FROM (SELECT TOP 10 Code FROM Catalog.Products) AS s"));
	EXPECT_TRUE(sel->m_from.m_subquery != nullptr);
}

TEST(QueryRewrite, NoFlatten_InnerAllowed)
{
	// ALLOWED merged away would turn "show me what I may see" back into a refusal — and the query
	// would still run, which is what makes this the silent kind of wrong. (The parser refuses INTO
	// and FOR UPDATE inside a source, so ALLOWED is the one of the three that can reach here.)
	auto sel = ParseAndRewrite(
		wxT("SELECT Code FROM (SELECT ALLOWED Code FROM Catalog.Products) AS s"));
	ASSERT_TRUE(sel->m_from.m_subquery != nullptr);
	EXPECT_TRUE(sel->m_from.m_subquery->m_allowed);
}

TEST(QueryRewrite, FlattenStillHappensWithoutThoseWords)
{
	// The guard above must be about the clause, not about flattening.
	auto sel = ParseAndRewrite(
		wxT("SELECT Code FROM (SELECT Code FROM Catalog.Products) AS s"));
	EXPECT_TRUE(sel->m_from.m_subquery == nullptr);
}

TEST(QueryRewrite, CloneCarriesTheStatementWords)
{
	// The rewrite works on a deep clone; a POD field the clone forgot would be dropped from every
	// execution while the parse still showed it.
	ibQueryParser parser;
	ibQuerySelectPtr ast = parser.Parse(
		wxT("SELECT ALLOWED Code FROM Catalog.Products WHERE Code = \"1\" FOR UPDATE"));
	ASSERT_TRUE(ast != nullptr);

	ibQuerySelectPtr rewritten = ibQueryRewrite::Rewrite(*ast);
	EXPECT_TRUE(rewritten->m_allowed);
	EXPECT_TRUE(rewritten->m_forUpdate);
}
