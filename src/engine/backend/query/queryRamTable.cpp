////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : RAM table helpers — the balance roll-forward shared by registers
////////////////////////////////////////////////////////////////////////////

#include "queryRamTable.h"

#include "backend/system/value/valueTable.h"   // ibValueModelTable — ToValueTable
#include "backend/system/value/valueType.h"    // ibValueTypeDescription::AdjustValue

ibValue ibQueryRamTable::ToValueTable() const
{
	ibValueModelTable* const table = new ibValueModelTable();
	// 🛑 Held while its rows are made: a table fresh from `new` has a count of zero, a row that takes the
	// model and lets go drives it through zero, and that deletes the table under the loop (valueQueryable.cpp,
	// M::ToTable, measured 2026-09-10).
	const ibValue keep(table);

	ibValueModelTable::ibValueModelColumnCollection* const columns = table->GetColumnCollection();
	std::vector<ibMetaID> ids;   // each table column's id, in step with m_columns
	ids.reserve(m_columns.size());
	for (const ibQueryRamColumn& column : m_columns) {
		const auto* const added = columns->AddColumn(column.m_name, column.m_type,
			column.m_caption.IsEmpty() ? column.m_name : column.m_caption);
		ids.push_back(added != nullptr ? static_cast<ibMetaID>(added->GetColumnID()) : ibMetaID());
	}

	// 🛑 THE ROWS GO IN WITHOUT A NOTIFY. AppendRow is the door a PERSON adds a row through: it notifies the
	// model, and the notify makes the view's order stale, which is recomputed over every row there is — once
	// per row, O(n²): 50 000 rows took seventy seconds (measured on the LINQ answer). A table being built has
	// nobody watching it. A row starts as the model's own empty row (NewRow — every column its type's empty
	// value), and a cell this table holds is adjusted to its column's type on the way in.
	for (const Row& from : m_rows) {
		ibComposerNode* const row = table->NewRow();
		for (size_t i = 0; i < ids.size(); ++i)
			if (const ibValue* const cell = from.find_value(m_columns[i].m_id))
				row->AppendTableValue(ids[i], ibValueTypeDescription::AdjustValue(m_columns[i].m_type, *cell));
		table->Append(row, /*notify*/ false);
	}

	return keep;
}

void FoldBalancesForward(ibQueryRamTable& table,
                         const std::vector<ibMetaID>& keyColumns,
                         ibMetaID periodColumn,
                         const std::vector<ibBalanceFoldSlot>& slots,
                         const ibBalanceOpening& opening)
{
	(void)periodColumn;   // rows arrive ordered by period within a key; the fold only needs that order

	// The running balance per key, carried across the rows of that key. Seeded from what each key
	// held ENTERING the interval — without that seed every key would appear to start at zero, which
	// is the classic way a stock report shows correct turnovers and nonsense balances.
	ibBalanceOpening running = opening;

	// The key is the TUPLE of the key columns' values — identity, not presentation,
	// and no longer rendered to text to say so (see ibValueSeqHash, value.h).
	auto keyOf = [&](long row) {
		std::vector<ibValue> k;
		k.reserve(keyColumns.size());
		for (const ibMetaID id : keyColumns)
			k.push_back(table.GetCell(row, id));
		return k;
	};

	for (long r = 0; r < table.RowCount(); ++r) {
		const std::vector<ibValue> key = keyOf(r);
		std::map<ibMetaID, ibNumber>& carry = running[key];

		for (const ibBalanceFoldSlot& s : slots) {
			const ibNumber receipt = table.GetCell(r, s.m_receipt).GetNumber();
			const ibNumber expense = table.GetCell(r, s.m_expense).GetNumber();
			const ibNumber turnover = receipt - expense;

			// Opening is whatever the previous period of THIS key closed at (zero on first sight).
			const ibNumber open  = carry[s.m_turnover];
			const ibNumber close = open + turnover;

			table.SetCell(r, s.m_turnover, ibValue(turnover));
			table.SetCell(r, s.m_opening,  ibValue(open));
			table.SetCell(r, s.m_closing,  ibValue(close));

			carry[s.m_turnover] = close;   // keyed by the slot, so several resources fold independently
		}
	}
}
