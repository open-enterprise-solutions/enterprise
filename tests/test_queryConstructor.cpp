// The query constructor's MODEL half (backend/query/queryConstructorModel.{h,cpp}).
//
// Pure: no database, no session, no window. What is pinned here is the part of the constructor
// that can be wrong SILENTLY — which sources a statement may select from, and where the fields of
// a source come from. The tabs above it are a view; these are the answers they draw.
//
// The two properties worth a test each, both stated in docs/private/query-constructor.md §5b:
//
//   * a temp table becomes a source for the statements AFTER the one that made it — and for no
//     others, because a package is read top to bottom;
//   * the fields of a source are asked OF THE SOURCE: a nested table and a temp table answer with
//     their own projections, and nothing branches on a type to find that out.

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include "backend/query/queryConstructorModel.h"
#include "core/query/queryParser.h"

namespace {

ibQueryPackage Package(const ibString& text)
{
	ibQueryParser parser;
	return parser.ParsePackage(text);
}

// The package the temp-table rules are read off: make Sales, read it, drop it, then a statement
// that stands after the drop.
ibQueryPackage SalesPackage()
{
	return Package(
		wxT("SELECT Ref INTO Sales FROM Document.Orders;")
		wxT("SELECT Ref FROM Sales;")
		wxT("DROP Sales;")
		wxT("SELECT Ref FROM Catalog.Products"));
}

std::vector<ibString> Names(const std::vector<ibQueryConstructorSource>& sources)
{
	std::vector<ibString> out;
	for (const ibQueryConstructorSource& source : sources)
		out.push_back(source.Text());
	return out;
}

std::vector<ibString> Names(const std::vector<ibQueryConstructorField>& fields)
{
	std::vector<ibString> out;
	for (const ibQueryConstructorField& field : fields)
		out.push_back(field.m_name);
	return out;
}

} // namespace

TEST(QueryConstructorModel, TheFirstStatementSeesNoTempTable)
{
	// Nothing has been made yet. Offering `Sales` here would produce a query that reads a name
	// nothing has filled — and it would parse, which is exactly what makes it worth a test.
	const std::vector<ibQueryConstructorSource> sources =
		ibQueryConstructorModel::GetTempSources(SalesPackage(), 0);
	EXPECT_TRUE(sources.empty());
}

TEST(QueryConstructorModel, AStatementSeesWhatTheOnesBeforeItLeft)
{
	const std::vector<ibString> names = Names(ibQueryConstructorModel::GetTempSources(SalesPackage(), 1));
	ASSERT_EQ(1u, names.size());
	EXPECT_EQ(wxT("Sales"), names[0]);
}

TEST(QueryConstructorModel, ADropTakesTheNameBackOutOfTheList)
{
	// "Release early" MEANS the table is gone — a constructor still offering it would be offering
	// something that no longer exists, and the engine would refuse the query it wrote.
	const std::vector<ibQueryConstructorSource> before =
		ibQueryConstructorModel::GetTempSources(SalesPackage(), 2);   // after the read, before the drop
	EXPECT_EQ(1u, before.size());

	const std::vector<ibQueryConstructorSource> after =
		ibQueryConstructorModel::GetTempSources(SalesPackage(), 4);   // after the drop
	EXPECT_TRUE(after.empty());
}

TEST(QueryConstructorModel, ATempSourceIsABareNameAndSaysItIsTemporary)
{
	const std::vector<ibQueryConstructorSource> sources =
		ibQueryConstructorModel::GetTempSources(SalesPackage(), 1);
	ASSERT_EQ(1u, sources.size());
	EXPECT_TRUE(sources[0].m_temp);
	ASSERT_EQ(1u, sources[0].m_path.size()) << "a temp table has no <Kind>. prefix — it has no metaclass";
	EXPECT_EQ(wxT("Sales"), sources[0].Text());
}

TEST(QueryConstructorModel, ANestedTableAnswersWithItsOwnProjections)
{
	// The design consequence worth pinning: for a nested table the field list comes from the INNER
	// query, not from a descriptor. Both are answers to one question, and the caller asks the
	// source rather than branching on which kind it is.
	ibQueryParser parser;
	const ibQuerySelectPtr outer = parser.Parse(
		wxT("SELECT Code FROM (SELECT Code, Name FROM Catalog.Products) AS sub"));
	ASSERT_NE(nullptr, outer);
	ASSERT_NE(nullptr, outer->m_from.m_subquery);

	ibQueryConstructorModel model(nullptr);
	const std::vector<ibString> fields = Names(model.GetFields(outer->m_from, ibQueryPackage(), 0));

	ASSERT_EQ(2u, fields.size());
	EXPECT_EQ(wxT("Code"), fields[0]);
	EXPECT_EQ(wxT("Name"), fields[1]);
}

// ⭐⭐ A COMPOSITION READS ITS QUERY WITHOUT THE ROLES IT SAYS — its Fields page says them (Max, 2026-09-29).
// Every select forgets them, the one it reads FROM and a union's branch included, since a role travels up
// with its column; a text that says none comes back as it was, byte for byte.
TEST(QueryConstructorModel, ACompositionReadsItsQueryWithoutTheRolesItSays)
{
	const ibString plain = wxT("SELECT T.Qty AS Qty FROM Catalog.Products AS T");
	EXPECT_EQ(plain, ibQueryTextWithoutRoles(plain));

	const ibString said =
		wxT("SELECT U.Start ROLE OPENING AS Start, U.Day ROLE PERIOD 2 AS Day ")
		wxT("FROM (SELECT T.Qty ROLE CLOSING AS Start, T.Date AS Day FROM Catalog.Products AS T ")
		wxT("      UNION ALL SELECT S.Qty ROLE DIMENSION, S.Date FROM Catalog.Stock AS S) AS U");
	ibQueryPackage package = Package(said);
	EXPECT_TRUE(ibQueryDropRoles(package));
	EXPECT_FALSE(ibQueryDropRoles(package)) << "a role was left somewhere the walk did not reach";

	const ibString text = ibQueryTextWithoutRoles(said);
	EXPECT_EQ(wxNOT_FOUND, text.Upper().Find(wxT("ROLE"))) << text.ToStdString();
	// …and it still reads, its fields where they were.
	const ibQueryPackage back = Package(text);
	ASSERT_EQ(1u, back.m_statements.size());
	EXPECT_EQ(2u, back.m_statements[0].m_select->m_projections.size());
}

TEST(QueryConstructorModel, ANestedTableUsesTheAliasWhereTheAuthorGaveOne)
{
	ibQueryParser parser;
	const ibQuerySelectPtr outer = parser.Parse(
		wxT("SELECT x FROM (SELECT Price * 2 AS x FROM Catalog.Products) AS sub"));
	ASSERT_NE(nullptr, outer);

	ibQueryConstructorModel model(nullptr);
	const std::vector<ibString> fields = Names(model.GetFields(outer->m_from, ibQueryPackage(), 0));
	ASSERT_EQ(1u, fields.size());
	EXPECT_EQ(wxT("x"), fields[0]) << "the alias is what the outer query refers the column by";
}

TEST(QueryConstructorModel, ATempTableAnswersWithTheProjectionsOfTheStatementThatMadeIt)
{
	const ibQueryPackage package = Package(
		wxT("SELECT Ref, Price AS Amount INTO Sales FROM Document.Orders;")
		wxT("SELECT Ref FROM Sales"));
	ASSERT_EQ(2u, package.m_statements.size());

	ibQueryConstructorModel model(nullptr);
	const std::vector<ibString> fields =
		Names(model.GetFields(package.m_statements[1].m_select->m_from, package, 1));

	ASSERT_EQ(2u, fields.size());
	EXPECT_EQ(wxT("Ref"),    fields[0]);
	EXPECT_EQ(wxT("Amount"), fields[1]);
}

TEST(QueryConstructorModel, ATempTableThatWasNeverMadeHasNoFields)
{
	// Not a crash and not an invented column list: a name nothing filled has nothing in it, and
	// saying so is what lets the shell show an empty table rather than a wrong one.
	const ibQueryPackage package = Package(wxT("SELECT Ref FROM Sales"));
	ibQueryConstructorModel model(nullptr);
	EXPECT_TRUE(model.GetFields(package.m_statements[0].m_select->m_from, package, 0).empty());
}

TEST(QueryConstructorModel, QualifiedFieldsCarryTheAliasTheQueryWrites)
{
	// With more than one table in play, a field is written with its prefix — and the prefix is the
	// alias where the author gave one, because that is what the lowering resolves by.
	ibQueryParser parser;
	const ibQuerySelectPtr outer = parser.Parse(
		wxT("SELECT p.Code FROM (SELECT Code FROM Catalog.Products) AS p"));
	ASSERT_NE(nullptr, outer);

	ibQueryConstructorModel model(nullptr);
	const std::vector<ibString> fields = Names(model.GetQualifiedFields(outer->m_from, ibQueryPackage(), 0));
	ASSERT_EQ(1u, fields.size());
	EXPECT_EQ(wxT("p.Code"), fields[0]);
}

TEST(QueryConstructorModel, WithNoApplicationTheCatalogueIsEmptyRatherThanAFailure)
{
	// No application data, no application: the constructor still opens, and its table list is simply
	// empty. A catalogue that threw here would make the window unopenable in exactly the situation
	// where somebody is trying to look at a query.
	ibQueryConstructorModel model(nullptr);
	EXPECT_NO_THROW((void)model.GetSources());
}

// ===========================================================================
//  WHERE A WRITTEN PATH STARTS — one rule for every holder of fields (WalkPath): the query's own
//  sources, a composer's source, a settings window's list. They used to find the start each in its
//  own way, and a line the settings window listed could be one the run left out.
// ===========================================================================

namespace {

ibQueryConstructorField Field(const ibString& name, bool available = true)
{
	ibQueryConstructorField field;
	field.m_name      = name;
	field.m_available = available;
	return field;
}

} // namespace

TEST(QueryConstructorModel, APathStartsAtTheLongestPrefixAFieldIsNamed)
{
	// A linked package names its fields with the selection in front — `Sales.Qty` is ONE field, and
	// reading its first segment alone would find `Sales`, a different one.
	ibQueryConstructorModel model(nullptr);
	const std::vector<ibQueryConstructorField> fields = { Field(wxT("Sales")), Field(wxT("Sales.Qty"), false) };
	const ibQueryConstructorField leaf = model.WalkPath(fields, { wxT("Sales"), wxT("Qty") });
	EXPECT_EQ(wxT("Sales.Qty"), leaf.m_name);
	EXPECT_FALSE(leaf.m_available) << "the field the path names answers, not its first segment";
}

TEST(QueryConstructorModel, APathNoFieldStartsIsNoVerdict)
{
	// Silence, never a guess: a name the holder does not have is not hidden — it is not this question.
	ibQueryConstructorModel model(nullptr);
	const ibQueryConstructorField leaf = model.WalkPath({ Field(wxT("Qty")) }, { wxT("Amount") });
	EXPECT_TRUE(leaf.m_name.IsEmpty());
	EXPECT_TRUE(leaf.m_available);
}

TEST(QueryConstructorModel, AHiddenStartHidesTheWalkPastAHopNobodyAnswers)
{
	// What the walk KNEW stands: a hop past a hidden field that nothing resolves (here there is no
	// application to walk into) does not bring the hidden field back.
	ibQueryConstructorModel model(nullptr);
	const ibQueryConstructorField leaf = model.WalkPath({ Field(wxT("Warehouse"), false) }, { wxT("Warehouse"), wxT("Code") });
	EXPECT_FALSE(leaf.m_available);
}

// ===========================================================================
//  The temp-data store — WHO keeps a temp table, or a lock, alive.
//
//  The question the store answers is not about queries but about ownership: without a holder a
//  temp table lives for one execution, with one it lives until the holder lets go. That is the
//  whole difference between a batch and a `TempDataManager`, and it is testable without a
//  session, a database or a window.
// ===========================================================================

#include "backend/query/queryTempStore.h"
#include "backend/query/queryable.h"      // ibBackendQueryable — a stored table IS one

namespace {

// A LOCK's holds, as its tables' providers give them — what each one is, the store does not know (here an int: a lock
// no base was asked for).
using ibTestHolds = std::vector<std::shared_ptr<void>>;

ibQueryRamTable OneRow(const ibString& column, const ibValue& cell)
{
	ibQueryRamTable table;
	table.AddColumn(1, column, ibTypeDescription());
	const long row = table.AppendRow();
	table.SetCell(row, 1, cell);
	return table;
}

} // namespace

TEST(QueryTempStore, AFreshStoreHoldsNothing)
{
	ibQueryTempDataStore store;
	EXPECT_TRUE(store.Empty());
	EXPECT_FALSE(store.Has(ibT("Sales")));
	EXPECT_TRUE(store.Sources().empty());
}

TEST(QueryTempStore, WhatIsPutInCanBeFoundByName)
{
	ibQueryTempDataStore store;
	store.Put(wxT("Sales"), OneRow(wxT("Ref"), ibValue(1)));

	EXPECT_TRUE(store.Has(ibT("Sales")));
	ASSERT_EQ(1u, store.Sources().size());
	EXPECT_NE(nullptr, store.Sources().at(wxT("Sales")));
}

TEST(QueryTempStore, ADroppedTableIsGoneFromBothTheNameAndTheRows)
{
	// A name removed while its rows stayed alive would be memory nobody can reach and nobody can
	// free — the reason Drop touches both.
	ibQueryTempDataStore store;
	store.Put(wxT("Sales"), OneRow(wxT("Ref"), ibValue(1)));

	EXPECT_TRUE(store.Drop(wxT("Sales")));
	EXPECT_FALSE(store.Has(ibT("Sales")));
	EXPECT_TRUE(store.Empty());
}

TEST(QueryTempStore, DroppingWhatWasNeverThereSaysSo)
{
	// False, not a shrug: the caller turns it into "that table does not exist", which is a typo
	// or a statement that was expected to run and did not.
	ibQueryTempDataStore store;
	EXPECT_FALSE(store.Drop(wxT("Sales")));
}

TEST(QueryTempStore, CloseReleasesEverythingAtOnce)
{
	// This is `TempDataManager.Close()` — the one decision there is to make about a set of
	// temp tables.
	ibQueryTempDataStore store;
	store.Put(wxT("Sales"),  OneRow(wxT("Ref"), ibValue(1)));
	store.Put(wxT("Orders"), OneRow(wxT("Ref"), ibValue(2)));
	ASSERT_EQ(2u, store.Sources().size());

	store.Close();
	EXPECT_TRUE(store.Empty());
	EXPECT_FALSE(store.Has(ibT("Sales")));
	EXPECT_FALSE(store.Has(ibT("Orders")));
}

TEST(QueryTempStore, ALockSharesTheTablesNamesAndIsLetGoByUnlock)
{
	// One name space: `INTO Held` after `LOCK Held FROM …` would name two things. DROP is a table's verb,
	// UNLOCK the lock's. A lock's name is matched as the language matches names — without regard to case.
	// …and UNLOCK empties what the store keeps NOW: a manager's lock goes (a transaction keeps its own share, to its end).
	ibQueryTempDataStore store;
	const auto transactions = std::make_shared<ibTestHolds>(1, std::make_shared<int>(0));
	store.Put(ibT("Held"), transactions);

	EXPECT_TRUE(store.Has(ibT("held")));
	EXPECT_FALSE(store.Empty());
	EXPECT_FALSE(store.Drop(ibT("Held")));
	EXPECT_TRUE(store.Unlock(ibT("HELD")));
	EXPECT_FALSE(store.Has(ibT("Held")));
	EXPECT_TRUE(store.Empty());
	EXPECT_TRUE(transactions->empty());
}

TEST(QueryTempStore, AnUnnamedLockIsLetGoOnlyByClose)
{
	// A LOCK without a name (`LOCK <name> FROM`) is none an UNLOCK can say — the manager holds it until it is closed or gone.
	ibQueryTempDataStore store;
	store.Put(ibString(), std::make_shared<ibTestHolds>());

	EXPECT_FALSE(store.Has(ibString()));
	EXPECT_FALSE(store.Unlock(ibString()));
	EXPECT_FALSE(store.Empty());
	store.Close();
	EXPECT_TRUE(store.Empty());
}

TEST(QueryTempStore, ForgettingTheLocksLeavesTheOtherHolderItsShare)
{
	// A package's own store forgets its locks at its end: the transaction holds them from there, and
	// a share left in the store would hold them as long as a result of that package is read.
	const auto transactions = std::make_shared<ibTestHolds>(1, std::make_shared<int>(0));
	{
		ibQueryTempDataStore store;
		store.Put(ibT("Held"), transactions);
		EXPECT_EQ(2, transactions.use_count());
		store.ForgetLocks();
		EXPECT_EQ(1, transactions.use_count());
		EXPECT_FALSE(store.Has(ibT("Held")));

		// …and so does the store's end: a manager that is gone holds nothing.
		store.Put(ibT("Held"), transactions);
	}
	EXPECT_EQ(1, transactions.use_count());
}

TEST(QueryTempStore, AStoredTableKeepsItsColumnsAndRows)
{
	// The table is handed out BY COPY per read, because a temp table is read more than once by
	// design — that is the point of putting it somewhere.
	ibQueryTempDataStore store;
	store.Put(wxT("Sales"), OneRow(wxT("Ref"), ibValue(ibString(wxT("A")))));

	const std::shared_ptr<const ibBackendQueryable> source = store.Sources().at(wxT("Sales"));
	ASSERT_NE(nullptr, source);

	const std::vector<std::shared_ptr<const ibBackendQueryColumn>> columns = source->GetQueryColumns();
	ASSERT_EQ(1u, columns.size());
	EXPECT_EQ(wxT("Ref"), columns[0]->GetName());

	const ibQueryRamTable first  = source->ComputeRows({});
	const ibQueryRamTable second = source->ComputeRows({});
	EXPECT_EQ(1, first.RowCount());
	EXPECT_EQ(1, second.RowCount()) << "a second read must see the same rows, not an emptied table";
	EXPECT_EQ(wxT("A"), second.GetCell(0, 1).GetString());
}

// ===========================================================================
//  An INDEX is not a word the text carries — it is what a read costs.
// ===========================================================================

TEST(QueryTempStore, AnIndexedEqualityReadsOnlyTheMatchingRows)
{
	// The point of the whole clause: with an index, a read filtering that column is answered from
	// the map instead of by walking the table. The rows that come back must be exactly the matching
	// ones — an index changes how many rows are looked at, never which ones match.
	ibQueryRamTable table;
	table.AddColumn(1, wxT("Code"), ibTypeDescription());
	for (const wxChar* code : { wxT("A"), wxT("B"), wxT("A"), wxT("C") }) {
		const long row = table.AppendRow();
		table.SetCell(row, 1, ibValue(ibString(code)));
	}

	ibQueryTempDataStore store;
	store.Put(wxT("Sales"), std::move(table), { wxT("Code") });

	const std::shared_ptr<const ibBackendQueryable> source = store.Sources().at(wxT("Sales"));
	ASSERT_NE(nullptr, source);
	const std::vector<std::shared_ptr<const ibBackendQueryColumn>> columns = source->GetQueryColumns();
	ASSERT_EQ(1u, columns.size());

	ibQueryCondition equality;
	equality.m_col   = columns[0];
	equality.m_op    = ibQueryFilterOp::Equal;
	equality.m_value = ibValue(ibString(wxT("A")));

	const ibQueryRamTable matched = source->ComputeRows({ equality });
	EXPECT_EQ(2, matched.RowCount()) << "two rows carry A";
	EXPECT_EQ(wxT("A"), matched.GetCell(0, 1).GetString());
	EXPECT_EQ(wxT("A"), matched.GetCell(1, 1).GetString());
}

TEST(QueryTempStore, AnIndexedValueNothingCarriesReadsNothing)
{
	// The trap this guards: "not in the index" must mean NO rows, not ALL rows — a miss that fell
	// back to the full table would turn a filter into no filter.
	ibQueryRamTable table;
	table.AddColumn(1, wxT("Code"), ibTypeDescription());
	const long row = table.AppendRow();
	table.SetCell(row, 1, ibValue(ibString(wxT("A"))));

	ibQueryTempDataStore store;
	store.Put(wxT("Sales"), std::move(table), { wxT("Code") });

	const std::shared_ptr<const ibBackendQueryable> source = store.Sources().at(wxT("Sales"));
	ibQueryCondition equality;
	equality.m_col   = source->GetQueryColumns()[0];
	equality.m_op    = ibQueryFilterOp::Equal;
	equality.m_value = ibValue(ibString(wxT("Z")));

	EXPECT_EQ(0, source->ComputeRows({ equality }).RowCount());
}

TEST(QueryTempStore, WithoutAnIndexEveryRowIsStillReturned)
{
	// No index, no change: the read walks the table exactly as it did before the clause existed.
	ibQueryRamTable table;
	table.AddColumn(1, wxT("Code"), ibTypeDescription());
	for (const wxChar* code : { wxT("A"), wxT("B") }) {
		const long row = table.AppendRow();
		table.SetCell(row, 1, ibValue(ibString(code)));
	}

	ibQueryTempDataStore store;
	store.Put(wxT("Sales"), std::move(table));   // no indexed columns

	const std::shared_ptr<const ibBackendQueryable> source = store.Sources().at(wxT("Sales"));
	ibQueryCondition equality;
	equality.m_col   = source->GetQueryColumns()[0];
	equality.m_op    = ibQueryFilterOp::Equal;
	equality.m_value = ibValue(ibString(wxT("A")));

	EXPECT_EQ(2, source->ComputeRows({ equality }).RowCount())
		<< "the filter is applied downstream — the store hands back what it has";
}

// ===========================================================================
//  A second reading of a stored table keys its rows by its own columns.
// ===========================================================================

TEST(QueryTempStore, ASecondReadingKeysItsRowsByItsOwnColumns)
{
	// Stored tables number their columns alike (1, 2, …), so the second of two in one statement is read through a twin
	// (ibAliasQueryable), and its readers key the rows by the twin's columns: the copy into a base in memory, a
	// semi-join's keys. Under the origin's numbers the copy found none of its columns and was refused (2026-11-19).
	ibQueryRamTable table;
	table.AddColumn(1, wxT("Code"), ibTypeDescription());
	for (const wxChar* code : { wxT("A"), wxT("B"), wxT("A") }) {
		const long row = table.AppendRow();
		table.SetCell(row, 1, ibValue(ibString(code)));
	}
	ibQueryTempDataStore store;
	store.Put(wxT("Sales"), std::move(table), { wxT("Code") });

	const auto twin = std::make_shared<const ibAliasQueryable>(store.Sources().at(wxT("Sales")), wxT("S2"));
	const std::shared_ptr<const ibBackendQueryColumn> code = twin->GetQueryColumns().at(0);
	ASSERT_NE(1, code->GetColumnId()) << "a twin's column has an id of its own";

	const ibQueryRamTable rows = twin->ComputeRows({});
	ASSERT_EQ(1u, rows.Columns().size());
	EXPECT_EQ(code->GetColumnId(), rows.Columns()[0].m_id);
	ASSERT_EQ(3, rows.RowCount());
	EXPECT_EQ(wxT("B"), rows.GetCell(1, code->GetColumnId()).GetString());

	// …and a condition on the twin's column reaches the origin as its own: the index answers it.
	ibQueryCondition equality;
	equality.m_col   = code;
	equality.m_op    = ibQueryFilterOp::Equal;
	equality.m_value = ibValue(ibString(wxT("A")));
	EXPECT_EQ(2, twin->ComputeRows({ equality }).RowCount());
}
