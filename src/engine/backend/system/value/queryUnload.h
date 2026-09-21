#pragma once

// ⭐ THE VALUE TABLE THAT `QueryResult.Unload()` HANDS BACK.
//
// A script that runs a query very often wants the ROWS, not a cursor over them: to count them
// (`Table.Count()`), to index one (`Table[0].Column`), to sort or search them, to hand them to a
// function, to draw them in a table control. A cursor (`Select()`) can do none of that without the
// script copying it out by hand - so the result of a query says `Unload()` and the rows come as the
// platform's own value table, the same one a register's `Unload()` already returns.
//
// WHAT THIS DECIDES, AND NOTHING ELSE: the table's SHAPE (one column per output column of the query,
// in the order the query names them, each carrying the type the query knows it by) and that the rows
// are copied out in the cursor's order. It does not know what a query is: it is given the columns and
// two questions - "is there another row?" and "what is column i on it?" - so the rule is written once
// and can be tested without a database, a session or a configuration.
//
// ⚠ A COLUMN WHOSE TYPE THE QUERY DOES NOT KNOW (a computed column, a CASE, a literal) is UNTYPED,
// not a string column: an untyped table column keeps whatever value arrives, while a String one would
// quietly turn every number in it into text. "Unknown" and "string" are different statements.

#include "valueTable.h"

#include <functional>
#include <vector>

// What a query says about one output column: the name a script reads it by, and what it holds.
struct ibQueryUnloadColumn
{
	wxString          m_name;
	ibTypeDescription m_type;   // no types = unknown (see the note above)
};

class ibQueryUnload
{
public:

	// Build the table. `nextRow` moves to the next row and says whether there is one; `readColumn(i)`
	// reads column `i` (its index in `columns`) of the CURRENT row.
	//
	// The column names are taken as given: a query names its output columns uniquely (that is what lets
	// a selection read them back), so this does not disambiguate - a repeated name would make two
	// columns a script cannot tell apart, and that would be the query's mistake to see.
	static ibValue BuildTable(const std::vector<ibQueryUnloadColumn>& columns,
	                          const std::function<bool()>& nextRow,
	                          const std::function<ibValue(size_t)>& readColumn)
	{
		ibValueModelTable* table = new ibValueModelTable();
		// 🛑 HELD WHILE ITS ROWS ARE MADE, as every table built for a script is: a table fresh from `new`
		// has a count of zero, a row that takes the model and lets go drives it through zero, and that
		// DELETES the table under the loop (measured 2026-09-10 on `ToTable()`). See valueQueryable.cpp,
		// M::ToTable, which carries the whole story.
		const ibValue keep(table);

		ibValueModelTable::ibValueModelColumnCollection* tableColumns = table->GetColumnCollection();
		wxASSERT(tableColumns);

		// The id each table column was given, in step with `columns`.
		std::vector<ibMetaID> ids;
		ids.reserve(columns.size());
		for (const ibQueryUnloadColumn& column : columns) {
			const auto* added = tableColumns->AddColumn(column.m_name, column.m_type, column.m_name);
			ids.push_back(added != nullptr ? static_cast<ibMetaID>(added->GetColumnID()) : ibMetaID());
		}

		std::vector<std::pair<ibMetaID, ibValue>> row;
		row.reserve(columns.size());
		while (nextRow()) {
			row.clear();
			for (size_t i = 0; i < columns.size(); ++i)
				row.emplace_back(ids[i], readColumn(i));
			table->AppendRow(row);
		}

		return keep;
	}
};
