// =============================================================================
// QueryResult.Unload() - the rows of a query as a value table.
//
// A script that runs a query usually wants the rows, not a cursor: to count them, index one, sort or
// search them, hand them on. `Table = Query.Execute().Unload()` gives the platform's value table.
//
// Every read that hands a script its rows as a table ends at ONE place - ibQueryRamTable::ToValueTable:
// Unload(), a queryable's ToTable(), a register's slices and figures, a LINQ answer each fill the fast
// table and have it loaded. WHAT IS TESTED HERE is that loading, over a fast table filled by hand, so no
// database, session or configuration is needed:
//
//   1. one table column per column of the fast table, in its order, named as it names them;
//   2. a column's caption is the one it carries, and its name where it carries none;
//   3. every row, in the fast table's order, each cell the value that was put there;
//   4. no rows still gives the table its columns (an empty answer is not an answer without a shape);
//   5. an UNTYPED column keeps what arrives - a number stays a number - where a String column
//      would turn it into text: "the query does not know the type" is not "it is a string";
//   6. the table outlives the fast table it was loaded from.
//
// And the surface on the script side: QueryResult answers `Unload`, and asking a result that was
// already read says so instead of answering an empty table that looks like "found nothing".
// =============================================================================

#include <gtest/gtest.h>

#include "backend/backend_exception.h"
#include "backend/query/queryRamTable.h"
#include "backend/system/value/valueTable.h"
#include "backend/system/value/valueQuery.h"

#include <vector>

namespace {

// A fast table of untyped columns, filled row by row the way a read fills it.
ibQueryRamTable Rows(const std::vector<wxString>& names, const std::vector<std::vector<ibValue>>& rows)
{
	ibQueryRamTable table;
	for (size_t i = 0; i < names.size(); ++i)
		table.AddColumn(static_cast<ibMetaID>(i + 1), names[i], ibTypeDescription());
	for (const std::vector<ibValue>& cells : rows) {
		const long at = table.AppendRow();
		for (size_t i = 0; i < cells.size(); ++i)
			table.SetCell(at, static_cast<ibMetaID>(i + 1), cells[i]);
	}
	return table;
}

ibValueModelTable* TableOf(const ibValue& value)
{
	return value.ConvertToType<ibValueModelTable>();
}

// The rows as a script reads them: by column name, in order.
std::vector<std::vector<ibValue>> ReadRows(ibValueModelTable* table, const std::vector<wxString>& names)
{
	std::vector<std::vector<ibValue>> out;
	const std::shared_ptr<ibValueIteratorState> walk = table->CreateIterator();
	if (walk == nullptr)
		return out;
	ibValue row;
	while (walk->MoveNext(row)) {
		std::vector<ibValue> cells;
		for (const wxString& name : names) {
			ibValue cell;
			const long prop = row.FindProp(name);
			if (prop >= 0)
				row.GetPropVal(prop, cell);
			cells.push_back(cell);
		}
		out.push_back(cells);
	}
	return out;
}

ibValue Str(const wxString& text) { return ibValue(text); }

} // namespace

// 1 - the table's columns are the fast table's, in its order and under its names.
TEST(QueryUnload, Columns_FollowTheFastTable_InOrderAndByName)
{
	const ibValue result = Rows({ wxT("Code"), wxT("Name"), wxT("Price") }, {}).ToValueTable();

	ibValueModelTable* table = TableOf(result);
	ASSERT_NE(table, nullptr);
	ibValueModelTable::ibValueModelColumnCollection* columns = table->GetColumnCollection();
	ASSERT_EQ(columns->GetColumnCount(), 3u);
	EXPECT_EQ(columns->GetColumnInfo(0)->GetColumnName(), wxT("Code"));
	EXPECT_EQ(columns->GetColumnInfo(1)->GetColumnName(), wxT("Name"));
	EXPECT_EQ(columns->GetColumnInfo(2)->GetColumnName(), wxT("Price"));
}

// 2 - a register's figure is shown by the caption its column carries (`<resource> Balance`), not by its name.
TEST(QueryUnload, Caption_IsTheColumns_OrItsName)
{
	ibQueryRamTable rows;
	rows.AddColumn(1, wxT("AmountBalance"), ibTypeDescription(), wxT("Amount Balance"));
	rows.AddColumn(2, wxT("Code"), ibTypeDescription());

	ibValueModelTable* table = TableOf(rows.ToValueTable());
	ASSERT_NE(table, nullptr);
	ibValueModelTable::ibValueModelColumnCollection* columns = table->GetColumnCollection();
	ASSERT_EQ(columns->GetColumnCount(), 2u);
	EXPECT_EQ(columns->GetColumnInfo(0)->GetColumnCaption(), wxT("Amount Balance"));
	EXPECT_EQ(columns->GetColumnInfo(1)->GetColumnCaption(), wxT("Code"));
}

// 3 - every row, in the fast table's order, each cell the value that was put there.
TEST(QueryUnload, Rows_AreLoadedInOrder)
{
	const ibValue result = Rows({ wxT("Code"), wxT("Name") }, {
		{ Str(wxT("A-1")), Str(wxT("Bolt")) },
		{ Str(wxT("A-2")), Str(wxT("Nut")) },
		{ Str(wxT("A-3")), Str(wxT("Washer")) },
	}).ToValueTable();

	ibValueModelTable* table = TableOf(result);
	ASSERT_NE(table, nullptr);
	EXPECT_EQ(table->Count(), 3u);

	const auto read = ReadRows(table, { wxT("Code"), wxT("Name") });
	ASSERT_EQ(read.size(), 3u);
	EXPECT_EQ(read[0][0].GetString(), wxT("A-1"));
	EXPECT_EQ(read[0][1].GetString(), wxT("Bolt"));
	EXPECT_EQ(read[1][0].GetString(), wxT("A-2"));
	EXPECT_EQ(read[2][1].GetString(), wxT("Washer"));
}

// 4 - a query that found nothing still answers with its shape.
TEST(QueryUnload, NoRows_StillHasTheColumns)
{
	ibValueModelTable* table = TableOf(Rows({ wxT("Code"), wxT("Name") }, {}).ToValueTable());
	ASSERT_NE(table, nullptr);
	EXPECT_EQ(table->Count(), 0u);
	EXPECT_EQ(table->GetColumnCollection()->GetColumnCount(), 2u);
}

// 5 - an untyped column keeps a number a number. (A DECLARED column type converts what it is given - that is
// what a type is for - which is exactly why "the query does not know the type" must not be given one; creating a
// typed column needs the configuration's metadata, which a headless test does not have.)
TEST(QueryUnload, UntypedColumn_KeepsTheValueItReceives)
{
	const ibValue result = Rows({ wxT("Sum") }, { { ibValue(12.5) }, { Str(wxT("text")) } }).ToValueTable();

	const auto read = ReadRows(TableOf(result), { wxT("Sum") });
	ASSERT_EQ(read.size(), 2u);
	EXPECT_EQ(read[0][0].GetType(), ibValueTypes::TYPE_NUMBER)
		<< "an untyped column must not turn a number into text";
	EXPECT_DOUBLE_EQ(read[0][0].GetDouble(), 12.5);
	EXPECT_EQ(read[1][0].GetType(), ibValueTypes::TYPE_STRING) << "and it holds a value of another kind as it is";
	EXPECT_EQ(read[1][0].GetString(), wxT("text"));
}

// 6 - the returned table is alive and complete after the fast table it came from is gone.
TEST(QueryUnload, TheTable_OutlivesTheFastTable)
{
	ibValue kept;
	{
		std::vector<std::vector<ibValue>> cells;
		for (int i = 0; i < 200; ++i)
			cells.push_back({ ibValue(static_cast<double>(i)) });
		kept = Rows({ wxT("N") }, cells).ToValueTable();
	}   // the fast table is gone; only the value table remains

	ibValueModelTable* table = TableOf(kept);
	ASSERT_NE(table, nullptr);
	EXPECT_EQ(table->Count(), 200u);
	const auto read = ReadRows(table, { wxT("N") });
	ASSERT_EQ(read.size(), 200u);
	EXPECT_DOUBLE_EQ(read[0][0].GetDouble(), 0.0);
	EXPECT_DOUBLE_EQ(read[199][0].GetDouble(), 199.0);
}

// The script side: a query result answers `Unload`, beside `Select`.
TEST(QueryUnload, QueryResult_Exposes_Unload)
{
	ibValueQueryResult res;   // empty / AST-less
	EXPECT_GE(res.FindMethod(wxT("Unload")), 0);
	EXPECT_GE(res.FindMethod(wxT("Select")), 0);
	EXPECT_NE(res.FindMethod(wxT("Unload")), res.FindMethod(wxT("Select")));
}

// A result whose cursor is already spent does not answer an empty table: that would read as "the query
// found nothing". It says it was read.
TEST(QueryUnload, ARead_Result_RefusesASecondUnload_Aloud)
{
	ibValueQueryResult res;   // an empty result has no cursor - the state of one already consumed
	const long unload = res.FindMethod(wxT("Unload"));
	ASSERT_GE(unload, 0);

	ibValue out;
	try {
		res.CallAsFunc(unload, out, nullptr, 0);
		FAIL() << "unloading a result with no cursor left must raise, not return an empty table";
	}
	catch (const ibBackendException& err) {
		EXPECT_NE(err.GetErrorDescription().Find(wxT("already been read")), wxNOT_FOUND) << err.GetErrorDescription().ToStdString();
	}
}
