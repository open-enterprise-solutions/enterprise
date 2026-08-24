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
	// A STRING LITERAL IS DOUBLE-QUOTED. `'a%'` is a DATE literal in this dialect, and it used to
	// parse here only because a failed date parse was ignored and left the value empty — the pattern
	// this test names was never actually in the query it built (the lexer says so out loud now).
	auto sel = ParseAndRewrite(wxT("SELECT * FROM Catalog.Products WHERE NOT (Name LIKE \"a%\")"));
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
		wxT("SELECT pn FROM (SELECT Producer.Name AS pn FROM Catalog.Products) AS s WHERE pn LIKE \"a%\""));

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

//////////////////////////////////////////////////////////////////////
// Rule — a run of INNER joins is ordered so every link stands on
// tables already read. The author writes tables in the order they
// think of them; which one has to come first is the engine's problem.
//////////////////////////////////////////////////////////////////////

namespace {
// The name a source is known by in the FROM list — the alias where there is one.
wxString JoinedName(const ibQuerySelect& sel, size_t index)
{
	const ibQuerySource& source = sel.m_joins[index].m_source;
	if (!source.m_alias.IsEmpty())
		return source.m_alias;
	return source.m_name.empty() ? wxString() : source.m_name.back();
}
} // namespace

TEST(QueryRewrite, InnerJoinNamingALaterTable_IsMovedAfterIt)
{
	// `b` is linked to `c`, which is written after it. Both joins are INNER, so the order is the
	// engine's to choose and `c` has to be read first.
	auto sel = ParseAndRewrite(
		wxT("SELECT b.Code FROM Catalog.Products AS a ")
		wxT("INNER JOIN Catalog.Units AS b ON b.Owner = c.Ref ")
		wxT("INNER JOIN Catalog.Owners AS c ON c.Ref = a.Owner"));

	ASSERT_EQ(sel->m_joins.size(), 2u);
	EXPECT_EQ(JoinedName(*sel, 0), wxT("c"));
	EXPECT_EQ(JoinedName(*sel, 1), wxT("b"));
}

TEST(QueryRewrite, AnOuterJoinIsAFence_NothingCrossesIt)
{
	// The same shape with `c` joined LEFT. Position is meaning for an outer join — which side is
	// null-padded, and what a later link sees of the padded rows — so it does not move, and `b`
	// cannot jump over it. The query stays as written and the consistency check owns the complaint.
	auto sel = ParseAndRewrite(
		wxT("SELECT b.Code FROM Catalog.Products AS a ")
		wxT("INNER JOIN Catalog.Units AS b ON b.Owner = c.Ref ")
		wxT("LEFT JOIN Catalog.Owners AS c ON c.Ref = a.Owner"));

	ASSERT_EQ(sel->m_joins.size(), 2u);
	EXPECT_EQ(JoinedName(*sel, 0), wxT("b"));
	EXPECT_EQ(JoinedName(*sel, 1), wxT("c"));
}

TEST(QueryRewrite, ACycleIsLeftExactlyAsWritten)
{
	// `b` needs `c` and `c` needs `b`: there is no order in which both links stand on tables already
	// read. Inventing one would replace a clear complaint with a silent wrong answer.
	auto sel = ParseAndRewrite(
		wxT("SELECT b.Code FROM Catalog.Products AS a ")
		wxT("INNER JOIN Catalog.Units AS b ON b.Owner = c.Ref ")
		wxT("INNER JOIN Catalog.Owners AS c ON c.Ref = b.Owner"));

	ASSERT_EQ(sel->m_joins.size(), 2u);
	EXPECT_EQ(JoinedName(*sel, 0), wxT("b"));
	EXPECT_EQ(JoinedName(*sel, 1), wxT("c"));
}

TEST(QueryRewrite, AlreadyOrderedJoins_AreLeftAlone)
{
	// The pass must be idempotent on a sound query — a reorder that shuffled a correct FROM would
	// change the plan under every existing query for no reason.
	auto sel = ParseAndRewrite(
		wxT("SELECT b.Code FROM Catalog.Products AS a ")
		wxT("INNER JOIN Catalog.Units AS b ON b.Owner = a.Ref ")
		wxT("INNER JOIN Catalog.Owners AS c ON c.Ref = b.Owner"));

	ASSERT_EQ(sel->m_joins.size(), 2u);
	EXPECT_EQ(JoinedName(*sel, 0), wxT("b"));
	EXPECT_EQ(JoinedName(*sel, 1), wxT("c"));
}
