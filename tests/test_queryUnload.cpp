// =============================================================================
// QueryResult.Unload() - the rows of a query as a value table.
//
// A script that runs a query usually wants the rows, not a cursor: to count them, index one, sort or
// search them, hand them on. `Table = Query.Execute().Unload()` gives the platform's value table.
// (Card MIG-61 of the migration board: the 1C code all over the configuration does
// `Запрос.Выполнить().Выгрузить()` and then works with the table.)
//
// WHAT IS TESTED HERE is the rule that shapes the table - ibQueryUnload::BuildTable - with a fake row
// source, so no database, session or configuration is needed:
//
//   1. one table column per query column, in the query's order, named as the query names them;
//   2. every row, in the cursor's order, each cell the value the query read for it;
//   3. no rows still gives the table its columns (an empty answer is not an answer without a shape);
//   4. an UNTYPED column keeps what arrives - a number stays a number - where a String column
//      would turn it into text: "the query does not know the type" is not "it is a string";
//   5. the table outlives the loop that made it (it is held while its rows are made, and handed on
//      held: a row that takes the model and lets go must not delete it).
//
// And the surface on the script side: QueryResult answers `Unload`, and asking a result that was
// already read says so instead of answering an empty table that looks like "found nothing".
// =============================================================================

#include <gtest/gtest.h>

#include "backend/backend_exception.h"
#include "backend/system/value/queryUnload.h"
#include "backend/system/value/valueQuery.h"

#include <vector>

namespace {

// A row source that stands in for a cursor: Next() moves on, Read(i) is column i of the current row.
struct FakeRows {
	std::vector<std::vector<ibValue>> m_rows;
	size_t m_next = 0;
	int    m_reads = 0;

	bool    Next() { return m_next++ < m_rows.size(); }
	ibValue Read(size_t column) { ++m_reads; return m_rows[m_next - 1][column]; }
};

ibValue Unload(const std::vector<ibQueryUnloadColumn>& columns, FakeRows& rows)
{
	return ibQueryUnload::BuildTable(columns,
		[&rows]() { return rows.Next(); },
		[&rows](size_t column) { return rows.Read(column); });
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

ibQueryUnloadColumn Untyped(const wxString& name) { return { name, ibTypeDescription() }; }

} // namespace

// 1 - the table's columns are the query's, in the query's order and under the query's names.
TEST(QueryUnload, Columns_FollowTheQuery_InOrderAndByName)
{
	FakeRows rows;
	const ibValue result = Unload({ Untyped(wxT("Code")), Untyped(wxT("Name")), Untyped(wxT("Price")) }, rows);

	ibValueModelTable* table = TableOf(result);
	ASSERT_NE(table, nullptr);
	ibValueModelTable::ibValueModelColumnCollection* columns = table->GetColumnCollection();
	ASSERT_EQ(columns->GetColumnCount(), 3u);
	EXPECT_EQ(columns->GetColumnInfo(0)->GetColumnName(), wxT("Code"));
	EXPECT_EQ(columns->GetColumnInfo(1)->GetColumnName(), wxT("Name"));
	EXPECT_EQ(columns->GetColumnInfo(2)->GetColumnName(), wxT("Price"));
}

// 2 - every row, in the cursor's order, each cell the value that was read for it.
TEST(QueryUnload, Rows_AreCopiedInCursorOrder)
{
	FakeRows rows;
	rows.m_rows = {
		{ Str(wxT("A-1")), Str(wxT("Bolt")) },
		{ Str(wxT("A-2")), Str(wxT("Nut")) },
		{ Str(wxT("A-3")), Str(wxT("Washer")) },
	};
	const ibValue result = Unload({ Untyped(wxT("Code")), Untyped(wxT("Name")) }, rows);

	ibValueModelTable* table = TableOf(result);
	ASSERT_NE(table, nullptr);
	EXPECT_EQ(table->Count(), 3u);

	const auto read = ReadRows(table, { wxT("Code"), wxT("Name") });
	ASSERT_EQ(read.size(), 3u);
	EXPECT_EQ(read[0][0].GetString(), wxT("A-1"));
	EXPECT_EQ(read[0][1].GetString(), wxT("Bolt"));
	EXPECT_EQ(read[1][0].GetString(), wxT("A-2"));
	EXPECT_EQ(read[2][1].GetString(), wxT("Washer"));
	EXPECT_EQ(rows.m_reads, 6) << "each cell is read once, no more";
}

// 3 - a query that found nothing still answers with its shape.
TEST(QueryUnload, NoRows_StillHasTheColumns)
{
	FakeRows rows;
	const ibValue result = Unload({ Untyped(wxT("Code")), Untyped(wxT("Name")) }, rows);

	ibValueModelTable* table = TableOf(result);
	ASSERT_NE(table, nullptr);
	EXPECT_EQ(table->Count(), 0u);
	EXPECT_EQ(table->GetColumnCollection()->GetColumnCount(), 2u);
	EXPECT_EQ(rows.m_reads, 0);
}

// 4 - an untyped column keeps a number a number. (A DECLARED column type converts what it is given - that is
// what a type is for - which is exactly why "the query does not know the type" must not be given one; creating a
// typed column needs the configuration's metadata, which a headless test does not have.)
TEST(QueryUnload, UntypedColumn_KeepsTheValueItReceives)
{
	FakeRows rows;
	rows.m_rows = { { ibValue(12.5) }, { Str(wxT("text")) } };
	const ibValue result = Unload({ Untyped(wxT("Sum")) }, rows);

	const auto read = ReadRows(TableOf(result), { wxT("Sum") });
	ASSERT_EQ(read.size(), 2u);
	EXPECT_EQ(read[0][0].GetType(), ibValueTypes::TYPE_NUMBER)
		<< "an untyped column must not turn a number into text";
	EXPECT_DOUBLE_EQ(read[0][0].GetDouble(), 12.5);
	EXPECT_EQ(read[1][0].GetType(), ibValueTypes::TYPE_STRING) << "and it holds a value of another kind as it is";
	EXPECT_EQ(read[1][0].GetString(), wxT("text"));
}

// 5 - the returned table is alive and complete after the loop that made it is gone.
TEST(QueryUnload, TheTable_OutlivesTheLoopThatMadeIt)
{
	ibValue kept;
	{
		FakeRows rows;
		for (int i = 0; i < 200; ++i)
			rows.m_rows.push_back({ ibValue(static_cast<double>(i)) });
		kept = Unload({ Untyped(wxT("N")) }, rows);
	}   // the fake rows are gone; only the table's own copy remains

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
