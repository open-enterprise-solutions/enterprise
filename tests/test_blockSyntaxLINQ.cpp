// =============================================================================
// A LINQ block as a thing that can be built — backend/compiler/blockSyntaxLINQ.h.
//
// Two things are pinned: which query word may follow which (the table the
// editor's completion offers clauses from, and the constructor writes by), and
// the block the constructor writes — in the lexer's own words, one clause a
// line, refused rather than written when the order is not the language's.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/compiler/blockSyntaxLINQ.h"
#include "backend/compiler/codeDef.h"

#include <algorithm>

namespace {

bool Offers(int after, int key)
{
	const std::vector<int> allowed = ibLinqClausesAfter(after);
	return std::find(allowed.begin(), allowed.end(), key) != allowed.end();
}

ibLinqBlockSource Source(const wxString& alias, const wxString& expression)
{
	ibLinqBlockSource source;
	source.m_alias = alias;
	source.m_expression = expression;
	return source;
}

} // namespace

// ------------------------------ what may follow ------------------------------

TEST(LinqBlock, WhereTheGrammarWantsOneWordItOffersOnlyThat)
{
	EXPECT_EQ(std::vector<int>{ KEY_IN }, ibLinqClausesAfter(KEY_FROM));
	EXPECT_EQ(std::vector<int>{ KEY_IN }, ibLinqClausesAfter(KEY_JOIN));
	EXPECT_EQ(std::vector<int>{ KEY_EQUALS }, ibLinqClausesAfter(KEY_ON));
	EXPECT_EQ(std::vector<int>{ KEY_BY }, ibLinqClausesAfter(KEY_GROUP));
	EXPECT_EQ(std::vector<int>{ KEY_DISTINCT }, ibLinqClausesAfter(KEY_SELECT));
}

TEST(LinqBlock, AfterAClauseTheClausesFollow)
{
	for (const int key : { KEY_WHERE, KEY_SELECT, KEY_ORDERBY, KEY_GROUP, KEY_JOIN, KEY_TAKE, KEY_SKIP, KEY_FROM })
		EXPECT_TRUE(Offers(KEY_WHERE, key)) << key;
	// A group's key is followed by its name or by nothing: without a name the groups are the answer, and
	// the compiler reads no clause after them - a `Select` there was written and not read.
	EXPECT_EQ(std::vector<int>{ KEY_INTO }, ibLinqClausesAfter(KEY_BY));
	// After the name the query goes on over the groups.
	for (const int key : { KEY_WHERE, KEY_SELECT, KEY_ORDERBY, KEY_SKIP, KEY_TAKE })
		EXPECT_TRUE(Offers(KEY_INTO, key)) << key;
	EXPECT_FALSE(Offers(KEY_INTO, KEY_JOIN));
}

// ------------------------------- the block ----------------------------------

TEST(LinqBlock, ASourceAConditionAndColumns)
{
	ibLinqBlock block;
	block.m_sources.push_back(Source(wxT("g"), wxT("Data.Catalogs.Goods")));
	block.m_conditions.push_back(wxT("g.Price > limit"));   // a variable visible here is the parameter
	block.m_fields.push_back({ wxT("Name"), wxT("g.Description") });
	block.m_fields.push_back({ wxT("Price"), wxT("g.Price") });

	wxString refusal;
	EXPECT_EQ(wxT("From g In Data.Catalogs.Goods\n")
		wxT("  Where g.Price > limit\n")
		wxT("  Select { Name = g.Description, Price = g.Price }"),
		block.Render(wxT("  "), refusal));
	EXPECT_TRUE(refusal.IsEmpty());
}

TEST(LinqBlock, ASecondSourceIsAJoinOrAProduct)
{
	ibLinqBlock block;
	block.m_sources.push_back(Source(wxT("g"), wxT("Data.Catalogs.Goods")));
	ibLinqBlockSource prices = Source(wxT("p"), wxT("prices"));
	prices.m_join = true;
	prices.m_leftKey = wxT("g.Ref");
	prices.m_rightKey = wxT("p.Item");
	block.m_sources.push_back(prices);
	block.m_sources.push_back(Source(wxT("w"), wxT("Data.Catalogs.Warehouses")));

	wxString refusal;
	EXPECT_EQ(wxT("From g In Data.Catalogs.Goods\n")
		wxT("Join p In prices On g.Ref Equals p.Item\n")
		wxT("From w In Data.Catalogs.Warehouses\n")
		wxT("Select g"),
		block.Render(wxString(), refusal));
}

// The direction is written after the key it belongs to, so every key shares ONE clause - the middle one
// running the other way included.
TEST(LinqBlock, OrderKeysShareOneClause_EachWithItsOwnWay)
{
	ibLinqBlock block;
	block.m_sources.push_back(Source(wxT("g"), wxT("t")));
	block.m_order.push_back({ wxT("g.Group"), false });
	block.m_order.push_back({ wxT("g.Price"), true });
	block.m_order.push_back({ wxT("g.Name"), false });
	block.m_take = wxT("10");
	block.m_distinct = true;

	wxString refusal;
	EXPECT_EQ(wxT("From g In t\n")
		wxT("OrderBy g.Group, g.Price Descending, g.Name\n")
		wxT("Take 10\n")
		wxT("Select g\n")
		wxT("Distinct"),
		block.Render(wxString(), refusal));
}

// ------------------------------- grouping -----------------------------------

namespace {

ibLinqBlockTotal Total(const wxString& name, ibLinqTotal function, const wxString& expression)
{
	ibLinqBlockTotal total;
	total.m_name = name;
	total.m_function = function;
	total.m_expression = expression;
	return total;
}

} // namespace

// Every key is a column, reached from the group by its name.
TEST(LinqBlock, AGroupAnswersWithItsKeys)
{
	ibLinqBlock block;
	block.m_sources.push_back(Source(wxT("o"), wxT("orders")));
	block.m_groupKeys.push_back({ wxT("Customer"), wxT("o.Customer") });
	block.m_groupInto = wxT("byCustomer");

	wxString refusal;
	EXPECT_EQ(wxT("From o In orders\nGroup o By o.Customer Into byCustomer\nSelect { Customer = byCustomer.Key }"),
		block.Render(wxString(), refusal));
}

// ⭐ SEVERAL KEYS ARE MADE INTO ONE, which a group compares by its contents; a group keeps the one field its
// totals read, and a count reads none.
TEST(LinqBlock, SeveralKeysAreMadeIntoOneAndTotalsReadWhatTheGroupKeeps)
{
	ibLinqBlock block;
	block.m_sources.push_back(Source(wxT("o"), wxT("orders")));
	block.m_groupKeys.push_back({ wxT("Customer"), wxT("o.Customer") });
	block.m_groupKeys.push_back({ wxT("Warehouse"), wxT("o.Warehouse") });
	block.m_totals.push_back(Total(wxT("Amount"), ibLinqTotal::Sum, wxT("o.Amount")));
	block.m_totals.push_back(Total(wxT("Largest"), ibLinqTotal::Max, wxT("o.Amount")));
	block.m_totals.push_back(Total(wxT("Rows"), ibLinqTotal::Count, wxString()));
	block.m_groupInto = wxT("grp");

	wxString refusal;
	EXPECT_EQ(wxT("From o In orders\n")
		wxT("Group o.Amount By New Structure(\"Customer, Warehouse\", o.Customer, o.Warehouse) Into grp\n")
		wxT("Select { Customer = grp.Key.Customer, Warehouse = grp.Key.Warehouse, Amount = grp.Values.Sum(), ")
		wxT("Largest = grp.Values.Max(), Rows = grp.Values.Count() }"),
		block.Render(wxString(), refusal)) << refusal.ToStdString();
}

// The rows are filtered before grouping; a condition on groups and the order come after the name, and an
// order written by a total's name is that total.
TEST(LinqBlock, AfterTheNameTheQueryReadsTheGroups)
{
	ibLinqBlock block;
	block.m_sources.push_back(Source(wxT("o"), wxT("orders")));
	block.m_conditions.push_back(wxT("o.Posted"));
	block.m_groupKeys.push_back({ wxT("Customer"), wxT("o.Customer") });
	block.m_totals.push_back(Total(wxT("Amount"), ibLinqTotal::Sum, wxT("o.Amount")));
	block.m_groupInto = wxT("grp");
	block.m_groupConditions.push_back(wxT("grp.Values.Count() > 1"));
	block.m_order.push_back({ wxT("Amount"), true });
	block.m_order.push_back({ wxT("o.Customer"), false });
	block.m_take = wxT("10");

	wxString refusal;
	EXPECT_EQ(wxT("From o In orders\n")
		wxT("Where o.Posted\n")
		wxT("Group o.Amount By o.Customer Into grp\n")
		wxT("Where grp.Values.Count() > 1\n")
		wxT("OrderBy grp.Values.Sum() Descending, grp.Key\n")
		wxT("Take 10\n")
		wxT("Select { Customer = grp.Key, Amount = grp.Values.Sum() }"),
		block.Render(wxString(), refusal)) << refusal.ToStdString();
}

// Without a name the answer is the groups themselves, and nothing is read after them: the rows are
// ordered and cut BEFORE the group - the only place the compiler reads an order then.
TEST(LinqBlock, AGroupWithoutANameIsTheAnswer)
{
	ibLinqBlock block;
	block.m_sources.push_back(Source(wxT("o"), wxT("orders")));
	block.m_groupKeys.push_back({ wxT("Customer"), wxT("o.Customer") });
	block.m_skip = wxT("5");

	wxString refusal;
	EXPECT_EQ(wxT("From o In orders\nSkip 5\nGroup o By o.Customer"), block.Render(wxString(), refusal));

	// …so a column cannot be chosen: said, with the way out.
	block.m_totals.push_back(Total(wxT("Amount"), ibLinqTotal::Sum, wxT("o.Amount")));
	EXPECT_TRUE(block.Render(wxString(), refusal).IsEmpty());
	EXPECT_FALSE(refusal.IsEmpty());
}

// After grouping the rows are gone: a column still reading one is refused, not written for the compiler to
// meet as a name it cannot reach.
TEST(LinqBlock, AColumnReadingARowIsRefusedOnceTheQueryGroups)
{
	ibLinqBlock block;
	block.m_sources.push_back(Source(wxT("o"), wxT("orders")));
	block.m_groupKeys.push_back({ wxT("Customer"), wxT("o.Customer") });
	block.m_groupInto = wxT("grp");
	block.m_fields.push_back({ wxT("Date"), wxT("o.Date") });

	wxString refusal;
	EXPECT_TRUE(block.Render(wxString(), refusal).IsEmpty());
	EXPECT_NE(wxNOT_FOUND, refusal.Find(wxT("'o'"))) << refusal.ToStdString();

	// Written over the group, it stands.
	block.m_fields.front().m_expression = wxT("grp.Values");
	EXPECT_FALSE(block.Render(wxString(), refusal).IsEmpty()) << refusal.ToStdString();
}

TEST(LinqBlock, TotalsWithNothingGroupedAreRefused)
{
	ibLinqBlock block;
	block.m_sources.push_back(Source(wxT("o"), wxT("orders")));
	block.m_totals.push_back(Total(wxT("Amount"), ibLinqTotal::Sum, wxT("o.Amount")));

	wxString refusal;
	EXPECT_TRUE(block.Render(wxString(), refusal).IsEmpty());
	EXPECT_FALSE(refusal.IsEmpty());
}

// ------------------------------ a number of rows -----------------------------

TEST(LinqBlock, SkipAndTakeTakeANumberOfRows)
{
	ibLinqBlock block;
	block.m_sources.push_back(Source(wxT("o"), wxT("orders")));
	wxString refusal;

	for (const wxChar* count : { wxT("10"), wxT("pageSize"), wxT("pageSize * 2") }) {
		block.m_take = count;
		EXPECT_FALSE(block.Render(wxString(), refusal).IsEmpty()) << count;
	}
	for (const wxChar* notACount : { wxT("\"10\""), wxT("10.5"), wxT("'x'") }) {
		block.m_skip = notACount;
		EXPECT_TRUE(block.Render(wxString(), refusal).IsEmpty()) << notACount;
		EXPECT_NE(wxNOT_FOUND, refusal.Find(wxT("Skip"))) << refusal.ToStdString();
	}
}

// ----------------------------- reading it back ------------------------------

namespace {

// Written, read, written again: the same text. What the constructor opens is what it would write.
wxString RoundTrip(const ibLinqBlock& block)
{
	wxString refusal;
	const wxString written = block.Render(wxString(), refusal);
	ibLinqBlock read;
	EXPECT_TRUE(ibLinqBlock::Parse(written, read, refusal)) << refusal.ToStdString();
	return read.Render(wxString(), refusal);
}

} // namespace

TEST(LinqBlock, EveryClauseReadsBackAsItWasWritten)
{
	ibLinqBlock block;
	block.m_sources.push_back(Source(wxT("g"), wxT("Data.Catalogs.Goods")));
	ibLinqBlockSource prices = Source(wxT("p"), wxT("prices"));
	prices.m_join = true;
	prices.m_leftKey = wxT("g.Ref");
	prices.m_rightKey = wxT("p.Item");
	block.m_sources.push_back(prices);
	block.m_conditions.push_back(wxT("g.Price > limit"));
	block.m_conditions.push_back(wxT("Not g.DeletionMark"));
	block.m_order.push_back({ wxT("g.Description"), false });
	block.m_order.push_back({ wxT("p.Price"), true });
	block.m_skip = wxT("5");
	block.m_take = wxT("10");
	block.m_fields.push_back({ wxT("Name"), wxT("g.Description") });
	block.m_fields.push_back({ wxT("Price"), wxT("Round(p.Price, 2)") });   // a comma inside brackets is not a column's
	block.m_distinct = true;

	wxString refusal;
	EXPECT_EQ(block.Render(wxString(), refusal), RoundTrip(block));
}

// ⭐ A GROUPED BLOCK READS BACK AS ITS PARTS, not as columns of text: the keys, the totals, the conditions
// on groups and an order by a total - several kept fields included, whose totals pick theirs out with a
// function spelled in the module's syntax.
TEST(LinqBlock, AGroupReadsBackWithItsKeysAndTotals)
{
	ibLinqBlock block;
	block.m_sources.push_back(Source(wxT("o"), wxT("orders")));
	block.m_conditions.push_back(wxT("o.Posted"));
	block.m_groupKeys.push_back({ wxT("Customer"), wxT("o.Customer") });
	block.m_groupKeys.push_back({ wxT("Warehouse"), wxT("o.Warehouse") });
	block.m_totals.push_back(Total(wxT("Amount"), ibLinqTotal::Sum, wxT("o.Amount")));
	block.m_totals.push_back(Total(wxT("Quantity"), ibLinqTotal::Average, wxT("o.Quantity")));
	block.m_totals.push_back(Total(wxT("Rows"), ibLinqTotal::Count, wxString()));
	block.m_groupInto = wxT("grp");
	block.m_groupConditions.push_back(wxT("grp.Values.Count() > 1"));
	block.m_order.push_back({ wxT("Amount"), true });
	block.m_fields.push_back({ wxT("Lines"), wxT("grp.Values") });

	wxString refusal;
	const wxString written = block.Render(wxString(), refusal);
	ASSERT_FALSE(written.IsEmpty()) << refusal.ToStdString();
	EXPECT_EQ(written, RoundTrip(block));

	ibLinqBlock read;
	ASSERT_TRUE(ibLinqBlock::Parse(written, read, refusal)) << refusal.ToStdString();
	ASSERT_EQ(2u, read.m_groupKeys.size());
	EXPECT_EQ(wxT("o.Warehouse"), read.m_groupKeys[1].m_expression);
	ASSERT_EQ(3u, read.m_totals.size());
	EXPECT_TRUE(read.m_totals[1].m_function == ibLinqTotal::Average);
	EXPECT_EQ(wxT("o.Quantity"), read.m_totals[1].m_expression);
	ASSERT_EQ(1u, read.m_conditions.size());
	ASSERT_EQ(1u, read.m_groupConditions.size());
	ASSERT_EQ(1u, read.m_order.size());
	EXPECT_EQ(wxT("Amount"), read.m_order[0].m_expression);
	ASSERT_EQ(1u, read.m_fields.size());
}

// Written by a person: one key and one field kept, the key named by its column.
TEST(LinqBlock, AGroupWrittenByHandIsRead)
{
	ibLinqBlock block;
	wxString refusal;
	ASSERT_TRUE(ibLinqBlock::Parse(
		wxT("From t In transactions\n")
		wxT("  Group t.Amount By t.Country Into g\n")
		wxT("  Select { Country = g.Key, Total = g.Values.Sum(), Names = g.Values }"), block, refusal)) << refusal.ToStdString();

	ASSERT_EQ(1u, block.m_groupKeys.size());
	EXPECT_EQ(wxT("Country"), block.m_groupKeys[0].m_name);
	EXPECT_EQ(wxT("t.Country"), block.m_groupKeys[0].m_expression);
	ASSERT_EQ(1u, block.m_totals.size());
	EXPECT_EQ(wxT("Total"), block.m_totals[0].m_name);
	EXPECT_EQ(wxT("t.Amount"), block.m_totals[0].m_expression);
	ASSERT_EQ(1u, block.m_fields.size());
	EXPECT_EQ(wxT("g.Values"), block.m_fields[0].m_expression);
}

// An order written by hand reads back key by key, each with the way written after it - the key in the
// middle included, and a key that names none running ascending.
TEST(LinqBlock, AnOrderReadsBackKeyByKey)
{
	ibLinqBlock block;
	wxString refusal;
	ASSERT_TRUE(ibLinqBlock::Parse(
		wxT("From o In orders\n")
		wxT("  OrderBy o.Warehouse, o.Date Descending, o.Item Ascending, o.Line\n")
		wxT("  Select o"), block, refusal)) << refusal.ToStdString();

	ASSERT_EQ(4u, block.m_order.size());
	EXPECT_EQ(wxT("o.Warehouse"), block.m_order[0].m_expression);
	EXPECT_FALSE(block.m_order[0].m_descending);
	EXPECT_EQ(wxT("o.Date"), block.m_order[1].m_expression);
	EXPECT_TRUE(block.m_order[1].m_descending);
	EXPECT_EQ(wxT("o.Item"), block.m_order[2].m_expression);
	EXPECT_FALSE(block.m_order[2].m_descending);
	EXPECT_EQ(wxT("o.Line"), block.m_order[3].m_expression);
	EXPECT_FALSE(block.m_order[3].m_descending);
}

// Written by a person, not by the constructor: other line breaks, other spacing, one column unnamed.
TEST(LinqBlock, AQueryWrittenByHandIsRead)
{
	ibLinqBlock block;
	wxString refusal;
	ASSERT_TRUE(ibLinqBlock::Parse(
		wxT("  From c In Data.Catalogs.Counterparties\n")
		wxT("\t\tWhere c.TaxId <> \"\" And c.Phone = phone   // a comment, with a From in it\n")
		wxT("\t\tSelect c.Description"), block, refusal)) << refusal.ToStdString();

	ASSERT_EQ(1u, block.m_sources.size());
	EXPECT_EQ(wxT("c"), block.m_sources[0].m_alias);
	EXPECT_EQ(wxT("Data.Catalogs.Counterparties"), block.m_sources[0].m_expression);
	// The comment after the condition - with a query word in it - is neither part of the condition nor
	// the start of a clause.
	ASSERT_EQ(1u, block.m_conditions.size());
	EXPECT_EQ(wxT("c.TaxId <> \"\" And c.Phone = phone"), block.m_conditions[0]);
	ASSERT_EQ(1u, block.m_fields.size());
	EXPECT_TRUE(block.m_fields[0].m_name.IsEmpty());
	EXPECT_EQ(wxT("c.Description"), block.m_fields[0].m_expression);

	// …and written back bare, as it was.
	EXPECT_NE(wxNOT_FOUND, block.Render(wxString(), refusal).Find(wxT("Select c.Description")));
}

TEST(LinqBlock, APartTheBlockDoesNotModelIsRefusedNotDropped)
{
	ibLinqBlock block;
	wxString refusal;
	EXPECT_FALSE(ibLinqBlock::Parse(wxT("From o In orders Var total = o.Qty * o.Price Select total"), block, refusal));
	EXPECT_FALSE(refusal.IsEmpty());
	EXPECT_FALSE(ibLinqBlock::Parse(wxT("x = 1"), block, refusal));
}

// ------------------------------- refusals -----------------------------------

TEST(LinqBlock, NothingToReadIsRefused)
{
	wxString refusal;
	EXPECT_TRUE(ibLinqBlock().Render(wxString(), refusal).IsEmpty());
	EXPECT_FALSE(refusal.IsEmpty());
}

TEST(LinqBlock, AJoinWithoutItsKeysIsRefused)
{
	ibLinqBlock block;
	block.m_sources.push_back(Source(wxT("g"), wxT("goods")));
	ibLinqBlockSource prices = Source(wxT("p"), wxT("prices"));
	prices.m_join = true;
	prices.m_leftKey = wxT("g.Ref");
	block.m_sources.push_back(prices);

	wxString refusal;
	EXPECT_TRUE(block.Render(wxString(), refusal).IsEmpty());
	EXPECT_NE(wxNOT_FOUND, refusal.Find(wxT("'p'"))) << refusal.ToStdString();
}

// ⭐ A DOT WITH NOTHING AFTER IT - the join keys start as `g.` - is refused: written into a module the lexer
// reads the next word as the field, `g. Equals` as `g.Equals`, and the compiler stops mid-query. The draft
// still shows the block as it stands, so a preview does not go blank at the first wrong cell.
TEST(LinqBlock, AnUnfinishedFieldIsRefusedAndTheDraftStillShowsIt)
{
	ibLinqBlock block;
	block.m_sources.push_back(Source(wxT("g"), wxT("goods")));
	ibLinqBlockSource prices = Source(wxT("p"), wxT("prices"));
	prices.m_join = true;
	prices.m_leftKey = wxT("g.");
	prices.m_rightKey = wxT("p.");
	block.m_sources.push_back(prices);

	wxString refusal;
	EXPECT_TRUE(block.Render(wxString(), refusal).IsEmpty());
	EXPECT_NE(wxNOT_FOUND, refusal.Find(wxT("'g.'"))) << refusal.ToStdString();

	const wxString draft = block.RenderDraft(wxString(), refusal);
	EXPECT_NE(wxNOT_FOUND, draft.Find(wxT("On g. Equals p."))) << draft.ToStdString();
	EXPECT_FALSE(refusal.IsEmpty());
}

// A query left half-written is read as it was left: a query word after a dangling dot is the query's, and
// the query ends with its answer - not where the text handed over does, nor where the compiler stopped.
TEST(LinqBlock, AQueryLeftHalfWrittenIsReadToItsEnd)
{
	const wxString query =
		wxT("From g In goods\n")
		wxT("    Join c In counterparties On g. Equals c.\n")
		wxT("    Where c.DeletionMark = 1\n")
		wxT("    Group g.Number By g.Ref Into grp\n")
		wxT("    Select { Ref = grp.Key, Number = grp.Values.Average(), Column1 = 4 }");

	ibLinqBlock block;
	wxString refusal;
	size_t consumed = 0;
	ASSERT_TRUE(ibLinqBlock::Parse(query + wxT("\nMessage(q);\nx = 1;"), block, refusal, &consumed)) << refusal.ToStdString();
	EXPECT_EQ(query.length(), consumed);

	ASSERT_EQ(2u, block.m_sources.size());
	EXPECT_EQ(wxT("g."), block.m_sources[1].m_leftKey);
	EXPECT_EQ(wxT("c."), block.m_sources[1].m_rightKey);
	ASSERT_EQ(1u, block.m_conditions.size());
	EXPECT_EQ(wxT("c.DeletionMark = 1"), block.m_conditions[0]);
	ASSERT_EQ(1u, block.m_groupKeys.size());
	EXPECT_EQ(wxT("Ref"), block.m_groupKeys[0].m_name);
	ASSERT_EQ(1u, block.m_totals.size());
	EXPECT_TRUE(block.m_totals[0].m_function == ibLinqTotal::Average);
	EXPECT_EQ(wxT("g.Number"), block.m_totals[0].m_expression);
	ASSERT_EQ(1u, block.m_fields.size());
	EXPECT_EQ(wxT("4"), block.m_fields[0].m_expression);
}

// Rows ordered before a NAMED group: the constructor writes an order over the groups, after the name, so
// reading this one as that would change the answer - refused, not reinterpreted.
TEST(LinqBlock, RowsOrderedBeforeANamedGroupAreRefusedNotMoved)
{
	ibLinqBlock block;
	wxString refusal;
	EXPECT_FALSE(ibLinqBlock::Parse(
		wxT("From o In orders OrderBy o.Date Group o By o.Customer Into grp Select { Customer = grp.Key }"), block, refusal));
	EXPECT_FALSE(refusal.IsEmpty());
}
