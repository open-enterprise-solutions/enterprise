// The query constructor's MODEL half (backend/query/queryConstructorModel.{h,cpp}).
//
// Pure: no database, no session, no window. What is pinned here is the part of the constructor
// that can be wrong SILENTLY — which sources a statement may select from, and where the fields of
// a source come from. The tabs above it are a view; these are the answers they draw.
//
// The two properties worth a test each, both stated in docs/query-constructor.md §5b:
//
//   * a temp table becomes a source for the statements AFTER the one that made it — and for no
//     others, because a package is read top to bottom;
//   * the fields of a source are asked OF THE SOURCE: a nested table and a temp table answer with
//     their own projections, and nothing branches on a type to find that out.

#include <gtest/gtest.h>

#include "backend/query/queryConstructorModel.h"
#include "backend/query/queryParser.h"

namespace {

ibQueryPackage Package(const wxString& text)
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

std::vector<wxString> Names(const std::vector<ibQueryConstructorSource>& sources)
{
	std::vector<wxString> out;
	for (const ibQueryConstructorSource& source : sources)
		out.push_back(source.Text());
	return out;
}

std::vector<wxString> Names(const std::vector<ibQueryConstructorField>& fields)
{
	std::vector<wxString> out;
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
	const std::vector<wxString> names = Names(ibQueryConstructorModel::GetTempSources(SalesPackage(), 1));
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
	const std::vector<wxString> fields = Names(model.GetFields(outer->m_from, ibQueryPackage(), 0));

	ASSERT_EQ(2u, fields.size());
	EXPECT_EQ(wxT("Code"), fields[0]);
	EXPECT_EQ(wxT("Name"), fields[1]);
}

TEST(QueryConstructorModel, ANestedTableUsesTheAliasWhereTheAuthorGaveOne)
{
	ibQueryParser parser;
	const ibQuerySelectPtr outer = parser.Parse(
		wxT("SELECT x FROM (SELECT Price * 2 AS x FROM Catalog.Products) AS sub"));
	ASSERT_NE(nullptr, outer);

	ibQueryConstructorModel model(nullptr);
	const std::vector<wxString> fields = Names(model.GetFields(outer->m_from, ibQueryPackage(), 0));
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
	const std::vector<wxString> fields =
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
	const std::vector<wxString> fields = Names(model.GetQualifiedFields(outer->m_from, ibQueryPackage(), 0));
	ASSERT_EQ(1u, fields.size());
	EXPECT_EQ(wxT("p.Code"), fields[0]);
}

TEST(QueryConstructorModel, WithNoConfigTheCatalogueIsEmptyRatherThanAFailure)
{
	// No application data, no config: the constructor still opens, and its table list is simply
	// empty. A catalogue that threw here would make the window unopenable in exactly the situation
	// where somebody is trying to look at a query.
	ibQueryConstructorModel model(nullptr);
	EXPECT_NO_THROW((void)model.GetSources());
}

// ===========================================================================
//  The temp-table store — WHO keeps a temp table alive.
//
//  The question the store answers is not about queries but about ownership: without a holder a
//  temp table lives for one execution, with one it lives until the holder lets go. That is the
//  whole difference between a batch and a `TempTablesManager`, and it is testable without a
//  session, a database or a window.
// ===========================================================================

#include "backend/query/queryTempStore.h"
#include "backend/query/queryable.h"      // ibBackendQueryable — a stored table IS one

namespace {

ibQueryRamTable OneRow(const wxString& column, const ibValue& cell)
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
	ibQueryTempTableStore store;
	EXPECT_TRUE(store.Empty());
	EXPECT_FALSE(store.Has(wxT("Sales")));
	EXPECT_TRUE(store.Sources().empty());
}

TEST(QueryTempStore, WhatIsPutInCanBeFoundByName)
{
	ibQueryTempTableStore store;
	store.Put(wxT("Sales"), OneRow(wxT("Ref"), ibValue(1)));

	EXPECT_TRUE(store.Has(wxT("Sales")));
	ASSERT_EQ(1u, store.Sources().size());
	EXPECT_NE(nullptr, store.Sources().at(wxT("Sales")));
}

TEST(QueryTempStore, ADroppedTableIsGoneFromBothTheNameAndTheRows)
{
	// A name removed while its rows stayed alive would be memory nobody can reach and nobody can
	// free — the reason Drop touches both.
	ibQueryTempTableStore store;
	store.Put(wxT("Sales"), OneRow(wxT("Ref"), ibValue(1)));

	EXPECT_TRUE(store.Drop(wxT("Sales")));
	EXPECT_FALSE(store.Has(wxT("Sales")));
	EXPECT_TRUE(store.Empty());
}

TEST(QueryTempStore, DroppingWhatWasNeverThereSaysSo)
{
	// False, not a shrug: the caller turns it into "that table does not exist", which is a typo
	// or a statement that was expected to run and did not.
	ibQueryTempTableStore store;
	EXPECT_FALSE(store.Drop(wxT("Sales")));
}

TEST(QueryTempStore, CloseReleasesEverythingAtOnce)
{
	// This is `TempTablesManager.Close()` — the one decision there is to make about a set of
	// temp tables.
	ibQueryTempTableStore store;
	store.Put(wxT("Sales"),  OneRow(wxT("Ref"), ibValue(1)));
	store.Put(wxT("Orders"), OneRow(wxT("Ref"), ibValue(2)));
	ASSERT_EQ(2u, store.Sources().size());

	store.Close();
	EXPECT_TRUE(store.Empty());
	EXPECT_FALSE(store.Has(wxT("Sales")));
	EXPECT_FALSE(store.Has(wxT("Orders")));
}

TEST(QueryTempStore, AStoredTableKeepsItsColumnsAndRows)
{
	// The table is handed out BY COPY per read, because a temp table is read more than once by
	// design — that is the point of putting it somewhere.
	ibQueryTempTableStore store;
	store.Put(wxT("Sales"), OneRow(wxT("Ref"), ibValue(wxString(wxT("A")))));

	const ibBackendQueryable* source = store.Sources().at(wxT("Sales"));
	ASSERT_NE(nullptr, source);

	const std::vector<const ibBackendQueryColumn*> columns = source->GetColumns();
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
		table.SetCell(row, 1, ibValue(wxString(code)));
	}

	ibQueryTempTableStore store;
	store.Put(wxT("Sales"), std::move(table), { wxT("Code") });

	const ibBackendQueryable* source = store.Sources().at(wxT("Sales"));
	ASSERT_NE(nullptr, source);
	const std::vector<const ibBackendQueryColumn*> columns = source->GetColumns();
	ASSERT_EQ(1u, columns.size());

	ibQueryCondition equality;
	equality.m_col   = columns[0];
	equality.m_op    = ibQueryFilterOp::Equal;
	equality.m_value = ibValue(wxString(wxT("A")));

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
	table.SetCell(row, 1, ibValue(wxString(wxT("A"))));

	ibQueryTempTableStore store;
	store.Put(wxT("Sales"), std::move(table), { wxT("Code") });

	const ibBackendQueryable* source = store.Sources().at(wxT("Sales"));
	ibQueryCondition equality;
	equality.m_col   = source->GetColumns()[0];
	equality.m_op    = ibQueryFilterOp::Equal;
	equality.m_value = ibValue(wxString(wxT("Z")));

	EXPECT_EQ(0, source->ComputeRows({ equality }).RowCount());
}

TEST(QueryTempStore, WithoutAnIndexEveryRowIsStillReturned)
{
	// No index, no change: the read walks the table exactly as it did before the clause existed.
	ibQueryRamTable table;
	table.AddColumn(1, wxT("Code"), ibTypeDescription());
	for (const wxChar* code : { wxT("A"), wxT("B") }) {
		const long row = table.AppendRow();
		table.SetCell(row, 1, ibValue(wxString(code)));
	}

	ibQueryTempTableStore store;
	store.Put(wxT("Sales"), std::move(table));   // no indexed columns

	const ibBackendQueryable* source = store.Sources().at(wxT("Sales"));
	ibQueryCondition equality;
	equality.m_col   = source->GetColumns()[0];
	equality.m_op    = ibQueryFilterOp::Equal;
	equality.m_value = ibValue(wxString(wxT("A")));

	EXPECT_EQ(2, source->ComputeRows({ equality }).RowCount())
		<< "the filter is applied downstream — the store hands back what it has";
}
