// What the 2026-08-07 pass landed, pinned. Pure: no database, no session, no window.
//
// Everything here was verified by RUNNING the constructor and nothing else, which is exactly the
// state in which a quiet regression is found from a screenshot weeks later. Each test below stands
// for a defect that was actually shipped and actually reported:
//
//   * a walk named by its leaf, so `PredefinedName` and `Reference.PredefinedName` collided;
//   * a name computed and then not written down, so everything downstream used the old one;
//   * a composer that REFUSED settings over an author's query;
//   * `SELECT * INTO Tmp` yielding a temp table with no fields;
//   * settings left pointing at a field the source no longer has.

#include <gtest/gtest.h>

#include "backend/query/queryAst.h"
#include "backend/query/queryParser.h"
#include "backend/query/queryRender.h"
#include "backend/query/queryRewrite.h"
#include "backend/query/queryLowering.h"
#include "backend/query/queryConstructorModel.h"
#include "backend/composition/dataComposer.h"

namespace {

ibQuerySelectPtr Parse(const wxString& text)
{
	ibQueryParser parser;
	return parser.Parse(text);
}

// The projection a constructor would ADD: a column path, no alias — the state every naming rule is
// about, because a projection that already carries an alias is not the engine's business.
ibQueryProjection Column(const wxString& dottedPath)
{
	ibQueryProjection projection;
	projection.m_expr = ibQueryColumnFromPath(dottedPath);
	return projection;
}

} // namespace

// ===========================================================================
//  The name a new column is given
// ===========================================================================

TEST(QueryNaming, AWalkIsNamedByItsWholePathWithTheDotsTakenOut)
{
	// THE DEFECT: every step of a walk ends in the same word. Named by the leaf, `Parent.Parent`
	// and `Parent` both asked to be called `Parent` — which the engine refuses as duplicate output
	// names, and which the constructor numbered into names saying nothing about which walk they are.
	const ibQuerySelectPtr select = Parse(wxT("SELECT Code FROM Catalog.Products AS Products"));
	ASSERT_NE(nullptr, select);

	EXPECT_EQ(wxT("ParentParentParent"),
		ibQueryProposedName(*select, Column(wxT("Parent.Parent.Parent"))));
	EXPECT_EQ(wxT("ReferencePredefinedName"),
		ibQueryProposedName(*select, Column(wxT("Products.Reference.PredefinedName"))));
}

TEST(QueryNaming, ALeadingSourceNameIsNotPartOfTheName)
{
	// The qualifier says WHICH TABLE — a fact about where the column is read from, not about what
	// it is. Gluing it in would put the table's name in front of every single column.
	const ibQuerySelectPtr select = Parse(wxT("SELECT Code FROM Catalog.Products AS Products"));
	ASSERT_NE(nullptr, select);

	EXPECT_EQ(wxT("Code"), ibQueryProposedName(*select, Column(wxT("Products.Code"))));
	// …and a first segment that is NOT a source is the first hop of a walk, so it stays.
	EXPECT_EQ(wxT("OwnerCode"), ibQueryProposedName(*select, Column(wxT("Owner.Code"))));
}

TEST(QueryNaming, AnAuthorsAliasIsNeverOverridden)
{
	const ibQuerySelectPtr select = Parse(wxT("SELECT Code FROM Catalog.Products AS Products"));
	ASSERT_NE(nullptr, select);

	ibQueryProjection projection = Column(wxT("Products.Reference.PredefinedName"));
	projection.m_alias = wxT("Supplier");
	EXPECT_EQ(wxT("Supplier"), ibQueryProposedName(*select, projection));
}

TEST(QueryNaming, ANameThatIsNotTheNaturalOneIsWrittenDown)
{
	// ⚠ THE DEFECT, exactly: the name was computed and not stored. EnsureUniqueName only assigned
	// an alias on a COLLISION, and "the right name differs from the natural one" is not a collision
	// — so the text kept `Products.Reference.PredefinedName` with no AS, and the field map, the
	// check and the result all went on calling it `PredefinedName`.
	const ibQuerySelectPtr select = Parse(wxT("SELECT Code FROM Catalog.Products AS Products"));
	ASSERT_NE(nullptr, select);

	ibQueryProjection projection = Column(wxT("Products.Reference.PredefinedName"));
	ibQueryEnsureUniqueName(*select, projection);
	EXPECT_EQ(wxT("ReferencePredefinedName"), projection.m_alias);

	// A plain column's proposed name IS its natural one, so it needs no alias — writing one there
	// would fill every query with `AS` clauses that say nothing.
	ibQueryProjection plain = Column(wxT("Products.Description"));
	ibQueryEnsureUniqueName(*select, plain);
	EXPECT_TRUE(plain.m_alias.IsEmpty());
}

TEST(QueryNaming, ADuplicateIsNumberedAndTheProjectionIsNotItsOwnRival)
{
	ibQuerySelectPtr select = Parse(wxT("SELECT Code FROM Catalog.Products AS Products"));
	ASSERT_NE(nullptr, select);
	select->m_projections.clear();
	select->m_projections.push_back(Column(wxT("Products.Code")));
	select->m_projections.push_back(Column(wxT("Products.Code")));

	ibQueryEnsureUniqueName(*select, select->m_projections[1]);
	EXPECT_EQ(wxT("Code1"), select->m_projections[1].m_alias);

	// ⚠ Called again on a projection ALREADY in the list, a plain scan finds the projection itself,
	// decides its own name is taken and renumbers it — every time. It must not.
	ibQueryEnsureUniqueName(*select, select->m_projections[1]);
	EXPECT_EQ(wxT("Code1"), select->m_projections[1].m_alias);
}

TEST(QueryNaming, ASourceAnswersToItsAliasElseTheLastSegmentOfItsPath)
{
	// The rule that was written out eight times before it had a home. Everything qualified is
	// written against this, and the alias numbering compares against it.
	const ibQuerySelectPtr aliased = Parse(wxT("SELECT Code FROM Catalog.Products AS p"));
	ASSERT_NE(nullptr, aliased);
	EXPECT_EQ(wxT("p"), ibQuerySourceName(aliased->m_from));

	const ibQuerySelectPtr bare = Parse(wxT("SELECT Code FROM Catalog.Products"));
	ASSERT_NE(nullptr, bare);
	EXPECT_EQ(wxT("Products"), ibQuerySourceName(bare->m_from));
}

// ===========================================================================
//  Settings over an author's query
// ===========================================================================

TEST(QueryComposerAuthorText, AnAuthorsQueryWithNothingAskedOfItIsVerbatim)
{
	ibDataDBComposer composer;
	composer.FromText(wxT("SELECT Code FROM Catalog.Products"));
	EXPECT_EQ(wxT("SELECT Code FROM Catalog.Products"), composer.RenderText());
}

TEST(QueryComposerAuthorText, SettingsWrapItRatherThanEditingIt)
{
	// ⚠ THE DEFECT: this used to RAISE — "settings over an author's query text are not supported
	// yet" — so a filter, a sort or a grouping over an arbitrary query failed outright.
	//
	// The author's text is not edited, because a WHERE injected INTO it would run before their own
	// aggregates, their DISTINCT and their TOP, and would answer a different question than the one
	// typed into the filter.
	ibDataDBComposer composer;
	composer.FromText(wxT("SELECT Code, Name FROM Catalog.Products"));
	composer.Sort(wxT("Code"));

	const wxString text = composer.RenderText();
	EXPECT_TRUE(text.Contains(wxT("(SELECT Code, Name FROM Catalog.Products)")))
		<< "the author's query must appear whole, as a nested source: " << text;
	EXPECT_TRUE(text.Contains(ibDataDBComposer::AuthorQuerySourceName()));
	EXPECT_TRUE(text.Contains(wxT("ORDER BY")));

	// AND WHAT IT WRITES, THE PARSER READS — the same contract as everything else in this language.
	const ibQuerySelectPtr wrapped = Parse(text);
	ASSERT_NE(nullptr, wrapped);
	ASSERT_NE(nullptr, wrapped->m_from.m_subquery) << "the wrap must be a nested SOURCE";
	EXPECT_EQ(1u, wrapped->m_orderBy.size());
}

TEST(QueryComposerAuthorText, ThePlainWrapFlattensBackIntoOneSelect)
{
	// The cost argument, pinned: rule 2 of the optimizer folds a plain nested projection back into
	// its source, so a simple author query with a filter costs what it did before there was a
	// filter. Without this the wrap would be a real extra materialisation on every list.
	ibDataDBComposer composer;
	composer.FromText(wxT("SELECT Code, Name FROM Catalog.Products"));
	composer.Sort(wxT("Code"));

	const ibQuerySelectPtr wrapped = Parse(composer.RenderText());
	ASSERT_NE(nullptr, wrapped);

	const ibQuerySelectPtr flat = ibQueryRewrite::Rewrite(*wrapped);
	ASSERT_NE(nullptr, flat);
	EXPECT_EQ(nullptr, flat->m_from.m_subquery) << "the nested source should have been flattened away";
	ASSERT_EQ(2u, flat->m_from.m_name.size());
	EXPECT_EQ(wxT("Products"), flat->m_from.m_name.back());
}

// ===========================================================================
//  The schema without the read
// ===========================================================================

TEST(QueryDescribeOutput, ASourcelessQueryIsDescribedByItsProjectionsAlone)
{
	const ibQuerySelectPtr select = Parse(wxT("SELECT 1 AS One, 2 AS Two"));
	ASSERT_NE(nullptr, select);

	std::vector<ibQueryLowering::OutputColumn> schema;
	ibQueryLowering::DescribeOutput(*select, {}, schema);

	ASSERT_EQ(2u, schema.size());
	EXPECT_EQ(wxT("One"), schema[0].m_name);
	EXPECT_EQ(wxT("Two"), schema[1].m_name);
	// The synthetic column is OWNED by the schema — it has to outlive the door that made it, because
	// nothing here reads rows and the columns are only being named.
	EXPECT_NE(nullptr, schema[0].m_ownedCol.get());
}

TEST(QueryDescribeOutput, AColumnWithNoTableToReadItFromIsRefusedWithTheReason)
{
	// Not "unknown attribute": the name is not unknown, there is nowhere to read it FROM. Describing
	// must give the same verdict Execute would, in the same words.
	const ibQuerySelectPtr select = Parse(wxT("SELECT Code"));
	ASSERT_NE(nullptr, select);

	std::vector<ibQueryLowering::OutputColumn> schema;
	EXPECT_THROW(ibQueryLowering::DescribeOutput(*select, {}, schema), ibBackendException);
}

// ===========================================================================
//  A temp table's fields
// ===========================================================================

TEST(QueryConstructorModelTempFields, SelectStarIntoATempTableIsNotAnEmptyTable)
{
	// ⚠ THE DEFECT: "no projections, therefore no fields". A `SELECT * INTO Tmp` produced a temp
	// table the next statement could name and could not select a single column from.
	ibQueryParser parser;
	const ibQueryPackage package = parser.ParsePackage(
		wxT("SELECT * INTO Tmp FROM (SELECT Code, Name FROM Catalog.Products) AS src;")
		wxT("SELECT Code FROM Tmp"));
	ASSERT_EQ(2u, package.m_statements.size());
	ASSERT_TRUE(package.m_statements[1].m_select != nullptr);

	ibQueryConstructorModel model(nullptr);
	const std::vector<ibQueryConstructorField> fields =
		model.GetFields(package.m_statements[1].m_select->m_from, package, 1);

	ASSERT_EQ(2u, fields.size()) << "the temp table's columns are the making select's";
	EXPECT_EQ(wxT("Code"), fields[0].m_name);
	EXPECT_EQ(wxT("Name"), fields[1].m_name);
}

TEST(QueryConstructorModelTempFields, ATempTableIsNamedByTheEnginesOwnAnswer)
{
	// The constructor and the runtime must not call one column two things: the field list is built
	// from ibQueryOutputName, not from a second reading of the path.
	ibQueryParser parser;
	const ibQueryPackage package = parser.ParsePackage(
		wxT("SELECT src.Code AS Article INTO Tmp FROM (SELECT Code FROM Catalog.Products) AS src;")
		wxT("SELECT Article FROM Tmp"));
	ASSERT_EQ(2u, package.m_statements.size());

	ibQueryConstructorModel model(nullptr);
	const std::vector<ibQueryConstructorField> fields =
		model.GetFields(package.m_statements[1].m_select->m_from, package, 1);

	ASSERT_EQ(1u, fields.size());
	EXPECT_EQ(wxT("Article"), fields[0].m_name);
}

// ===========================================================================
//  Settings dropped by RESOLUTION
// ===========================================================================

TEST(QueryComposerPrune, ASettingWhoseFieldIsGoneIsDropped)
{
	// Taking the arbitrary query away takes its columns with it, and a filter over one of those is
	// left pointing at nothing. Same rule for a table removed, an attribute renamed, a metaobject
	// deleted — one question, asked again, instead of a cleanup per event.
	ibDataDBComposer composer;
	composer.FromSource(wxT("Catalog"), wxT("Products"));
	composer.Filter(wxT("Code"), wxT("="), ibValue(wxT("A-1")));
	composer.Filter(wxT("QueryOnlyField"), wxT("="), ibValue(wxT("x")));
	composer.Sort(wxT("QueryOnlyField"));
	composer.Sort(wxT("Code"));
	composer.TotalBy(wxT("QueryOnlyField"));

	const int dropped = composer.PruneUnresolvedSettings([](const wxString& path) {
		return path == wxT("Code");
	});

	EXPECT_EQ(3, dropped);
	// ⭐ READ THROUGH THE SETTING IN FORCE. `Filter()` and `Sort()` write the READER's section since
	// 2026-08-24 — the flat stores they used to write were a second answerer the render only reached
	// when neither section had said anything. `FilterCount` / `GetFilterAt` went with them; what
	// counts scope conditions now is `ScopeCount`, and that is a different subject.
	ASSERT_EQ(1u, composer.GetCurrentFilterDesc().m_nodes.size());
	ASSERT_EQ(1u, composer.SortCount());
	EXPECT_EQ(0u, composer.GroupCount());

	const ibFilterNodeDescription& kept = composer.GetCurrentFilterDesc().m_nodes[0];
	EXPECT_EQ(wxT("Code"), kept.m_left.m_path);
	EXPECT_EQ(wxT("A-1"), kept.m_right.m_value.GetString()) << "the surviving line must keep its bound value";
}

TEST(QueryComposerPrune, AWalkIsJudgedByWhatItSTARTSFrom)
{
	// The first segment is what has to exist; the rest is a reference walk, and a walk resolves
	// through the metadata of whatever the first segment turned out to be.
	ibDataDBComposer composer;
	composer.FromSource(wxT("Catalog"), wxT("Products"));
	composer.Sort(wxT("Owner.Region.Name"));

	const int dropped = composer.PruneUnresolvedSettings([](const wxString& path) {
		return path.BeforeFirst(wxT('.')) == wxT("Owner");
	});

	EXPECT_EQ(0, dropped);
	EXPECT_EQ(1u, composer.SortCount());
}

TEST(QueryComposerPrune, WithNoAnswerNothingIsDropped)
{
	// ⚠ The same promise PruneUnresolved makes: what cannot be VERIFIED is left alone. "We do not
	// know" must never delete somebody's work.
	ibDataDBComposer composer;
	composer.FromSource(wxT("Catalog"), wxT("Products"));
	composer.Filter(wxT("Anything"), wxT("="), ibValue(1));
	composer.Sort(wxT("AnythingElse"));

	EXPECT_EQ(0, composer.PruneUnresolvedSettings(nullptr));
	EXPECT_EQ(1u, composer.GetCurrentFilterDesc().m_nodes.size());
	EXPECT_EQ(1u, composer.SortCount());
}

// ===========================================================================
//  Grouping — one door, two readings
// ===========================================================================

TEST(QueryGrouping, ANonGroupingQueryOwesNothing)
{
	// The rule only exists once a query FOLDS. Asking a plain select what it still has to group by
	// must answer "nothing" — not "everything", which would be the constructor helpfully wrecking
	// an ordinary query the moment anybody looked at it.
	const ibQuerySelectPtr select = Parse(wxT("SELECT Code, Name FROM Catalog.Products AS Products"));
	ASSERT_NE(nullptr, select);
	EXPECT_TRUE(ibQueryLowering::UngroupedProjections(*select, {}).empty());
}

TEST(QueryGrouping, AnUnresolvableQueryIsNotJudged)
{
	// ⚠ The promise the whole check family makes: what cannot be VERIFIED is left alone. A host
	// reading this as "the work still to do" would otherwise add group keys to a query nobody could
	// resolve — and the constructor calls it on every add.
	const ibQuerySelectPtr select = Parse(
		wxT("SELECT Code, SUM(Qty) FROM Catalog.NoSuchThingExists AS t"));
	ASSERT_NE(nullptr, select);
	EXPECT_TRUE(ibQueryLowering::UngroupedProjections(*select, {}).empty());
}

// ===========================================================================
//  A table handed in as a parameter
// ===========================================================================

TEST(QueryParameterTable, ItIsMARKED_InTheTextAndSurvivesTheRoundTrip)
{
	// `&` already means "this came from outside" everywhere else in this language, so it means the
	// same thing on a source. Without the mark, `FROM Goods` could be a temp table the package made,
	// a bound table or a metaobject, and which one would be decided by the order the resolver
	// happens to look — a query has to read as what it is.
	const ibQuerySelectPtr select = Parse(wxT("SELECT * INTO Goods FROM &GoodsTable"));
	ASSERT_NE(nullptr, select);
	EXPECT_TRUE(select->m_from.m_parameter);
	ASSERT_EQ(1u, select->m_from.m_name.size());
	EXPECT_EQ(wxT("GoodsTable"), select->m_from.m_name[0]);

	// AND WHAT IT WRITES, THE PARSER READS — the mark has to come back on.
	const wxString text = ibRenderQuery(*select);
	EXPECT_TRUE(text.Contains(wxT("&GoodsTable"))) << text;
	const ibQuerySelectPtr again = Parse(text);
	ASSERT_NE(nullptr, again);
	EXPECT_TRUE(again->m_from.m_parameter);
}

TEST(QueryParameterTable, ItGoesInToATemporaryTableAndOnlyThere)
{
	// The discipline, refused at the parse: a value table lives in RAM, so every statement that
	// names it directly stitches the read in memory AGAIN. Materialised once, it is a table the
	// engine can promote and join server-side.
	EXPECT_THROW(Parse(wxT("SELECT Article FROM &GoodsTable")), ibBackendException);
	EXPECT_NO_THROW(Parse(wxT("SELECT * INTO Goods FROM &GoodsTable")));
}

// ===========================================================================
//  Which aggregates fit a type — one door, offered and refused by the same list
// ===========================================================================

namespace {

ibTypeDescription TypeOf(ibValueTypes primitive)
{
	ibTypeDescription type;
	type.SetDefaultMetaType(primitive);
	return type;
}

bool Offers(const std::vector<ibQueryKeyword>& list, ibQueryKeyword keyword)
{
	return std::find(list.begin(), list.end(), keyword) != list.end();
}

} // namespace

TEST(QueryAggregateTypes, ANumberTakesEveryFold)
{
	const std::vector<ibQueryKeyword> allowed =
		ibQueryLowering::AggregatesFor(TypeOf(ibValueTypes::TYPE_NUMBER));
	EXPECT_TRUE(Offers(allowed, ibQueryKeyword::Sum));
	EXPECT_TRUE(Offers(allowed, ibQueryKeyword::Avg));
	EXPECT_TRUE(Offers(allowed, ibQueryKeyword::Min));
	EXPECT_TRUE(Offers(allowed, ibQueryKeyword::Max));
	EXPECT_TRUE(Offers(allowed, ibQueryKeyword::Count));
}

TEST(QueryAggregateTypes, AStringIsOrderedButNotSummable)
{
	// The whole point of the rule: `SUM(Description)` is not a mistake to be caught by the database
	// in a dialect of its own — it is a choice that should never have been offered.
	const std::vector<ibQueryKeyword> allowed =
		ibQueryLowering::AggregatesFor(TypeOf(ibValueTypes::TYPE_STRING));
	EXPECT_FALSE(Offers(allowed, ibQueryKeyword::Sum));
	EXPECT_FALSE(Offers(allowed, ibQueryKeyword::Avg));
	EXPECT_TRUE(Offers(allowed, ibQueryKeyword::Min));
	EXPECT_TRUE(Offers(allowed, ibQueryKeyword::Max));
	EXPECT_TRUE(Offers(allowed, ibQueryKeyword::Count));
}

TEST(QueryAggregateTypes, ADateIsOrderedAndNotSummable)
{
	const std::vector<ibQueryKeyword> allowed =
		ibQueryLowering::AggregatesFor(TypeOf(ibValueTypes::TYPE_DATE));
	EXPECT_FALSE(Offers(allowed, ibQueryKeyword::Sum));
	EXPECT_TRUE(Offers(allowed, ibQueryKeyword::Max));
}

TEST(QueryAggregateTypes, AnUnknownTypeOffersEverything)
{
	// ⚠ Empty means UNKNOWN, never "no type" — and unknown must not narrow. Which type a row holds
	// is the row's business; refusing on "it might be a string" would be this answer inventing one,
	// and it would take away a fold the query can perfectly well do.
	const std::vector<ibQueryKeyword> allowed = ibQueryLowering::AggregatesFor(ibTypeDescription());
	EXPECT_TRUE(Offers(allowed, ibQueryKeyword::Sum));
	EXPECT_TRUE(Offers(allowed, ibQueryKeyword::Count));
}

TEST(QueryAggregateTypes, CountIsAlwaysThere)
{
	// A cell whose list is empty is a cell nobody can fill. Counting asks nothing of the type, so it
	// is the one answer that always exists.
	EXPECT_TRUE(Offers(ibQueryLowering::AggregatesFor(TypeOf(ibValueTypes::TYPE_BOOLEAN)),
		ibQueryKeyword::Count));
}

// ===========================================================================
//  CAST — narrowing a composite reference so it can be walked
// ===========================================================================
//
// ⚠ WHAT IT IS AND IS NOT. `CAST(Recorder AS Document.Order)` NARROWS: the value already is of that
// type, and the cast only says WHICH of a composite reference's types is meant. It is not a
// conversion — `CAST(Code AS Number)` would need an operation the door does not have, in both
// providers and every dialect, and it is refused with a message that says so.
//
// The narrowing exists for one reason: a composite reference has no single set of fields behind it,
// so the walk is refused ("not a single-target reference"). Naming the type supplies the answer, and
// from there it is an ordinary dot-walk.

TEST(QueryCast, ACastAloneRoundTrips)
{
	const ibQuerySelectPtr select = Parse(
		wxT("SELECT CAST(Recorder AS Document.Order) FROM AccumulationRegister.Goods"));
	ASSERT_NE(nullptr, select);
	ASSERT_EQ(1u, select->m_projections.size());

	const ibQueryAstExprPtr& expr = select->m_projections[0].m_expr;
	ASSERT_NE(nullptr, expr);
	EXPECT_EQ(ibQueryAstExprKind::Cast, expr->m_kind);
	ASSERT_EQ(2u, expr->m_path.size()) << "the TARGET TYPE is the cast's own path";
	EXPECT_EQ(wxT("Document"), expr->m_path[0]);
	EXPECT_EQ(wxT("Order"),    expr->m_path[1]);
	ASSERT_NE(nullptr, expr->m_arg);
	EXPECT_EQ(ibQueryAstExprKind::Column, expr->m_arg->m_kind) << "and the field it narrows is inside";

	EXPECT_EQ(wxT("CAST(Recorder AS Document.Order)"), ibRenderQueryExpr(*expr));
}

TEST(QueryCast, AWalkAfterACastIsAColumnRootedOnIt)
{
	// ⚠ THE SHAPE, and it is deliberate: `CAST(x AS T).A.B` is a COLUMN whose path is {A, B} and
	// whose ROOT is the cast. The resolver already walks a column's path from a starting queryable;
	// the cast only says which queryable to start on — so SelectPath, ExpandDotWalkJoins and the RAM
	// join all work on it unchanged, with nothing downstream learning a new trick.
	ibQueryParser parser;
	const ibQueryAstExprPtr expr = parser.ParseExpression(wxT("CAST(Recorder AS Document.Order).Number"));
	ASSERT_NE(nullptr, expr);
	EXPECT_EQ(ibQueryAstExprKind::Column, expr->m_kind);
	ASSERT_EQ(1u, expr->m_path.size());
	EXPECT_EQ(wxT("Number"), expr->m_path[0]);
	ASSERT_NE(nullptr, expr->m_arg);
	EXPECT_EQ(ibQueryAstExprKind::Cast, expr->m_arg->m_kind);
}

TEST(QueryCast, AWalkAfterACastWritesItsRootBack)
{
	// WHAT IT WRITES, THE PARSER READS — the contract every other clause holds to. A round trip that
	// dropped the cast would leave a walk through a composite the engine then refuses.
	const wxString text = wxT("CAST(Recorder AS Document.Order).Partner.Code");
	ibQueryParser parser;
	const ibQueryAstExprPtr expr = parser.ParseExpression(text);
	ASSERT_NE(nullptr, expr);
	EXPECT_EQ(text, ibRenderQueryExpr(*expr));

	const ibQueryAstExprPtr again = parser.ParseExpression(ibRenderQueryExpr(*expr));
	ASSERT_NE(nullptr, again);
	EXPECT_EQ(text, ibRenderQueryExpr(*again));
}

TEST(QueryCast, ItReadsInsideAWholeQueryAndComesBackOut)
{
	const wxString text =
		wxT("SELECT\n\tCAST(Recorder AS Document.Order).Number AS OrderNumber\nFROM\n\tAccumulationRegister.Goods");
	const ibQuerySelectPtr select = Parse(text);
	ASSERT_NE(nullptr, select);
	const wxString written = ibRenderQuery(*select);
	EXPECT_TRUE(written.Contains(wxT("CAST(Recorder AS Document.Order).Number"))) << written;

	const ibQuerySelectPtr again = Parse(written);
	ASSERT_NE(nullptr, again);
	EXPECT_EQ(written, ibRenderQuery(*again));
}

TEST(QueryCast, ItNeedsAType)
{
	// `CAST(x)` is not a shorter cast — it is an unfinished one, and a cast with no type says
	// nothing at all.
	ibQueryParser parser;
	EXPECT_THROW(parser.ParseExpression(wxT("CAST(Recorder)")), ibBackendException);
}

// ===========================================================================
//  The names things HAVE (queryRender.h) — the reads, collapsed from many copies
// ===========================================================================

TEST(QueryDimensionName, ALevelAnswersToItsOwnNameElseTheLeaf)
{
	// A totals LEVEL is an output column, so it needs a name of its own: two levels over one column
	// (Date by month, Date by day) would otherwise both answer to `Date` and the second would win.
	// This rule was written out in the grid and in the lowering before it had a home.
	const ibQuerySelectPtr select = Parse(
		wxT("SELECT Code FROM Catalog.Products AS Products TOTALS BY Products.Parent"));
	ASSERT_NE(nullptr, select);
	ASSERT_EQ(1u, select->m_totalsBy.size());
	EXPECT_EQ(wxT("Parent"), ibQueryDimensionName(select->m_totalsBy[0]));

	const ibQuerySelectPtr named = Parse(
		wxT("SELECT Code FROM Catalog.Products AS Products TOTALS BY Products.Parent AS Folder"));
	ASSERT_NE(nullptr, named);
	ASSERT_EQ(1u, named->m_totalsBy.size());
	EXPECT_EQ(wxT("Folder"), ibQueryDimensionName(named->m_totalsBy[0]));
}

TEST(QuerySourceName, ANestedTableWithNoAliasHasNoNameOfItsOwn)
{
	// EMPTY is the honest answer — a nested table has no path to fall back on. The window adds
	// "(nested table)" for the reader; the ENGINE must not, because that string is not a name
	// anything could be written against.
	const ibQuerySelectPtr select = Parse(
		wxT("SELECT Code FROM (SELECT Code FROM Catalog.Products) AS sub"));
	ASSERT_NE(nullptr, select);
	EXPECT_EQ(wxT("sub"), ibQuerySourceName(select->m_from));

	ibQuerySource bare;
	bare.m_subquery = select->m_from.m_subquery;
	EXPECT_TRUE(ibQuerySourceName(bare).IsEmpty());
}

// ===========================================================================
//  One walk, two readings (queryConstructorModel)
// ===========================================================================

TEST(QueryConstructorModelPaths, APathIntoANestedTableResolvesToItsProjection)
{
	// The nested table answers with its OWN projections, so a path into it resolves without any
	// metadata at all — which is what makes this testable and what makes a temp table's fields work.
	ibQueryParser parser;
	const ibQueryPackage package = parser.ParsePackage(
		wxT("SELECT src.Code AS Article FROM (SELECT Code FROM Catalog.Products) AS src"));
	ASSERT_EQ(1u, package.m_statements.size());
	ASSERT_TRUE(package.m_statements[0].m_select != nullptr);

	ibQueryConstructorModel model(nullptr);
	const ibQueryConstructorField field = model.FieldOfPath(*package.m_statements[0].m_select,
		{ wxT("src"), wxT("Code") }, package, 0);
	EXPECT_EQ(wxT("Code"), field.m_name);
}

TEST(QueryConstructorModelPaths, AnUnresolvablePathIsEmptyNotAGuess)
{
	// ⚠ The promise the whole family makes. A host reads TypeOfPath to decide what to OFFER, and an
	// invented answer there would narrow a choice the query can perfectly well make.
	ibQueryParser parser;
	const ibQueryPackage package = parser.ParsePackage(
		wxT("SELECT src.Code FROM (SELECT Code FROM Catalog.Products) AS src"));
	ASSERT_EQ(1u, package.m_statements.size());

	ibQueryConstructorModel model(nullptr);
	const ibQuerySelect& select = *package.m_statements[0].m_select;

	EXPECT_TRUE(model.FieldOfPath(select, { wxT("src"), wxT("NoSuchField") }, package, 0).m_name.IsEmpty());
	EXPECT_EQ(0u, model.ReferenceOfPath(select, { wxT("src"), wxT("NoSuchField") }, package, 0));
	EXPECT_EQ(0u, model.TypeOfPath(select, { wxT("src"), wxT("NoSuchField") }, package, 0).GetClsidCount());

	// …and walking THROUGH something that is not a reference stops rather than inventing a level.
	EXPECT_TRUE(model.FieldOfPath(select, { wxT("src"), wxT("Code"), wxT("Anything") }, package, 0)
		.m_name.IsEmpty());
}

// ===========================================================================
//  OVERALL — the level above every dimension (queryAst.h m_totalsOverall)
// ===========================================================================

TEST(QueryTotalsOverall, ItIsReadWhereItIsWrittenAndWrittenBackFirst)
{
	// `TOTALS … BY OVERALL, Dim` — the overall sits above every dimension, so it is written first,
	// and reading it back has to put it there again or the round trip moves the level.
	const ibQuerySelectPtr select = Parse(
		wxT("SELECT Code FROM Catalog.Products AS Products ")
		wxT("TOTALS SUM(Products.Price) BY OVERALL, Products.Parent"));
	ASSERT_NE(nullptr, select);
	EXPECT_TRUE(select->m_totalsOverall);
	ASSERT_EQ(1u, select->m_totalsBy.size());   // OVERALL is NOT a dimension in the list

	const wxString written = ibRenderQuery(*select);
	EXPECT_TRUE(written.Contains(wxT("OVERALL")));

	const ibQuerySelectPtr again = Parse(written);
	ASSERT_NE(nullptr, again);
	EXPECT_TRUE(again->m_totalsOverall);
	EXPECT_EQ(1u, again->m_totalsBy.size());
	EXPECT_EQ(written, ibRenderQuery(*again));
}

TEST(QueryTotalsOverall, ItStandsAloneWithNoDimensionsAtAll)
{
	// One row over everything is a whole totals query. The comma is what says dimensions follow, so
	// without one there are none — and that must parse rather than demand a dimension nobody wants.
	const ibQuerySelectPtr select = Parse(
		wxT("SELECT Code FROM Catalog.Products AS Products TOTALS SUM(Products.Price) BY OVERALL"));
	ASSERT_NE(nullptr, select);
	EXPECT_TRUE(select->m_totalsOverall);
	EXPECT_TRUE(select->m_totalsBy.empty());
	EXPECT_TRUE(select->m_hasTotals);

	const ibQuerySelectPtr again = Parse(ibRenderQuery(*select));
	ASSERT_NE(nullptr, again);
	EXPECT_TRUE(again->m_totalsOverall);
	EXPECT_TRUE(again->m_totalsBy.empty());
}

TEST(QueryTotalsOverall, ADimensionOnlyTotalsDoesNotAcquireOne)
{
	// The flag is a CHOICE, and the default is off: the fold has always computed the root, and a
	// query that did not ask for the row must not start returning it.
	const ibQuerySelectPtr select = Parse(
		wxT("SELECT Code FROM Catalog.Products AS Products TOTALS BY Products.Parent"));
	ASSERT_NE(nullptr, select);
	EXPECT_FALSE(select->m_totalsOverall);
	EXPECT_FALSE(ibRenderQuery(*select).Contains(wxT("OVERALL")));
}

TEST(QueryTotalsOverall, TotalsWithNeitherADimensionNorOverallIsRefused)
{
	// The refusal was relaxed, not removed: BY has to name SOMETHING.
	ibQueryParser parser;
	EXPECT_THROW(parser.ParsePackage(
		wxT("SELECT Code FROM Catalog.Products AS Products TOTALS SUM(Products.Price) BY")),
		ibBackendException);
}

// ===========================================================================
//  A condition over a folded value is a HAVING (queryRewrite)
// ===========================================================================

TEST(QueryAggregateCondition, AnAggregateWrittenInWhereBecomesHaving)
{
	// The constructor's Conditions tab offers aggregate fields beside plain ones — they are all
	// fields of the result — so a condition over SUM(x) gets written where every condition is
	// written. Left in WHERE it reached the ROW filter, which has no aggregates to filter by.
	const ibQuerySelectPtr parsed = Parse(
		wxT("SELECT Products.Parent, SUM(Products.Price) FROM Catalog.Products AS Products ")
		wxT("WHERE SUM(Products.Price) > 100 GROUP BY Products.Parent"));
	ASSERT_NE(nullptr, parsed);

	const ibQuerySelectPtr rewritten = ibQueryRewrite::Rewrite(*parsed);
	ASSERT_NE(nullptr, rewritten);
	EXPECT_EQ(nullptr, rewritten->m_where);      // nothing left to filter rows by
	ASSERT_NE(nullptr, rewritten->m_having);
	EXPECT_EQ(ibQueryAstExprKind::Compare, rewritten->m_having->m_kind);
}

TEST(QueryAggregateCondition, ThePlainTermsStayInWhere)
{
	// Split per AND-TERM: a row filter and a group filter written side by side are both kept, each
	// where it belongs. Moving the whole WHERE would have thrown away the row filter.
	const ibQuerySelectPtr parsed = Parse(
		wxT("SELECT Products.Parent, SUM(Products.Price) FROM Catalog.Products AS Products ")
		wxT("WHERE Products.Code = \"A\" AND SUM(Products.Price) > 100 GROUP BY Products.Parent"));
	ASSERT_NE(nullptr, parsed);

	const ibQuerySelectPtr rewritten = ibQueryRewrite::Rewrite(*parsed);
	ASSERT_NE(nullptr, rewritten);
	ASSERT_NE(nullptr, rewritten->m_where);
	EXPECT_EQ(ibQueryAstExprKind::Compare, rewritten->m_where->m_kind);   // the plain one, alone
	ASSERT_NE(nullptr, rewritten->m_having);
}

TEST(QueryAggregateCondition, AnOrAcrossTheTwoMovesWholeAndIsNeverSplit)
{
	// `A = 1 OR SUM(x) > 5` is ONE term and it goes to HAVING ENTIRE. Splitting it into a row filter
	// and a group filter would change which rows survive — and leaving it in WHERE is simply not an
	// option: anything naming an aggregate can only be evaluated after the fold.
	const ibQuerySelectPtr parsed = Parse(
		wxT("SELECT Products.Parent, SUM(Products.Price) FROM Catalog.Products AS Products ")
		wxT("WHERE Products.Code = \"A\" OR SUM(Products.Price) > 100 GROUP BY Products.Parent"));
	ASSERT_NE(nullptr, parsed);

	const ibQuerySelectPtr rewritten = ibQueryRewrite::Rewrite(*parsed);
	ASSERT_NE(nullptr, rewritten);
	EXPECT_EQ(nullptr, rewritten->m_where);              // the only term left, so nothing filters rows
	ASSERT_NE(nullptr, rewritten->m_having);
	EXPECT_EQ(ibQueryAstExprKind::Logical, rewritten->m_having->m_kind);   // still the whole OR
}

TEST(QueryAggregateCondition, AnExistingHavingIsKeptAndJoined)
{
	// The author may have written both. One must not silently replace the other.
	const ibQuerySelectPtr parsed = Parse(
		wxT("SELECT Products.Parent, SUM(Products.Price) FROM Catalog.Products AS Products ")
		wxT("WHERE COUNT(*) > 3 GROUP BY Products.Parent HAVING SUM(Products.Price) > 100"));
	ASSERT_NE(nullptr, parsed);

	const ibQuerySelectPtr rewritten = ibQueryRewrite::Rewrite(*parsed);
	ASSERT_NE(nullptr, rewritten);
	ASSERT_NE(nullptr, rewritten->m_having);
	EXPECT_EQ(ibQueryAstExprKind::Logical, rewritten->m_having->m_kind);   // both, AND-folded
	EXPECT_EQ(nullptr, rewritten->m_where);
}

// ===========================================================================
//  Grouping completeness reads the whole expression tree, not its top
// ===========================================================================

TEST(QueryGroupingCompleteness, AFreeColumnInsideAComputedProjectionStillNeedsGrouping)
{
	// `SUM(Qty) / Price` is neither a bare column nor an aggregate: it belongs in no list the
	// constructor shows, and both collectors used to skip it whole — so the free `Price` inside it
	// was held to no rule at all. An expression is a TREE; the rule reads it as one.
	const ibQuerySelectPtr select = Parse(
		wxT("SELECT SUM(Products.Price) / Products.Code FROM Catalog.Products AS Products"));
	ASSERT_NE(nullptr, select);

	// Resolution needs a configuration, so this pins the SHAPE the walk sees rather than the
	// resolved answer: one folded column and one free one, in one projection.
	ASSERT_EQ(1u, select->m_projections.size());
	EXPECT_EQ(ibQueryAstExprKind::Arith, select->m_projections[0].m_expr->m_kind);
}

TEST(QueryAggregateCondition, HavingIsItsOwnClauseAndNeedsNoGroupBy)
{
	// With no GROUP BY the WHOLE RESULT is one group — the same thing `TOTALS … BY OVERALL` says in
	// the other clause. The parser read HAVING only as a tail of GROUP BY, so this shape died as
	// "unexpected text after the query" — and the constructor produces it by itself, because a
	// condition over a folded value MOVES here from WHERE.
	const ibQuerySelectPtr select = Parse(
		wxT("SELECT Products.Code FROM Catalog.Products AS Products ")
		wxT("HAVING SUM(Products.Price) = &Total"));
	ASSERT_NE(nullptr, select);
	EXPECT_TRUE(select->m_groupBy.empty());
	ASSERT_NE(nullptr, select->m_having);

	// …and it survives the round trip, which is what the window depends on.
	const wxString written = ibRenderQuery(*select);
	const ibQuerySelectPtr again = Parse(written);
	ASSERT_NE(nullptr, again);
	ASSERT_NE(nullptr, again->m_having);
	EXPECT_EQ(written, ibRenderQuery(*again));
}

// ===========================================================================
//  COUNT(DISTINCT x) — a property of the CALL, not of the statement
// ===========================================================================

TEST(QueryDistinctAggregate, ItIsReadAndWrittenInsideTheCall)
{
	// `COUNT(DISTINCT Board)` asks how many DIFFERENT boards, not how many rows have one. It is not
	// the statement's own DISTINCT (whole rows) — a query may want either, or both.
	const ibQuerySelectPtr select = Parse(
		wxT("SELECT COUNT(DISTINCT Actions.Board) FROM Catalog.Actions AS Actions"));
	ASSERT_NE(nullptr, select);
	ASSERT_EQ(1u, select->m_projections.size());
	const ibQueryAstExprPtr& call = select->m_projections[0].m_expr;
	ASSERT_NE(nullptr, call);
	EXPECT_EQ(ibQueryAstExprKind::Func, call->m_kind);
	EXPECT_TRUE(call->m_distinctArg);
	EXPECT_FALSE(select->m_distinct);   // the statement's own DISTINCT is untouched

	const wxString written = ibRenderQuery(*select);
	EXPECT_TRUE(written.Contains(wxT("COUNT(DISTINCT ")));
	const ibQuerySelectPtr again = Parse(written);
	ASSERT_NE(nullptr, again);
	EXPECT_TRUE(again->m_projections[0].m_expr->m_distinctArg);
	EXPECT_EQ(written, ibRenderQuery(*again));
}

TEST(QueryDistinctAggregate, ThePlainCallStaysPlain)
{
	const ibQuerySelectPtr select = Parse(
		wxT("SELECT COUNT(Actions.Board) FROM Catalog.Actions AS Actions"));
	ASSERT_NE(nullptr, select);
	EXPECT_FALSE(select->m_projections[0].m_expr->m_distinctArg);
	EXPECT_FALSE(ibRenderQuery(*select).Contains(wxT("DISTINCT")));
}

TEST(QueryDistinctAggregate, DistinctStarIsRefusedWithAReasonNotASyntaxError)
{
	// `COUNT(DISTINCT *)` names nothing to be distinct ABOUT. Saying so beats a parser complaining
	// about a star it would otherwise have accepted.
	ibQueryParser parser;
	EXPECT_THROW(parser.Parse(wxT("SELECT COUNT(DISTINCT *) FROM Catalog.Actions AS Actions")),
		ibBackendException);
}

TEST(QueryDistinctAggregate, ItSurvivesTheOptimizerClone)
{
	// The rewrite deep-clones the tree; a flag left behind there would run a different query from
	// the one that was read.
	const ibQuerySelectPtr parsed = Parse(
		wxT("SELECT COUNT(DISTINCT Actions.Board) FROM Catalog.Actions AS Actions"));
	ASSERT_NE(nullptr, parsed);
	const ibQuerySelectPtr rewritten = ibQueryRewrite::Rewrite(*parsed);
	ASSERT_NE(nullptr, rewritten);
	ASSERT_EQ(1u, rewritten->m_projections.size());
	EXPECT_TRUE(rewritten->m_projections[0].m_expr->m_distinctArg);
}

TEST(QueryDistinctAggregate, ItWorksInTotalsToo)
{
	// The shape Max wrote out: TOTALS COUNT(DISTINCT Board) BY OVERALL, Author.
	const ibQuerySelectPtr select = Parse(
		wxT("SELECT Actions.Author FROM Catalog.Actions AS Actions ")
		wxT("TOTALS COUNT(DISTINCT Actions.Board) BY OVERALL, Actions.Author"));
	ASSERT_NE(nullptr, select);
	EXPECT_TRUE(select->m_totalsOverall);
	ASSERT_EQ(1u, select->m_totalsAggregates.size());
	EXPECT_TRUE(select->m_totalsAggregates[0].m_expr->m_distinctArg);

	const wxString written = ibRenderQuery(*select);
	EXPECT_EQ(written, ibRenderQuery(*Parse(written)));
}

// ===========================================================================
//  Detail records — a level of the ladder, and NOT a word of the query text
// ===========================================================================

// ⭐ "A detail record is an empty grouping" (Max). The node sits in the ladder like any other,
// but it says nothing about WHAT TO COMPUTE — so the rendered query is the same query it would be
// without it, and the request travels as an argument of the READ (ExecuteTotals(…, withDetails)).
//
// The pin that matters: a fieldless level must not write `BY` with nothing after it. That produced
// a query the parser refused, and the caller saw an empty report with no reason given.
TEST(QueryComposerDetails, ADetailLevelWritesNothingIntoTheQuery)
{
	ibDataDBComposer composer;
	composer.FromText(wxT("SELECT Partner, Amount FROM Document.Sales"));
	composer.Resource(wxT("SUM"), wxT("Amount"));
	composer.TotalBy(wxT("Partner"));

	const wxString grouped = composer.RenderText();
	EXPECT_TRUE(grouped.Contains(wxT("TOTALS")));
	EXPECT_TRUE(grouped.Contains(wxT("BY Partner")));

	// The detail level joins the ladder…
	ibDataComposer::GroupNode details;
	details.m_kind = ibCompositionLevelKind::Details;
	composer.Root().m_rowGroups.push_back(details);

	// …and the query text does not change by one character.
	EXPECT_EQ(grouped, composer.RenderText());
	EXPECT_FALSE(composer.RenderText().Contains(wxT("BY ,")));

	// What DID change is what the read is asked for.
	EXPECT_TRUE(ibDataComposer::WantsDetails(composer.Root()));
}

// AN OUTPUT THAT GROUPS BY NOTHING is not "details under a heading" — it has no heading to hang
// them under, its read is a flat cursor and every row it returns is a detail row already. So the
// question answers NO there, and the totals read is not asked for something it need not do.
TEST(QueryComposerDetails, AnOutputWithNoGroupingIsNotADetailsRequest)
{
	ibDataDBComposer composer;
	composer.FromText(wxT("SELECT Partner, Amount FROM Document.Sales"));

	EXPECT_FALSE(ibDataComposer::WantsDetails(composer.Root()));

	ibDataComposer::GroupNode details;
	details.m_kind = ibCompositionLevelKind::Details;
	composer.Root().m_rowGroups.push_back(details);
	EXPECT_FALSE(ibDataComposer::WantsDetails(composer.Root()));
	EXPECT_FALSE(ibDataComposer::HasGroupingFields(composer.Root()));
}

// ===========================================================================
//  The cross-table — one fold, two axes
// ===========================================================================

// ⭐⭐ A CROSS-TABLE IS ONE `TOTALS BY`, ROWS FIRST. Both axes fold together and the server returns
// one row per intersection, which is what a cell IS. Nothing in the text says "cross": the shape
// comes out of the ORDER the keys were written in.
TEST(QueryComposerCross, BothAxesFoldInOneTotalsRowsFirst)
{
	ibDataDBComposer composer;
	composer.FromText(wxT("SELECT Partner, Warehouse, Amount FROM Document.Sales"));
	composer.Resource(wxT("SUM"), wxT("Amount"));

	ibDataComposer::GroupNode rows;
	rows.m_settings.m_group.Append(wxT("Partner"));
	composer.Root().m_rowGroups.push_back(rows);

	ibDataComposer::GroupNode columns;
	columns.m_settings.m_group.Append(wxT("Warehouse"));
	composer.Root().m_columnGroups.push_back(columns);
	// ⚠ AND THE OUTPUT SAYS WHAT IT IS. The kind is STORED, not read off the axes (Output::Kind) —
	// an empty table is a legitimate thing to have declared, so FILLING a column axis is not what
	// makes an output a table. Whoever declares one says so; here that is the test.
	composer.Root().m_kind = ibCompositionOutputKind::Table;

	const wxString text = composer.RenderText();
	EXPECT_TRUE(text.Contains(wxT("TOTALS"))) << text;
	// One BY, both keys, rows before columns — a table transposed would be the same numbers in the
	// wrong place, with nothing in the answer to say which was meant.
	EXPECT_TRUE(text.Contains(wxT("BY Partner, Warehouse"))) << text;
	EXPECT_EQ(ibCompositionOutputKind::Table, composer.Root().Kind());
}

// ⭐ A TABLE GROUPED ONLY ACROSS THE PAGE IS STILL GROUPED. The gate that decides whether TOTALS is
// written at all used to look down the page only, so this output rendered with no TOTALS — an empty
// report, with the grouping plainly on screen.
TEST(QueryComposerCross, AGroupingOnTheColumnAxisAloneStillWritesTotals)
{
	ibDataDBComposer composer;
	composer.FromText(wxT("SELECT Warehouse, Amount FROM Document.Sales"));
	composer.Resource(wxT("SUM"), wxT("Amount"));

	ibDataComposer::GroupNode columns;
	columns.m_settings.m_group.Append(wxT("Warehouse"));
	composer.Root().m_columnGroups.push_back(columns);

	EXPECT_TRUE(ibDataComposer::HasGroupingFields(composer.Root()));
	const wxString text = composer.RenderText();
	EXPECT_TRUE(text.Contains(wxT("BY Warehouse"))) << text;
	EXPECT_FALSE(text.Contains(wxT("BY OVERALL"))) << text;
}

// ⭐ WHICH AXIS A HEADING READS ALONG is asked of the output info, never worked out from the depth
// by each printer in turn. A dimension's depth alone cannot tell the third row heading from the
// first column heading — the seam between them is what the composer knows and the schema does not.
TEST(QueryComposerCross, TheOutputInfoClassifiesAHeadingByItsAxis)
{
	ibCompositionOutputInfo info;
	info.m_rowLevels = 2;   // two headings down the page, everything deeper reads across

	ibQueryLowering::OutputColumn first;
	first.m_role = ibQueryLowering::ibColumnRole::Dimension;
	first.m_level = 0;
	ibQueryLowering::OutputColumn third;
	third.m_role = ibQueryLowering::ibColumnRole::Dimension;
	third.m_level = 2;
	ibQueryLowering::OutputColumn measure;
	measure.m_role = ibQueryLowering::ibColumnRole::Measure;

	EXPECT_EQ(ibCompositionAxis::Rows,    info.AxisOf(first));
	EXPECT_EQ(ibCompositionAxis::Columns, info.AxisOf(third));
	// A measure belongs to no axis — it is what stands where the two meet.
	EXPECT_EQ(ibCompositionAxis::None,    info.AxisOf(measure));
}

// A LEVEL WITH NO FIELDS IS THE DETAIL RECORDS and writes no key, so it must not move the seam. It
// did in the first draft — `m_rowGroups.size()` counted it — and one row heading printed across the
// page.
TEST(QueryComposerCross, ADetailLevelDoesNotMoveTheSeamBetweenTheAxes)
{
	ibDataComposer::Output output;
	ibDataComposer::GroupNode rows;
	rows.m_settings.m_group.Append(wxT("Partner"));
	output.m_rowGroups.push_back(rows);
	ibDataComposer::GroupNode details;
	details.m_kind = ibCompositionLevelKind::Details;
	output.m_rowGroups.push_back(details);

	EXPECT_EQ(2u, output.m_rowGroups.size());
	EXPECT_EQ(1u, ibDataComposer::DimensionCount(output.m_rowGroups));
}

// ⚠ AND THE DETAIL LEVEL SURVIVES A TIDY-UP. A level that LOST its fields is dropped — it would
// fold every row it sees into one nameless heading — and the two emptinesses must not be confused:
// one is a setting that stopped resolving, the other is a setting the author wrote.
TEST(QueryComposerDetails, CollapsingEmptyLevelsKeepsTheDetailOne)
{
	ibDataDBComposer composer;
	composer.FromText(wxT("SELECT Partner, Amount FROM Document.Sales"));
	composer.TotalBy(wxT("Partner"));

	ibDataComposer::GroupNode orphan;              // a grouping whose fields stopped resolving
	composer.Root().m_rowGroups.push_back(orphan);
	ibDataComposer::GroupNode details;
	details.m_kind = ibCompositionLevelKind::Details;
	composer.Root().m_rowGroups.push_back(details);
	ASSERT_EQ(3u, composer.Root().m_rowGroups.size());

	composer.CollapseEmptyLevels();

	ASSERT_EQ(2u, composer.Root().m_rowGroups.size());
	EXPECT_EQ(ibCompositionLevelKind::Grouping, composer.Root().m_rowGroups[0].m_kind);
	EXPECT_EQ(ibCompositionLevelKind::Details,  composer.Root().m_rowGroups[1].m_kind);
}

// ⭐⭐ AND THE PERIODICITY REACHES THE QUERY. A level set to periods in the settings window is
// written into the text the same way a person would type it — the composer is the reader of that
// setting, and without it the strip would be one more thing that saves and means nothing.
TEST(QueryComposerCross, ALevelsPeriodicityIsWrittenIntoTheText)
{
	ibDataDBComposer composer;
	composer.FromText(wxT("SELECT Date, Amount FROM Document.Sales"));
	composer.Resource(wxT("SUM"), wxT("Amount"));

	ibDataComposer::GroupNode level;
	level.m_settings.m_group.Append(wxT("Date"));
	level.m_settings.m_group.m_lines[0].m_periods.m_unit = wxT("Month");
	composer.Root().m_rowGroups.push_back(level);

	const wxString text = composer.RenderText();
	EXPECT_TRUE(text.Contains(wxT("BY Date PERIODS(Month)"))) << text;
}

// ⚠ A BOUND LEFT OUT KEEPS ITS POSITION — `PERIODS(Month, , &To)`. Filling it in would be the writer
// answering a question the person left open.
TEST(QueryComposerCross, AnUnstatedLowerBoundKeepsItsPlace)
{
	ibDataDBComposer composer;
	composer.FromText(wxT("SELECT Date, Amount FROM Document.Sales"));
	composer.Resource(wxT("SUM"), wxT("Amount"));

	ibDataComposer::GroupNode level;
	level.m_settings.m_group.Append(wxT("Date"));
	level.m_settings.m_group.m_lines[0].m_periods.m_unit = wxT("Month");
	level.m_settings.m_group.m_lines[0].m_periods.m_to   = wxT("&To");
	composer.Root().m_rowGroups.push_back(level);

	const wxString text = composer.RenderText();
	EXPECT_TRUE(text.Contains(wxT("PERIODS(Month, , &To)"))) << text;
}
