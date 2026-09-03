// L4-1 text query language — parser tests (queryParser.{h,cpp}).
//
// Golden "text -> AST": pure parse, no metadata, no database. Confirms the
// recursive-descent grammar (SELECT list / FROM / JOIN / WHERE precedence /
// GROUP BY / HAVING / ORDER BY / aggregates / &params / LIKE-IN-BETWEEN-IS NULL).

#include <gtest/gtest.h>

#include <functional>
#include <vector>

#include "backend/query/queryParser.h"
// ⚠ AT THE TOP, not beside the round-trip section further down: a test up here renders an AST back
// to text (ONTO is written where INTO is), and an include that arrives 200 lines later is not
// visible to it. MSBuild does not compile tests, so this only ever fails in the CMake tree.
#include "backend/query/queryRender.h"

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

TEST(QueryL4Parser, OrderByTakesAnExpression)
{
	// Sorting by a CONDITION is an ordinary thing to ask ("the one I care about first"), the lowering
	// has always carried it (OrderByExpr), and only the parser refused — "unexpected text after the
	// query", pointing at the word CASE.
	auto sel = Parse(wxT("SELECT Code FROM Catalog.Products ")
	                 wxT("ORDER BY CASE WHEN Code = \"A\" THEN 0 ELSE 1 END, Price * Qty DESC"));
	ASSERT_TRUE(sel != nullptr);
	ASSERT_EQ(sel->m_orderBy.size(), 2u);
	EXPECT_EQ(sel->m_orderBy[0].m_expr->m_kind, ibQueryAstExprKind::Case);
	EXPECT_TRUE(sel->m_orderBy[0].m_ascending);
	EXPECT_EQ(sel->m_orderBy[1].m_expr->m_kind, ibQueryAstExprKind::Arith);
	EXPECT_FALSE(sel->m_orderBy[1].m_ascending);

	// …and it survives the round trip, which is what the constructor reads back.
	const wxString written = ibRenderQuery(*sel);
	auto again = Parse(written);
	ASSERT_TRUE(again != nullptr);
	ASSERT_EQ(again->m_orderBy.size(), 2u);
	EXPECT_EQ(written, ibRenderQuery(*again));
}

TEST(QueryL4Parser, OrderByStillReadsAKeywordAsAName)
{
	// The other half of the same rule, and the reason the expression road is entered by the FIRST
	// TOKEN rather than always: an attribute called `Order` / `Count` / `Value` keeps sorting the way
	// it always has. An enumeration's own `Order` attribute is the case that put this rule here.
	auto sel = Parse(wxT("SELECT Code FROM Catalog.Products ORDER BY Order, Value DESC"));
	ASSERT_TRUE(sel != nullptr);
	ASSERT_EQ(sel->m_orderBy.size(), 2u);
	EXPECT_EQ(sel->m_orderBy[0].m_expr->m_kind, ibQueryAstExprKind::Column);
	ASSERT_EQ(sel->m_orderBy[0].m_expr->m_path.size(), 1u);
	EXPECT_EQ(sel->m_orderBy[0].m_expr->m_path[0], wxT("Order"));
	EXPECT_EQ(sel->m_orderBy[1].m_expr->m_kind, ibQueryAstExprKind::Column);
	EXPECT_FALSE(sel->m_orderBy[1].m_ascending);
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
	// TOTALS <aggregates> BY <dimension levels> — hierarchical subtotals.
	auto sel = Parse(wxT("SELECT Client, Amount FROM Document.Sales "
	                     "ORDER BY Client "
	                     "TOTALS SUM(Amount), MAX(Date) BY Client, Store Hierarchy"));
	EXPECT_TRUE(sel->m_hasTotals);
	ASSERT_EQ(sel->m_totalsAggregates.size(), 2u);
	EXPECT_EQ(sel->m_totalsAggregates[0].m_expr->m_func, ibQueryKeyword::Sum);
	EXPECT_EQ(sel->m_totalsAggregates[0].m_expr->m_arg->m_path[0], wxT("Amount"));
	EXPECT_EQ(sel->m_totalsAggregates[1].m_expr->m_func, ibQueryKeyword::Max);
	// A RESOURCE NOBODY NAMED carries no alias — the engine names it after its argument.
	EXPECT_TRUE(sel->m_totalsAggregates[0].m_alias.IsEmpty());
	ASSERT_EQ(sel->m_totalsBy.size(), 2u);
	ASSERT_EQ(sel->m_totalsBy[0].m_fields.size(), 1u);                       // a bare dimension is a level of ONE field
	EXPECT_EQ(sel->m_totalsBy[0].Head()->m_expr->m_path[0], wxT("Client"));
	EXPECT_EQ(sel->m_totalsBy[0].HeadUnfold(), ibQueryDimUnfold::Elements);  // no keyword => Elements
	EXPECT_EQ(sel->m_totalsBy[1].Head()->m_expr->m_path[0], wxT("Store"));
	EXPECT_EQ(sel->m_totalsBy[1].HeadUnfold(), ibQueryDimUnfold::Hierarchy); // explicit HIERARCHY
	EXPECT_TRUE(sel->m_totalsBy[0].m_alias.IsEmpty());                    // no name given => the column's own
}

// A DIMENSION LEVEL CAN BE NAMED. Without it, two levels over the same column answer to the same
// name and one of them wins — which is the whole reason the alias exists (ibQueryTotalDim::m_alias).
TEST(QueryL4Parser, TotalsDimensionAlias)
{
	auto sel = Parse(wxT("SELECT Client, Amount FROM Document.Sales "
	                     "TOTALS SUM(Amount) BY Date HIERARCHY AS Period, Client AS Buyer"));
	ASSERT_EQ(sel->m_totalsBy.size(), 2u);
	EXPECT_EQ(sel->m_totalsBy[0].Head()->m_expr->m_path[0], wxT("Date"));
	EXPECT_EQ(sel->m_totalsBy[0].HeadUnfold(), ibQueryDimUnfold::Hierarchy);   // the unfold stays the dimension's
	EXPECT_EQ(sel->m_totalsBy[0].m_alias, wxT("Period"));                  // the alias is the LEVEL's
	EXPECT_EQ(sel->m_totalsBy[1].m_alias, wxT("Buyer"));

	// The bare form (no AS) reads the same way — an identifier after a dimension can only be its name.
	auto bare = Parse(wxT("SELECT Amount FROM Document.Sales TOTALS SUM(Amount) BY Client Buyer"));
	ASSERT_EQ(bare->m_totalsBy.size(), 1u);
	EXPECT_EQ(bare->m_totalsBy[0].m_alias, wxT("Buyer"));
}

// ONE LEVEL OVER SEVERAL FIELDS. The bracket is what says "together": inside it the comma separates
// FIELDS OF ONE LEVEL, outside it the comma separates LEVELS — and a report grouping by partner and
// contract at once is one heading, not two nested ones.
TEST(QueryL4Parser, TotalsLevelOverSeveralFields)
{
	auto sel = Parse(wxT("SELECT Amount FROM Document.Sales "
	                     "TOTALS SUM(Amount) BY (Partner, Contract) AS Party, Store"));
	ASSERT_EQ(sel->m_totalsBy.size(), 2u);                                 // TWO levels, not three

	ASSERT_EQ(sel->m_totalsBy[0].m_fields.size(), 2u);                     // the bracketed pair is ONE level
	EXPECT_EQ(sel->m_totalsBy[0].m_fields[0].m_expr->m_path[0], wxT("Partner"));
	EXPECT_EQ(sel->m_totalsBy[0].m_fields[1].m_expr->m_path[0], wxT("Contract"));
	EXPECT_EQ(sel->m_totalsBy[0].m_alias, wxT("Party"));                   // the name belongs to the LEVEL
	EXPECT_FALSE(sel->m_totalsBy[0].IsSingleField());

	ASSERT_EQ(sel->m_totalsBy[1].m_fields.size(), 1u);                     // and the plain one beside it
	EXPECT_EQ(sel->m_totalsBy[1].Head()->m_expr->m_path[0], wxT("Store"));
}

// BY … PERIODS(unit[, from, to]) — read where the unfold is read, because it says the same KIND of
// thing: how that field is read. The unit is a bare word (the lowering owns that vocabulary, the
// same one a register's Turnovers argument is read by) and both bounds are optional.
TEST(QueryL4Parser, TotalsLevelByPeriods)
{
	auto sel = Parse(wxT("SELECT Amount FROM Document.Sales "
	                     "TOTALS SUM(Amount) BY Date PERIODS(Month, &From, &To) AS Period, Store"));
	ASSERT_EQ(sel->m_totalsBy.size(), 2u);

	const ibQueryTotalField* head = sel->m_totalsBy[0].Head();
	ASSERT_TRUE(head != nullptr);
	EXPECT_EQ(head->m_expr->m_path[0], wxT("Date"));
	ASSERT_TRUE(head->m_periods != nullptr);
	EXPECT_EQ(head->m_periods->m_unit, wxT("Month"));
	ASSERT_TRUE(head->m_periods->m_from != nullptr);
	ASSERT_TRUE(head->m_periods->m_to   != nullptr);
	EXPECT_EQ(sel->m_totalsBy[0].m_alias, wxT("Period"));     // the level still names itself

	// …and the plain level beside it is untouched: nothing about PERIODS changes how the list reads.
	ASSERT_TRUE(sel->m_totalsBy[1].Head() != nullptr);
	EXPECT_TRUE(sel->m_totalsBy[1].Head()->m_periods == nullptr);
}

// ONE LEVEL FIELD FROM TEXT, and back. The query constructor edits a level's fields as text, so it
// reads them through the LANGUAGE and writes them through the RENDERER — the pair below is what
// keeps a form from quietly dropping what it does not recognise (it did: a hand-written PERIODS was
// invisible in the grid, and the next edit of that cell threw it away).
TEST(QueryL4Parser, TotalsFieldFromTextRoundTrips)
{
	ibQueryParser parser;

	const ibQueryTotalField plain = parser.ParseTotalsField(wxT("Store"));
	EXPECT_EQ(plain.m_unfold, ibQueryDimUnfold::Elements);
	EXPECT_TRUE(plain.m_periods == nullptr);
	EXPECT_EQ(ibRenderTotalField(plain), wxT("Store"));

	const ibQueryTotalField unfolded = parser.ParseTotalsField(wxT("Store HIERARCHY"));
	EXPECT_EQ(unfolded.m_unfold, ibQueryDimUnfold::Hierarchy);
	EXPECT_EQ(ibRenderTotalField(unfolded), wxT("Store HIERARCHY"));

	const ibQueryTotalField periodic = parser.ParseTotalsField(wxT("Period PERIODS(Month, &From, &To)"));
	ASSERT_TRUE(periodic.m_periods != nullptr);
	EXPECT_EQ(periodic.m_periods->m_unit, wxT("Month"));
	EXPECT_EQ(ibRenderTotalField(periodic), wxT("Period PERIODS(Month, &From, &To)"));

	// …and the unwritten bound stays unwritten: an empty slot means "from the data", so putting one
	// back would be the renderer answering for the author.
	const ibQueryTotalField bare = parser.ParseTotalsField(wxT("Period PERIODS(Day)"));
	EXPECT_EQ(ibRenderTotalField(bare), wxT("Period PERIODS(Day)"));

	// Text the language does not accept is refused, not half-read.
	EXPECT_THROW(parser.ParseTotalsField(wxT("Period PERIODS(Month) AND")), ibBackendException);
}

// The bounds are OPTIONAL — `PERIODS(Month)` alone is the whole of it, and the series is then padded
// between the first and the last period the data holds.
TEST(QueryL4Parser, TotalsPeriodsWithoutBounds)
{
	auto sel = Parse(wxT("SELECT Amount FROM Document.Sales TOTALS SUM(Amount) BY Date PERIODS(Day)"));
	ASSERT_EQ(sel->m_totalsBy.size(), 1u);
	const ibQueryTotalField* head = sel->m_totalsBy[0].Head();
	ASSERT_TRUE(head != nullptr && head->m_periods != nullptr);
	EXPECT_EQ(head->m_periods->m_unit, wxT("Day"));
	EXPECT_TRUE(head->m_periods->m_from == nullptr);
	EXPECT_TRUE(head->m_periods->m_to   == nullptr);
}

// ONTO NAMES A RESULT — and it is the pair to INTO, not a synonym: INTO makes a temporary table and
// hands back a row count, ONTO names the result that comes back. A reader then asks for "Sales"
// instead of "the third statement", which is the one thing a position cannot survive: inserting a
// statement above it renumbers everything below.
TEST(QueryL4Parser, OntoNamesTheResult)
{
	auto sel = Parse(wxT("SELECT Client, Amount ONTO Sales FROM Document.Sales"));
	EXPECT_EQ(sel->m_ontoName, wxT("Sales"));
	EXPECT_TRUE(sel->m_intoTemp.IsEmpty());

	// Written where INTO is written, and rendered back to the same place.
	const wxString text = ibRenderQuery(*sel);
	EXPECT_TRUE(text.Contains(wxT("ONTO Sales")));
	EXPECT_EQ(Parse(text)->m_ontoName, wxT("Sales"));
}

// A QUERY RESULT LINK. A later statement READS a result an earlier one named, and it
// says so the way it says everything else: by naming it as a source and joining to it. The language
// needs nothing new for this; what it needs is that a bare name in FROM stays a bare name until
// somebody resolves it (the package does, at execution).
TEST(QueryL4Parser, AStatementCanReadANamedResult)
{
	auto package = ibQueryParser().ParsePackage(
		// ⚠ ONTO STANDS WHERE INTO STANDS — after the projection, before FROM. That is the whole
		// point of the pair, and writing it at the end is a syntax error, not a second spelling.
		wxT("SELECT Partner, Amount ONTO Sales FROM Document.Sales;")
		wxT("SELECT S.Partner FROM Sales AS S INNER JOIN Document.Plan AS P ON P.Partner = S.Partner"));
	ASSERT_EQ(package.m_statements.size(), 2u);

	const ibQuerySelect* first = package.m_statements[0].m_select.get();
	ASSERT_TRUE(first != nullptr);
	EXPECT_EQ(first->m_ontoName, wxT("Sales"));

	// The reader keeps the NAME — resolving it into that statement's own select is the package's
	// job at execution, not the parser's. A parser that resolved it would have to know what the
	// statements around it are, which is exactly the coupling this language does not have.
	const ibQuerySelect* second = package.m_statements[1].m_select.get();
	ASSERT_TRUE(second != nullptr);
	ASSERT_EQ(second->m_from.m_name.size(), 1u);
	EXPECT_EQ(second->m_from.m_name.front(), wxT("Sales"));
	EXPECT_EQ(second->m_from.m_alias, wxT("S"));
	EXPECT_TRUE(second->m_from.m_subquery == nullptr);
	ASSERT_EQ(second->m_joins.size(), 1u);
}

// THE TWO CANNOT BOTH BE WRITTEN. An INTO statement returns no result, so naming that result names
// nothing — refused with a sentence rather than by quietly ignoring one of the two words.
TEST(QueryL4Parser, IntoAndOntoTogetherAreRefused)
{
	EXPECT_THROW(Parse(wxT("SELECT Client INTO Tmp ONTO Sales FROM Document.Sales")), ibBackendException);
}

// THE UNFOLD BELONGS TO THE FIELD, so a level may take one field through a hierarchy and the next
// one flat. What the ENGINE does with that combination is its own refusal (a hierarchy walks one
// parent chain) — the language reads it, which is what keeps the two answers separable.
TEST(QueryL4Parser, TotalsLevelFieldsKeepTheirOwnUnfold)
{
	auto sel = Parse(wxT("SELECT Amount FROM Document.Sales "
	                     "TOTALS SUM(Amount) BY (Store HIERARCHY, Partner)"));
	ASSERT_EQ(sel->m_totalsBy.size(), 1u);
	ASSERT_EQ(sel->m_totalsBy[0].m_fields.size(), 2u);
	EXPECT_EQ(sel->m_totalsBy[0].m_fields[0].m_unfold, ibQueryDimUnfold::Hierarchy);
	EXPECT_EQ(sel->m_totalsBy[0].m_fields[1].m_unfold, ibQueryDimUnfold::Elements);
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

// ===========================================================================
// TOP n — row limit on the SELECT core.
// ===========================================================================

TEST(QueryL4Parser, TopN_ParsedOnCore)
{
	auto sel = Parse(wxT("SELECT TOP 10 Code FROM Catalog.Products ORDER BY Code"));
	ASSERT_TRUE(sel != nullptr);
	EXPECT_EQ(sel->m_top, 10);
	ASSERT_EQ(sel->m_projections.size(), 1u);
}

TEST(QueryL4Parser, TopN_WithDistinct)
{
	auto sel = Parse(wxT("SELECT TOP 5 DISTINCT Code FROM Catalog.Products"));
	EXPECT_EQ(sel->m_top, 5);
	EXPECT_TRUE(sel->m_distinct);
}

TEST(QueryL4Parser, TopN_PerUnionBranch)
{
	auto sel = Parse(wxT("SELECT TOP 7 Code FROM Catalog.A UNION SELECT TOP 3 Code FROM Catalog.B"));
	EXPECT_EQ(sel->m_top, 7);
	ASSERT_EQ(sel->m_unions.size(), 1u);
	EXPECT_EQ(sel->m_unions[0]->m_top, 3);
}

TEST(QueryL4Parser, TopN_InSubquery)
{
	auto sel = Parse(wxT("SELECT Code FROM (SELECT TOP 100 Code FROM Catalog.Products) AS s"));
	EXPECT_EQ(sel->m_top, 0);
	ASSERT_TRUE(sel->m_from.m_subquery != nullptr);
	EXPECT_EQ(sel->m_from.m_subquery->m_top, 100);
}

// ===========================================================================
// UNION vs UNION ALL — the per-branch keep-duplicates flag
// ===========================================================================

TEST(QueryL4Parser, UnionAll_FlagPerBranch)
{
	auto sel = Parse(
		wxT("SELECT Code FROM Catalog.A UNION SELECT Code FROM Catalog.B UNION ALL SELECT Code FROM Catalog.C"));
	ASSERT_EQ(sel->m_unions.size(), 2u);
	EXPECT_FALSE(sel->m_unions[0]->m_unionAll);   // plain UNION — dedupes
	EXPECT_TRUE(sel->m_unions[1]->m_unionAll);    // UNION ALL — keeps duplicates
}

// ===========================================================================
// The RETURN TRIP — AST back to query text (queryRender.{h,cpp})
//
// A constructor that can only GENERATE text is used once: the moment a query is
// edited by hand it can no longer read it. These tests pin the property that
// makes it a tool instead — text -> AST -> text -> AST, with the second AST
// equal to the first.
//
// Equality is checked by RE-RENDERING, not by comparing structs: two renders
// that agree mean the second parse understood everything the first one did.
// Byte-identity with the ORIGINAL source is deliberately not the contract —
// comments, line breaks and redundant parentheses belong to the author and the
// AST does not keep them.
// ===========================================================================

#include "backend/query/queryRender.h"

namespace {

// text -> AST -> text -> AST -> text; the last two must agree.
void ExpectRoundTrip(const wxString& source)
{
	ibQuerySelectPtr first = Parse(source);
	ASSERT_NE(nullptr, first) << "source did not parse: " << source.ToStdString();

	const wxString once = ibRenderQuery(*first);

	ibQuerySelectPtr second = Parse(once);
	ASSERT_NE(nullptr, second) << "rendered text did not parse back:\n" << once.ToStdString();

	const wxString twice = ibRenderQuery(*second);
	EXPECT_EQ(once, twice)
		<< "round trip is not stable\nfirst:\n" << once.ToStdString()
		<< "\nsecond:\n" << twice.ToStdString();
}

} // namespace

TEST(QueryRender, PlainSelectRoundTrips) {
	ExpectRoundTrip(wxT("SELECT Ref, Description FROM Catalog.Products"));
}

TEST(QueryRender, StarAndAliasRoundTrip) {
	ExpectRoundTrip(wxT("SELECT * FROM Catalog.Products AS p"));
	ExpectRoundTrip(wxT("SELECT p.Ref AS link FROM Catalog.Products AS p"));
}

TEST(QueryRender, DistinctAndTopSurvive) {
	ExpectRoundTrip(wxT("SELECT TOP 10 DISTINCT Ref FROM Catalog.Products"));
}

TEST(QueryRender, PredicatePrecedenceIsPreserved) {
	// THE CASE THAT MATTERS. The AST has no parentheses — the author's are
	// consumed by the parser — so rendering without them would re-associate on
	// the next parse: `a AND (b OR c)` coming back as `a AND b OR c` is a
	// different query, and a silent one.
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products WHERE Code = \"1\" AND (Name = \"a\" OR Name = \"b\")"));
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products WHERE (Code = \"1\" OR Code = \"2\") AND Name = \"a\""));
}

TEST(QueryRender, PredicateFormsRoundTrip) {
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products WHERE Name LIKE \"a%\""));
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products WHERE Name NOT LIKE \"a%\""));
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products WHERE Code IN (\"1\", \"2\", \"3\")"));
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products WHERE Owner IS NULL"));
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products WHERE Owner IS NOT NULL"));
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products WHERE Price BETWEEN 10 AND 20"));
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products WHERE NOT (Code = \"1\")"));
}

TEST(QueryRender, ParametersAndAggregatesRoundTrip) {
	ExpectRoundTrip(wxT("SELECT SUM(Price) AS total FROM Catalog.Products WHERE Owner = &owner"));
	ExpectRoundTrip(wxT("SELECT COUNT(*) AS rows FROM Catalog.Products"));
}

TEST(QueryRender, JoinsRoundTrip) {
	ExpectRoundTrip(wxT("SELECT p.Ref FROM Catalog.Products AS p LEFT JOIN Catalog.Units AS u ON p.Unit = u.Ref"));
	// ⭐ A PRODUCT IS A COMMA. Two tables with nothing said about how they meet are MULTIPLIED, and
	// that is an ordinary, complete query written the way it has always been written.
	ExpectRoundTrip(wxT("SELECT p.Ref FROM Catalog.Products AS p, Catalog.Units AS u"));
}

// …and a JOIN always carries its ON. The omitted-ON shorthand used to mean "find the link yourself,
// one side probably references the other" — which made the same text mean different things depending
// on what the metadata held that day, AND took the syntax a product needed. One shape, one meaning.
TEST(QueryL4Parser, AJoinWithoutOnIsRefused) {
	EXPECT_THROW(Parse(wxT("SELECT p.Ref FROM Catalog.Products AS p INNER JOIN Catalog.Units AS u")),
	             ibBackendException);
}

TEST(QueryRender, GroupHavingOrderRoundTrip) {
	ExpectRoundTrip(wxT("SELECT Owner, SUM(Price) AS total FROM Catalog.Products ")
		wxT("GROUP BY Owner HAVING SUM(Price) > 100 ORDER BY Owner DESC"));
}

TEST(QueryRender, SubqueryAndUnionRoundTrip) {
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products WHERE Owner IN (SELECT Ref FROM Catalog.Owners)"));
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products UNION ALL SELECT Ref FROM Catalog.Archive"));
}

// A PROJECTION IS AN EXPRESSION. The constructor's expression editor writes any of them into a
// field, so the parser has to read any of them back — else the window produces text its own engine
// refuses, which is the one thing this arc must never do.
TEST(QueryL4Parser, SelectListTakesAPredicate)
{
	auto sel = Parse(wxT("SELECT DeletionMark IS NULL AS Deleted, Code FROM Catalog.Products"));
	ASSERT_EQ(sel->m_projections.size(), 2u);
	EXPECT_EQ(sel->m_projections[0].m_expr->m_kind, ibQueryAstExprKind::IsNull);
	EXPECT_EQ(sel->m_projections[0].m_alias, wxT("Deleted"));
	EXPECT_EQ(sel->m_projections[1].m_expr->m_kind, ibQueryAstExprKind::Column);
}

TEST(QueryRender, PredicateProjectionRoundTrips) {
	ExpectRoundTrip(wxT("SELECT DeletionMark IS NULL AS Deleted FROM Catalog.Products"));
	ExpectRoundTrip(wxT("SELECT Price > 100 AS Expensive, Code FROM Catalog.Products"));
}

TEST(QueryRender, CaseRoundTrips) {
	ExpectRoundTrip(wxT("SELECT CASE WHEN Price > 100 THEN \"big\" ELSE \"small\" END AS size FROM Catalog.Products"));
}

TEST(QueryRender, TotalsRoundTrip) {
	ExpectRoundTrip(wxT("SELECT Owner, Price FROM Catalog.Products TOTALS SUM(Price) BY Owner"));
	ExpectRoundTrip(wxT("SELECT Owner, Price FROM Catalog.Products TOTALS SUM(Price) BY Owner HIERARCHY"));
	// PERIODS survives with exactly as many arguments as were written — a bound left out means
	// "from the data", and spelling one back would be the renderer answering for the author.
	ExpectRoundTrip(wxT("SELECT Date, Price FROM Catalog.Products TOTALS SUM(Price) BY Date PERIODS(Month)"));
	ExpectRoundTrip(wxT("SELECT Date, Price FROM Catalog.Products "
	                    "TOTALS SUM(Price) BY Date PERIODS(Day, &From, &To) AS Period"));
	// ⭐ AN UPPER BOUND WITH NO LOWER ONE — "up to here, starting wherever the data does". The comma
	// holds the empty place, which is what the renderer writes and what the constructor's periodicity
	// panel composes; if this form did not parse, setting only a `To` in the form would be refused
	// while the same text hand-written would run.
	ExpectRoundTrip(wxT("SELECT Date, Price FROM Catalog.Products "
	                    "TOTALS SUM(Price) BY Date PERIODS(Month, , &To)"));
	// The level's NAME survives the trip — written with AS, so the render cannot be read back as a
	// second dimension.
	ExpectRoundTrip(wxT("SELECT Owner, Price FROM Catalog.Products TOTALS SUM(Price) BY Owner AS Seller"));
	ExpectRoundTrip(wxT("SELECT Owner, Price FROM Catalog.Products "
	                    "TOTALS SUM(Price) BY Owner HIERARCHY AS Seller, Code AS Article"));
}

TEST(QueryRender, ExpressionAloneRenders) {
	// For a constructor's filter row or a diagnostic naming the offending term.
	ibQuerySelectPtr select = Parse(wxT("SELECT Ref FROM Catalog.Products WHERE Code = \"1\""));
	ASSERT_NE(nullptr, select);
	ASSERT_NE(nullptr, select->m_where);
	EXPECT_EQ(wxT("Code = \"1\""), ibRenderQueryExpr(*select->m_where));
}

// ===========================================================================
//  The constructor's own surface — ALLOWED / FOR UPDATE / INTO / DROP, the
//  PACKAGE, and a lone expression parsed back.
//
//  Every one of these exists because a constructor tab edits it, and each is
//  pinned the same way the rest of the language is: what the renderer writes,
//  the parser reads. A constructor that emits a clause its own parser cannot
//  read back is the one failure mode that makes the tool abandoned.
// ===========================================================================

namespace {

ibQueryPackage ParsePkg(const wxString& text)
{
	ibQueryParser parser;
	return parser.ParsePackage(text);
}

// text -> package -> text -> package -> text; the last two must agree.
void ExpectPackageRoundTrip(const wxString& source)
{
	const ibQueryPackage first = ParsePkg(source);
	const wxString once = ibRenderQueryPackage(first);

	const ibQueryPackage second = ParsePkg(once);
	const wxString twice = ibRenderQueryPackage(second);

	EXPECT_EQ(once, twice)
		<< "package round trip is not stable\nfirst:\n" << once.ToStdString()
		<< "\nsecond:\n" << twice.ToStdString();
}

} // namespace

TEST(QueryL4Parser, AllowedIsParsedAndIsNotTheDefault)
{
	auto plain = Parse(wxT("SELECT Ref FROM Catalog.Products"));
	ASSERT_NE(nullptr, plain);
	EXPECT_FALSE(plain->m_allowed) << "ALLOWED must never be the default — a quiet refusal is a lie";

	auto allowed = Parse(wxT("SELECT ALLOWED Ref FROM Catalog.Products"));
	ASSERT_NE(nullptr, allowed);
	EXPECT_TRUE(allowed->m_allowed);
}

TEST(QueryL4Parser, AllowedComposesWithTopAndDistinct)
{
	auto sel = Parse(wxT("SELECT ALLOWED TOP 10 DISTINCT Ref FROM Catalog.Products"));
	ASSERT_NE(nullptr, sel);
	EXPECT_TRUE(sel->m_allowed);
	EXPECT_EQ(10, sel->m_top);
	EXPECT_TRUE(sel->m_distinct);
}

TEST(QueryL4Parser, ForUpdateIsParsedAndIsNotTheDefault)
{
	auto plain = Parse(wxT("SELECT Ref FROM Catalog.Products"));
	ASSERT_NE(nullptr, plain);
	EXPECT_FALSE(plain->m_forUpdate);

	auto locked = Parse(wxT("SELECT Ref FROM Catalog.Products WHERE Code = \"1\" FOR UPDATE"));
	ASSERT_NE(nullptr, locked);
	EXPECT_TRUE(locked->m_forUpdate);
}

TEST(QueryL4Parser, ForWithoutUpdateIsASyntaxError)
{
	EXPECT_THROW(Parse(wxT("SELECT Ref FROM Catalog.Products FOR")), ibBackendException);
}

TEST(QueryL4Parser, IntoNamesTheTempTable)
{
	auto sel = Parse(wxT("SELECT Ref, Price INTO Sales FROM Document.Orders"));
	ASSERT_NE(nullptr, sel);
	EXPECT_EQ(wxT("Sales"), sel->m_intoTemp);
	ASSERT_EQ(2u, sel->m_projections.size());
	ASSERT_EQ(2u, sel->m_from.m_name.size());
}

TEST(QueryL4Parser, IntoWithoutANameIsASyntaxError)
{
	EXPECT_THROW(Parse(wxT("SELECT Ref INTO FROM Document.Orders")), ibBackendException);
}

TEST(QueryL4Parser, ATempTableIsABareNameAsASource)
{
	// The point of INTO: the NEXT statement selects from that name with no <Kind>. prefix,
	// because a temp table has no metaclass — it is what the previous statement left.
	auto sel = Parse(wxT("SELECT Ref FROM Sales"));
	ASSERT_NE(nullptr, sel);
	ASSERT_EQ(1u, sel->m_from.m_name.size());
	EXPECT_EQ(wxT("Sales"), sel->m_from.m_name[0]);
}

TEST(QueryL4Parser, SingleStatementIsAPackageOfOne)
{
	const ibQueryPackage pkg = ParsePkg(wxT("SELECT Ref FROM Catalog.Products"));
	ASSERT_EQ(1u, pkg.m_statements.size());
	EXPECT_NE(nullptr, pkg.SingleSelect());
	EXPECT_FALSE(pkg.m_statements[0].IsDrop());
}

TEST(QueryL4Parser, EmptyTextIsAnEmptyPackageNotAnError)
{
	// The constructor opens on nothing and builds from nothing — the same road as
	// building from something, just with a shorter start.
	const ibQueryPackage pkg = ParsePkg(wxEmptyString);
	EXPECT_TRUE(pkg.m_statements.empty());
	EXPECT_EQ(nullptr, pkg.SingleSelect());
}

TEST(QueryL4Parser, PackageKeepsStatementsInWrittenOrder)
{
	const ibQueryPackage pkg = ParsePkg(
		wxT("SELECT Ref INTO Sales FROM Document.Orders;")
		wxT("SELECT Ref FROM Sales;")
		wxT("DROP Sales"));

	ASSERT_EQ(3u, pkg.m_statements.size());
	EXPECT_EQ(wxT("Sales"), pkg.m_statements[0].m_select->m_intoTemp);
	EXPECT_EQ(wxT("Sales"), pkg.m_statements[1].m_select->m_from.m_name[0]);
	EXPECT_TRUE(pkg.m_statements[2].IsDrop());
	EXPECT_EQ(wxT("Sales"), pkg.m_statements[2].m_dropTemp);

	// A package of several is NOT a single query — the single-statement door says so
	// rather than silently swallowing everything after the first ';'.
	EXPECT_EQ(nullptr, pkg.SingleSelect());
}

TEST(QueryL4Parser, SingleQueryDoorRefusesAPackage)
{
	EXPECT_THROW(Parse(wxT("SELECT Ref FROM Catalog.Products; SELECT Ref FROM Catalog.Goods")),
		ibBackendException);
}

TEST(QueryL4Parser, ATrailingSeparatorIsPunctuationNotAStatement)
{
	auto sel = Parse(wxT("SELECT Ref FROM Catalog.Products;"));
	EXPECT_NE(nullptr, sel);

	const ibQueryPackage pkg = ParsePkg(wxT("SELECT Ref FROM Catalog.Products;"));
	EXPECT_EQ(1u, pkg.m_statements.size());
}

TEST(QueryL4Parser, DropWithoutANameIsASyntaxError)
{
	EXPECT_THROW(ParsePkg(wxT("DROP")), ibBackendException);
}

TEST(QueryL4Parser, AnExpressionParsesOnItsOwn)
{
	// The constructor's "arbitrary condition" cell: a hand-typed predicate is read by
	// THIS parser, never by a hand-rolled checker beside it.
	ibQueryParser parser;
	ibQueryAstExprPtr expr = parser.ParseExpression(wxT("Price >= 100 AND Code LIKE \"A%\""));
	ASSERT_NE(nullptr, expr);
	EXPECT_EQ(ibQueryAstExprKind::Logical, expr->m_kind);
	EXPECT_FALSE(expr->m_isOr);
}

TEST(QueryL4Parser, AnEmptyExpressionIsNullNotAnError)
{
	ibQueryParser parser;
	EXPECT_EQ(nullptr, parser.ParseExpression(wxEmptyString));
}

TEST(QueryL4Parser, ABrokenExpressionThrowsFromTheEngine)
{
	ibQueryParser parser;
	EXPECT_THROW(parser.ParseExpression(wxT("Price >= ")), ibBackendException);
	EXPECT_THROW(parser.ParseExpression(wxT("Price 100")), ibBackendException);
}

TEST(QueryRender, AllowedAndForUpdateRoundTrip) {
	ExpectRoundTrip(wxT("SELECT ALLOWED Ref FROM Catalog.Products"));
	ExpectRoundTrip(wxT("SELECT ALLOWED TOP 5 DISTINCT Ref FROM Catalog.Products"));
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products WHERE Price > 10 FOR UPDATE"));
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products ORDER BY Ref ASC FOR UPDATE"));
}

TEST(QueryRender, IntoRoundTrips) {
	ExpectRoundTrip(wxT("SELECT Ref, Price INTO Sales FROM Document.Orders WHERE Price > 0"));
}

// ';' SEPARATES the statements of a package and does NOT terminate the last one — a query must not
// grow a semicolon it was never written with. A trailing one typed by hand is still READ.
TEST(QueryRender, PackageStatementsAreSemicolonSeparated)
{
	const wxString one = ibRenderQueryPackage(ParsePkg(wxT("SELECT Ref FROM Catalog.Products")));
	EXPECT_EQ(wxNOT_FOUND, one.Find(wxT(';')));

	const wxString many = ibRenderQueryPackage(ParsePkg(
		wxT("SELECT Ref INTO Tmp FROM Catalog.Products; SELECT Ref FROM Tmp")));
	EXPECT_EQ(1u, many.Freq(wxT(';')));
	EXPECT_FALSE(many.EndsWith(wxT(";")));

	// A trailing ';' on the way IN is accepted and simply not written back out.
	const wxString trailing = ibRenderQueryPackage(ParsePkg(wxT("SELECT Ref FROM Catalog.Products;")));
	EXPECT_EQ(wxNOT_FOUND, trailing.Find(wxT(';')));

	ExpectPackageRoundTrip(wxT("SELECT Ref INTO Tmp FROM Catalog.Products; SELECT Ref FROM Tmp"));
}

TEST(QueryRender, PackageRoundTrips) {
	ExpectPackageRoundTrip(
		wxT("SELECT Ref, Price INTO Sales FROM Document.Orders;")
		wxT("SELECT Ref FROM Sales WHERE Price > 100 ORDER BY Ref ASC;")
		wxT("DROP Sales"));
}

TEST(QueryRender, RenderedPackageTextParsesBackAsThatPackage) {
	// The whole contract in one assertion: the constructor hands its text to the
	// ENGINE's parser, and the engine reads back exactly the statements it wrote.
	ibQueryPackage built;
	{
		ibQueryAstStatement into;
		into.m_select = Parse(wxT("SELECT Ref INTO Sales FROM Document.Orders"));
		built.m_statements.push_back(into);

		ibQueryAstStatement read;
		read.m_select = Parse(wxT("SELECT ALLOWED Ref FROM Sales"));
		built.m_statements.push_back(read);

		ibQueryAstStatement drop;
		drop.m_dropTemp = wxT("Sales");
		built.m_statements.push_back(drop);
	}

	const ibQueryPackage reparsed = ParsePkg(ibRenderQueryPackage(built));
	ASSERT_EQ(3u, reparsed.m_statements.size());
	EXPECT_EQ(wxT("Sales"), reparsed.m_statements[0].m_select->m_intoTemp);
	EXPECT_TRUE(reparsed.m_statements[1].m_select->m_allowed);
	EXPECT_EQ(wxT("Sales"), reparsed.m_statements[2].m_dropTemp);
}

// ===========================================================================
//  Regressions found by auditing the arc of 2026-08-06 — every one is a clause
//  an EXISTING pass did not know about, which is the silent kind.
// ===========================================================================

TEST(QueryL4Parser, IntoInsideANestedTableIsRefused)
{
	// A nested query is not a statement. INTO materialises a PACKAGE's temp table, so inside a
	// source it would be a silent no-op — and a silent no-op in something written deliberately is
	// worse than a refusal.
	EXPECT_THROW(Parse(wxT("SELECT Code FROM (SELECT Code INTO t FROM Catalog.Products) AS x")),
		ibBackendException);
}

TEST(QueryL4Parser, ForUpdateInsideANestedTableIsRefused)
{
	EXPECT_THROW(Parse(wxT("SELECT Code FROM (SELECT Code FROM Catalog.Products FOR UPDATE) AS x")),
		ibBackendException);
}

TEST(QueryL4Parser, ANestedTableWithoutThoseWordsStillParses)
{
	// The refusal above must be about the two words, not about nested tables.
	auto sel = Parse(wxT("SELECT Code FROM (SELECT Code FROM Catalog.Products) AS x"));
	ASSERT_NE(nullptr, sel);
	EXPECT_NE(nullptr, sel->m_from.m_subquery);
}

// ===========================================================================
//  INDEX BY — the columns a materialised temp table is indexed by.
// ===========================================================================

TEST(QueryL4Parser, IndexByNamesTheColumns)
{
	auto sel = Parse(wxT("SELECT Ref, Code INTO Sales FROM Document.Orders INDEX BY Ref, Code"));
	ASSERT_NE(nullptr, sel);
	ASSERT_EQ(2u, sel->m_indexBy.size());
	EXPECT_EQ(wxT("Ref"),  sel->m_indexBy[0]->m_path.back());
	EXPECT_EQ(wxT("Code"), sel->m_indexBy[1]->m_path.back());
}

TEST(QueryL4Parser, IndexByWithoutIntoIsRefused)
{
	// An index over a table nobody is making could only describe something that does not exist —
	// a silent no-op, and the loudest kind of wrong in a query written for speed.
	EXPECT_THROW(Parse(wxT("SELECT Ref FROM Document.Orders INDEX BY Ref")), ibBackendException);
}

TEST(QueryL4Parser, IndexByNeedsBy)
{
	EXPECT_THROW(Parse(wxT("SELECT Ref INTO Sales FROM Document.Orders INDEX Ref")), ibBackendException);
}

TEST(QueryRender, IndexByRoundTrips) {
	ExpectRoundTrip(wxT("SELECT Ref, Code INTO Sales FROM Document.Orders INDEX BY Ref, Code"));
	ExpectRoundTrip(wxT("SELECT Ref INTO Sales FROM Document.Orders WHERE Code > 1 INDEX BY Ref"));
}

// ---------------------------------------------------------------------------
//  «IN HIERARCHY» — the same operator, told how far down to look
// ---------------------------------------------------------------------------
//
// The three words are the language's own (TOTALS BY unfolds a dimension by them), and this is their
// SECOND venue: a filter. What the parser has to get right is which of the three was written, and
// the one rule that separates this from a plain IN — the operand is a parameter, because a subtree
// is walked DOWN from values that have to be in hand.

TEST(QueryL4Parser, InWithoutAWordIsElements)
{
	auto sel = Parse(wxT("SELECT Ref FROM Catalog.Products WHERE Ref IN (&A, &B)"));
	ASSERT_NE(nullptr, sel->m_where);
	EXPECT_EQ(ibQueryAstExprKind::In, sel->m_where->m_kind);
	EXPECT_EQ(ibQueryDimUnfold::Elements, sel->m_where->m_unfold);
	EXPECT_EQ(2u, sel->m_where->m_list.size());
}

TEST(QueryL4Parser, InHierarchyCarriesTheWordAndOneParameter)
{
	auto sel = Parse(wxT("SELECT Ref FROM Catalog.Products WHERE Parent IN HIERARCHY (&Roots)"));
	ASSERT_NE(nullptr, sel->m_where);
	EXPECT_EQ(ibQueryAstExprKind::In, sel->m_where->m_kind);
	EXPECT_EQ(ibQueryDimUnfold::Hierarchy, sel->m_where->m_unfold);
	ASSERT_EQ(1u, sel->m_where->m_list.size());
	EXPECT_EQ(ibQueryAstExprKind::Param, sel->m_where->m_list[0]->m_kind);
	EXPECT_EQ(wxT("Roots"), sel->m_where->m_list[0]->m_paramName);
	EXPECT_EQ(nullptr, sel->m_where->m_subquery);
}

TEST(QueryL4Parser, InHierarchyOnlyIsItsOwnWord)
{
	auto sel = Parse(wxT("SELECT Ref FROM Catalog.Products WHERE Parent IN HIERARCHYONLY (&Roots)"));
	ASSERT_NE(nullptr, sel->m_where);
	EXPECT_EQ(ibQueryDimUnfold::HierarchyOnly, sel->m_where->m_unfold);
}

TEST(QueryL4Parser, NotInHierarchyKeepsBothTheNegationAndTheWord)
{
	auto sel = Parse(wxT("SELECT Ref FROM Catalog.Products WHERE Parent NOT IN HIERARCHY (&Roots)"));
	ASSERT_NE(nullptr, sel->m_where);
	EXPECT_EQ(ibQueryAstExprKind::In, sel->m_where->m_kind);
	EXPECT_TRUE(sel->m_where->m_negated);
	EXPECT_EQ(ibQueryDimUnfold::Hierarchy, sel->m_where->m_unfold);
}

TEST(QueryL4Parser, InHierarchyRefusesEverythingButAParameter)
{
	// A SUBQUERY is the interesting refusal: expanding a subtree means walking down FROM the values,
	// so they have to exist before the read starts — and a subquery does not until it runs. A literal
	// list and a comma-separated one are refused for the same reason, said once.
	EXPECT_THROW(Parse(wxT("SELECT Ref FROM Catalog.Products WHERE Parent IN HIERARCHY (SELECT Ref FROM Catalog.Products)")),
		ibBackendException);
	EXPECT_THROW(Parse(wxT("SELECT Ref FROM Catalog.Products WHERE Parent IN HIERARCHY (&A, &B)")),
		ibBackendException);
	EXPECT_THROW(Parse(wxT("SELECT Ref FROM Catalog.Products WHERE Parent IN HIERARCHY (1)")),
		ibBackendException);

	// …and a plain IN is untouched by that rule: it still takes a list and a subquery.
	EXPECT_NO_THROW(Parse(wxT("SELECT Ref FROM Catalog.Products WHERE Parent IN (SELECT Ref FROM Catalog.Products)")));
	EXPECT_NO_THROW(Parse(wxT("SELECT Ref FROM Catalog.Products WHERE Parent IN (1, 2)")));
}

TEST(QueryRender, InHierarchyRoundTrips) {
	// The word is part of the OPERATOR, so a query that had it comes back with it — a constructor
	// that dropped it would silently widen the filter to the whole chart.
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products WHERE Parent IN HIERARCHY (&Roots)"));
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products WHERE Parent IN HIERARCHYONLY (&Roots)"));
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products WHERE Parent NOT IN HIERARCHY (&Roots)"));
	ExpectRoundTrip(wxT("SELECT Ref FROM Catalog.Products WHERE Parent IN (&A, &B)"));
}

// --- windows: `<call> OVER (…)` ---------------------------------------------
//
// The grammar reads them; the lowering does not run them yet (it refuses, loudly). These pin the
// SHAPE the parser produces, so the road that executes it later is built against a fixed target —
// and pin the four refusals, each of which exists because the silent alternative is worse.

TEST(QueryL4Parser, Window_PartitionOrderAndFrame)
{
	auto sel = Parse(wxT("SELECT Region, SUM(Amount) OVER (PARTITION BY Region ORDER BY Period DESC RANGE) AS Running "
	                     "FROM Document.Sales"));
	ASSERT_TRUE(sel != nullptr);
	ASSERT_EQ(sel->m_projections.size(), 2u);

	const ibQueryAstExpr& call = *sel->m_projections[1].m_expr;
	ASSERT_EQ(call.m_kind, ibQueryAstExprKind::Func);
	ASSERT_TRUE(call.m_over != nullptr);
	ASSERT_EQ(call.m_over->m_partitionBy.size(), 1u);
	ASSERT_EQ(call.m_over->m_orderBy.size(), 1u);
	EXPECT_FALSE(call.m_over->m_orderBy[0].m_ascending);          // DESC carried
	EXPECT_EQ(call.m_over->m_frame, ibQueryAstFrame::Range);
	EXPECT_EQ(sel->m_projections[1].m_alias, wxT("Running"));
}

// No OVER at all = an ordinary aggregate. The common case must stay untouched by the suffix parser.
TEST(QueryL4Parser, Window_AbsentLeavesAPlainAggregate)
{
	auto sel = Parse(wxT("SELECT SUM(Amount) FROM Document.Sales"));
	ASSERT_TRUE(sel != nullptr);
	ASSERT_EQ(sel->m_projections.size(), 1u);
	EXPECT_TRUE(sel->m_projections[0].m_expr->m_over == nullptr);
}

// An unordered partition is legal and frameless — it is the denominator of a share-of-total.
TEST(QueryL4Parser, Window_PartitionOnlyIsTheGroupTotal)
{
	auto sel = Parse(wxT("SELECT SUM(Amount) OVER (PARTITION BY Region) FROM Document.Sales"));
	ASSERT_TRUE(sel != nullptr);
	const ibQueryAstExpr& call = *sel->m_projections[0].m_expr;
	ASSERT_TRUE(call.m_over != nullptr);
	EXPECT_TRUE(call.m_over->m_orderBy.empty());
	EXPECT_EQ(call.m_over->m_frame, ibQueryAstFrame::Unstated);
}

TEST(QueryL4Parser, Window_RankingCall)
{
	auto sel = Parse(wxT("SELECT ROW_NUMBER() OVER (PARTITION BY Region ORDER BY Amount DESC) AS rn "
	                     "FROM Document.Sales"));
	ASSERT_TRUE(sel != nullptr);
	const ibQueryAstExpr& call = *sel->m_projections[0].m_expr;
	EXPECT_EQ(call.m_func, ibQueryKeyword::RowNumber);
	EXPECT_FALSE(call.m_star);                                    // no argument, and no star either
	ASSERT_TRUE(call.m_over != nullptr);
	EXPECT_EQ(call.m_over->m_frame, ibQueryAstFrame::Unstated);   // a ranking call has no frame
}

// A ranking call without OVER numbers rows within nothing — refused where the rule lives, not two
// layers down as "cannot prepare statement".
TEST(QueryL4Parser, Window_RankingWithoutOverIsRefused)
{
	EXPECT_THROW(Parse(wxT("SELECT ROW_NUMBER() FROM Document.Sales")), ibBackendException);
}

// ⭐⭐ THE ARGUMENT OF A RANKING CALL IS ITS KEY — `RANK(Amount DESC)` (Max, 2026-08-27: *"can we do
// without OVER and ORDER BY altogether?"*). A rank needs an order or it is first at nothing; written
// the SQL way that order lives inside `OVER (…)` and the author spells two clauses to say one thing,
// while the AREA is already stated beside the cell. So the short form says the key and the engine
// assembles the window out of the two halves.
//
// ⛔ THIS TEST PINNED THE OPPOSITE — that an argument is REFUSED — which was the rule until the short
// form was built, later the same day. It went on asserting the old one and CI failed on the arc that
// changed the language. A test that survives the rule it was written for is not a regression guard;
// it is a second opinion about what the language says.
TEST(QueryL4Parser, Window_RankingTakesItsKeyAsTheArgument)
{
	auto sel = Parse(wxT("SELECT RANK(Amount DESC) FROM Document.Sales"));
	ASSERT_TRUE(sel != nullptr);
	const ibQueryAstExpr& call = *sel->m_projections[0].m_expr;
	EXPECT_EQ(call.m_func, ibQueryKeyword::Rank);
	ASSERT_TRUE(call.m_over != nullptr);
	ASSERT_EQ(call.m_over->m_orderBy.size(), 1u);
	EXPECT_FALSE(call.m_over->m_orderBy[0].m_ascending);
	EXPECT_EQ(call.m_over->m_frame, ibQueryAstFrame::Unstated);   // a ranking call has no frame

	// …AND THE LONG FORM STILL COMPOSES WITH IT: an explicit OVER may add the partition, and when it
	// states no order of its own the argument's key is kept.
	auto both = Parse(wxT("SELECT RANK(Amount) OVER (PARTITION BY Region) FROM Document.Sales"));
	ASSERT_TRUE(both != nullptr);
	const ibQueryAstExpr& call2 = *both->m_projections[0].m_expr;
	ASSERT_TRUE(call2.m_over != nullptr);
	ASSERT_EQ(call2.m_over->m_orderBy.size(), 1u);
	EXPECT_EQ(call2.m_over->m_partitionBy.size(), 1u);
}

// …and what a ranking call still refuses: a FRAME. It numbers rows, it does not fold them.
TEST(QueryL4Parser, Window_RankingWithAFrameIsRefused)
{
	EXPECT_THROW(Parse(wxT("SELECT RANK(Amount) OVER (ORDER BY Amount ROWS) FROM Document.Sales")),
	             ibBackendException);
}

// A frame is a position in an ORDER; without one it is about nothing, and quietly ignoring it is how
// a reader comes to believe the query says something it does not.
TEST(QueryL4Parser, Window_FrameWithoutOrderIsRefused)
{
	EXPECT_THROW(Parse(wxT("SELECT SUM(Amount) OVER (PARTITION BY Region ROWS) FROM Document.Sales")),
	             ibBackendException);
}

// ===========================================================================
//  A PACKAGE'S OWN LINKS between its named selections (2026-08-21)
// ===========================================================================

// ⭐ Max's shape, in his words: mark two statements as named selections and set the links between
// them — nothing else. So the link is not a statement and lives in no statement: it is the
// PACKAGE's, and the text spells it where a statement stands.
//
// ⭐⭐ AND IT IS SPELLED `LINK <name> JOIN <name> ON …` (Max, 2026-08-27). The first form was
// recognised by POSITION with no keyword at all — a bare top-level `JOIN A AND B ON …` — to avoid
// taking the word away from configurations with an attribute called `Link`. At the START OF A
// STATEMENT no name can stand, so nothing was ever at stake; and the word pays for itself by
// letting the relation be written the way this language writes every relation, `AND` and all its
// second spelling gone.
TEST(QueryL4Parser, PackageLinkRelatesTwoNamedSelections)
{
	auto package = ibQueryParser().ParsePackage(
		wxT("SELECT Partner, Amount ONTO Sales FROM Document.Sales;")
		wxT("SELECT Partner, Paid ONTO Payments FROM Document.Payments;")
		wxT("LINK Sales JOIN Payments ON Sales.Partner = Payments.Partner"));

	// The two statements stayed statements — the link did not become one of them…
	ASSERT_EQ(package.m_statements.size(), 2u);
	ASSERT_EQ(package.m_links.size(), 1u);

	// …and nothing was pushed into either query: both read exactly what they were written to read.
	EXPECT_TRUE(package.m_statements[0].m_select->m_joins.empty());
	EXPECT_TRUE(package.m_statements[1].m_select->m_joins.empty());

	const ibQueryPackageLink& link = package.m_links.front();
	EXPECT_EQ(link.m_left,  wxT("Sales"));
	EXPECT_EQ(link.m_right, wxT("Payments"));
	EXPECT_EQ(link.m_kind,  ibQueryJoinKindAst::Inner);
	ASSERT_NE(link.m_on, nullptr);

	// AND IT COMES BACK OUT THE WAY IT WENT IN — the same contract as everything else in this language.
	const wxString written = ibRenderQueryPackage(package);
	// The layout is the renderer's (the head once, a line per relation); what this test pins is that
	// the link is THERE and reads back as the same link.
	EXPECT_TRUE(written.Contains(wxT("LINK Sales"))) << written.ToStdString();
	EXPECT_TRUE(written.Contains(wxT("JOIN Payments"))) << written.ToStdString();
	const ibQueryPackage again = ibQueryParser().ParsePackage(written);
	ASSERT_EQ(again.m_links.size(), 1u);
	EXPECT_EQ(again.m_links.front().m_left,  wxT("Sales"));
	EXPECT_EQ(again.m_links.front().m_right, wxT("Payments"));
}

// THE KIND IS SPELLED AS IT IS INSIDE A QUERY, because it means the same thing: which side keeps
// its rows when the other has none.
TEST(QueryL4Parser, PackageLinkCarriesItsJoinKind)
{
	auto package = ibQueryParser().ParsePackage(
		wxT("SELECT Partner ONTO Sales FROM Document.Sales;")
		wxT("SELECT Partner ONTO Plan FROM Document.Plan;")
		wxT("LINK Sales LEFT JOIN Plan ON Sales.Partner = Plan.Partner"));
	ASSERT_EQ(package.m_links.size(), 1u);
	EXPECT_EQ(package.m_links.front().m_kind, ibQueryJoinKindAst::Left);

	// Rendered back with the same word, and read back as the same kind.
	const wxString written = ibRenderQueryPackage(package);
	EXPECT_TRUE(written.Contains(wxT("LEFT JOIN Plan"))) << written.ToStdString();
	EXPECT_EQ(ibQueryParser().ParsePackage(written).m_links.front().m_kind, ibQueryJoinKindAst::Left);
}

// ⭐⭐ A CHAIN — one head, several relations, which is what the word in front made writable: `LINK A
// LEFT JOIN B ON … JOIN C ON …` is the FROM tree of the package's final query, written by the
// author instead of assembled from a list of pairs.
//
// It is FLATTENED into pairs, and nothing is lost by that: the lowering builds the final FROM out of
// the pairs, placing each where a side of it is already present. The head is carried as the left of
// every link in the chain, so a later condition may name any selection already placed — here the
// third relates to the SECOND, and the package still joins as one.
TEST(QueryL4Parser, PackageLinkChainsOffOneHead)
{
	const wxString text =
		wxT("SELECT Item ONTO Sales FROM Document.Sales;")
		wxT("SELECT Item ONTO Plan FROM Document.Plan;")
		wxT("SELECT Item ONTO Stock FROM AccumulationRegister.Stock;")
		wxT("LINK Sales LEFT JOIN Plan ON Sales.Item = Plan.Item")
		wxT("           JOIN Stock ON Stock.Item = Plan.Item");

	auto package = ibQueryParser().ParsePackage(text);
	ASSERT_EQ(package.m_statements.size(), 3u);
	ASSERT_EQ(package.m_links.size(), 2u);

	EXPECT_EQ(package.m_links[0].m_left,  wxT("Sales"));
	EXPECT_EQ(package.m_links[0].m_right, wxT("Plan"));
	EXPECT_EQ(package.m_links[0].m_kind,  ibQueryJoinKindAst::Left);
	EXPECT_EQ(package.m_links[1].m_left,  wxT("Sales"));   // the HEAD, not the previous right
	EXPECT_EQ(package.m_links[1].m_right, wxT("Stock"));
	EXPECT_EQ(package.m_links[1].m_kind,  ibQueryJoinKindAst::Inner);

	// ONE SECTION COMES BACK OUT — a chain is written as a chain, not as two LINKs off the same name.
	const wxString written = ibRenderQueryPackage(package);
	EXPECT_TRUE(written.Contains(wxT("LINK Sales")))     << written.ToStdString();
	EXPECT_TRUE(written.Contains(wxT("LEFT JOIN Plan"))) << written.ToStdString();
	EXPECT_TRUE(written.Contains(wxT("JOIN Stock")))     << written.ToStdString();
	EXPECT_EQ(written.find(wxT("LINK ")), written.rfind(wxT("LINK "))) << written.ToStdString();
	const ibQueryPackage again = ibQueryParser().ParsePackage(written);
	ASSERT_EQ(again.m_links.size(), 2u);
	EXPECT_EQ(again.m_links[1].m_right, wxT("Stock"));
}

// A LINK NAMES TWO SELECTIONS AND SAYS HOW THEY MEET — all three parts are required in TEXT. A
// package that says two selections are related without saying how says nothing the parser can read.
TEST(QueryL4Parser, PackageLinkWithoutAConditionIsRefused)
{
	EXPECT_THROW(ibQueryParser().ParsePackage(
		wxT("SELECT Partner ONTO Sales FROM Document.Sales;")
		wxT("LINK Sales JOIN Payments")), ibBackendException);
	EXPECT_THROW(ibQueryParser().ParsePackage(
		wxT("SELECT Partner ONTO Sales FROM Document.Sales;")
		wxT("LINK Sales ON Sales.Partner = Payments.Partner")), ibBackendException);
}

// ⚠ AND THE FORM THIS ONE REPLACED IS REFUSED WHERE IT STANDS. A bare top-level JOIN was the first
// spelling of a package link (2026-08-21); read on today it would be a statement beginning with
// JOIN, and the complaint would be about a missing SELECT — true, and no help at all to whoever
// wrote the old form. The message says what to write instead.
TEST(QueryL4Parser, TheOldBarePackageJoinIsRefusedWithASentence)
{
	try {
		ibQueryParser().ParsePackage(
			wxT("SELECT Partner ONTO Sales FROM Document.Sales;")
			wxT("JOIN Sales AND Payments ON Sales.Partner = Payments.Partner"));
		FAIL() << "the old package-link form parsed";
	}
	catch (const ibBackendException& err) {
		EXPECT_TRUE(err.GetErrorDescription().Contains(wxT("LINK")))
			<< err.GetErrorDescription().ToStdString();
	}
}

// ⭐⭐ `SPLIT` — WHERE THE LADDER OF LEVELS STOPS BEING ONE.
//
// The levels before it are common; each SPLIT opens a branch that folds the SAME rows its own way
// (Max, 2026-08-27: common totals by item, one grouping going off by characteristic and another by
// series, each with a selection of its own). The comma keeps its old meaning exactly, which is
// what lets every query written before this parse unchanged.
TEST(QueryL4Parser, TotalsSplitOpensBranches)
{
	auto sel = Parse(wxT("SELECT Item, Amount FROM Document.Sales "
	                     "TOTALS SUM(Amount) BY Item "
	                     "SPLIT ByCharacteristic BY Characteristic "
	                     "SPLIT BySeries BY Series, Store"));
	// THE HIDDEN NODE keeps what was written before the first SPLIT — untouched, and the only thing
	// a report without SPLIT ever has.
	ASSERT_EQ(sel->m_totalsBy.size(), 1u);
	EXPECT_EQ(sel->m_totalsBy[0].Head()->m_expr->m_path[0], wxT("Item"));

	// …and each SPLIT is a VISIBLE node of its own, named where it is opened and carrying its own ladder.
	ASSERT_EQ(sel->m_totalsSplits.size(), 2u);
	EXPECT_EQ(sel->m_totalsSplits[0].m_name, wxT("ByCharacteristic"));
	ASSERT_EQ(sel->m_totalsSplits[0].m_levels.size(), 1u);
	EXPECT_EQ(sel->m_totalsSplits[0].m_levels[0].Head()->m_expr->m_path[0], wxT("Characteristic"));

	// A COMMA INSIDE A NODE IS STILL A COMMA — `Store` continues the ladder `Series` began rather
	// than opening a third node.
	EXPECT_EQ(sel->m_totalsSplits[1].m_name, wxT("BySeries"));
	ASSERT_EQ(sel->m_totalsSplits[1].m_levels.size(), 2u);
	EXPECT_EQ(sel->m_totalsSplits[1].m_levels[0].Head()->m_expr->m_path[0], wxT("Series"));
	EXPECT_EQ(sel->m_totalsSplits[1].m_levels[1].Head()->m_expr->m_path[0], wxT("Store"));
}

// A QUERY MAY OPEN ONE AT ONCE — nothing on the hidden node, every visible node hanging off the
// grand total.
TEST(QueryL4Parser, TotalsSplitWithNothingInCommon)
{
	auto sel = Parse(wxT("SELECT Item, Amount FROM Document.Sales "
	                     "TOTALS SUM(Amount) BY SPLIT BY Item SPLIT BY Store"));
	EXPECT_TRUE(sel->m_totalsBy.empty());            // the hidden node carries nothing
	ASSERT_EQ(sel->m_totalsSplits.size(), 2u);
	EXPECT_EQ(sel->m_totalsSplits[0].m_levels.size(), 1u);
	EXPECT_EQ(sel->m_totalsSplits[1].m_levels.size(), 1u);
}

// ⭐ THE WORD SURVIVES THE ROUND TRIP — and this is the half that was forgotten when PERIODS went
// into the language: a cell that shows less than it edits loses the rest on the first edit.
TEST(QueryL4Parser, TotalsSplitRoundTrips)
{
	const wxString text = wxT("SELECT Item, Amount FROM Document.Sales "
	                          "TOTALS SUM(Amount) BY Item "
	                          "SPLIT ByCharacteristic BY Characteristic "
	                          "SPLIT BySeries BY Series");
	const wxString written = ibRenderQuery(*Parse(text));
	EXPECT_TRUE(written.Contains(wxT("SPLIT"))) << written.ToStdString();
	EXPECT_TRUE(written.Contains(wxT("SPLIT ByCharacteristic BY"))) << written.ToStdString();

	// …and read back the same, which is the property the constructor rests on: what it shows is
	// what it edits.
	auto again = Parse(written);
	ASSERT_EQ(again->m_totalsBy.size(), 1u);          // the hidden node
	ASSERT_EQ(again->m_totalsSplits.size(), 2u);      // …and the two visible ones
	EXPECT_EQ(again->m_totalsSplits[0].m_name, wxT("ByCharacteristic"));
	EXPECT_EQ(again->m_totalsSplits[1].m_name, wxT("BySeries"));
	EXPECT_EQ(ibRenderQuery(*again), written);   // a second trip changes nothing
}

// ⭐ A NODE MAY BE LEFT UNNAMED — `SPLIT BY …`, read by position. `BY` straight after the word is
// what says the name was left out, so the two forms need nothing but the next token to tell apart.
TEST(QueryL4Parser, TotalsSplitWithoutAName)
{
	auto sel = Parse(wxT("SELECT Item, Amount FROM Document.Sales "
	                     "TOTALS SUM(Amount) BY Item SPLIT BY Characteristic"));
	ASSERT_EQ(sel->m_totalsSplits.size(), 1u);
	EXPECT_TRUE(sel->m_totalsSplits[0].m_name.IsEmpty());
	ASSERT_EQ(sel->m_totalsSplits[0].m_levels.size(), 1u);
	EXPECT_EQ(sel->m_totalsSplits[0].m_levels[0].Head()->m_expr->m_path[0], wxT("Characteristic"));

	// …and it round-trips as it was written, name absent and all.
	const wxString written = ibRenderQuery(*sel);
	EXPECT_TRUE(written.Contains(wxT("SPLIT BY"))) << written.ToStdString();
	EXPECT_EQ(ibRenderQuery(*Parse(written)), written);
}

// `SPLIT` NEEDS ITS `BY`. The word opens a node and the ladder follows it; without the keyword there
// is nothing to say where the name ended and the groupings began.
TEST(QueryL4Parser, TotalsSplitWithoutByIsRefused)
{
	EXPECT_THROW(Parse(wxT("SELECT Item FROM Document.Sales TOTALS SUM(Amount) BY Item SPLIT Characteristic")),
	             ibBackendException);
}

// ============================================================================================
// ⭐⭐ `TOTALS SUM(x) OVER <level>` — the figure that belongs to ONE grouping (§27)
//
// This is our answer to "evaluate this expression in the context of a grouping": not a second
// language inside a string, but an ordinary aggregate whose AREA is named. It reads after the call
// and before the name, because it is part of what the figure IS rather than of what it is called.
// ============================================================================================

TEST(QueryL4Parser, TotalsAggregateOverALevel)
{
	auto sel = Parse(wxT("SELECT Item, Warehouse, Amount FROM Document.Sales "
	                     "TOTALS SUM(Amount), SUM(Amount) OVER Item AS InItem BY Item, Warehouse"));
	ASSERT_EQ(sel->m_totalsAggregates.size(), 2u);
	EXPECT_TRUE(sel->m_totalsAggregates[0].m_scope.IsEmpty());   // area from the ladder
	EXPECT_EQ(sel->m_totalsAggregates[1].m_scope, wxT("Item"));
	EXPECT_EQ(sel->m_totalsAggregates[1].m_alias, wxT("InItem"));
}

// A BRANCH QUALIFIES THE LEVEL, where two of them carry a level of the same name — the role a table
// name plays before a column. The branch alone is not an area: SPLIT does not narrow the rows.
TEST(QueryL4Parser, TotalsAggregateOverAQualifiedLevel)
{
	auto sel = Parse(wxT("SELECT Item, Amount FROM Document.Sales "
	                     "TOTALS SUM(Amount) OVER ByCharacteristic.Characteristic BY Item "
	                     "SPLIT ByCharacteristic BY Characteristic "
	                     "SPLIT BySeries BY Series"));
	ASSERT_EQ(sel->m_totalsAggregates.size(), 1u);
	EXPECT_EQ(sel->m_totalsAggregates[0].m_scope, wxT("ByCharacteristic.Characteristic"));
}

// …AND IT SURVIVES THE TEXT. The constructor edits what the render writes, so an area dropped on the
// way out would silently turn every such figure back into a ladder aggregate — a query that runs and
// answers a different question.
TEST(QueryL4Parser, TotalsAggregateOverRoundTrips)
{
	const wxString source = wxT("SELECT Item, Warehouse, Amount FROM Document.Sales "
	                            "TOTALS SUM(Amount), SUM(Amount) OVER Item AS InItem "
	                            "BY Item, Warehouse");
	const wxString written = ibRenderQuery(*Parse(source));
	EXPECT_TRUE(written.Contains(wxT("OVER Item"))) << written.ToStdString();

	auto again = Parse(written);
	ASSERT_EQ(again->m_totalsAggregates.size(), 2u);
	EXPECT_EQ(again->m_totalsAggregates[1].m_scope, wxT("Item"));
	EXPECT_EQ(again->m_totalsAggregates[1].m_alias, wxT("InItem"));
	EXPECT_EQ(ibRenderQuery(*again), written);   // a second trip changes nothing
}

// ⚠ `OVER` HERE TAKES A NAME, NEVER A BRACKET. A window partitions ROWS and lives in the SELECT;
// TOTALS runs after the ladder and folds NODES, so what follows is the name of a level. Refused
// where it is written rather than lowered into something plausible.
TEST(QueryL4Parser, TotalsAggregateOverAPartitionIsRefused)
{
	EXPECT_THROW(Parse(wxT("SELECT Item, Amount FROM Document.Sales "
	                       "TOTALS SUM(Amount) OVER (PARTITION BY Item) BY Item")),
	             ibBackendException);
}
