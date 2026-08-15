#ifndef __QUERY_RAM_TABLE_H__
#define __QUERY_RAM_TABLE_H__

// ibQueryRamTable — the flat raw RESULT of an L3 read (ibDataQueryBuilder::Run): a pure array of
// rows, built for FAST extraction (by row index + column model id). It is the SNAPSHOT, and that is
// ALL it is — it carries NO tree. Splitting a snapshot into folders / subtotal levels is the
// Selector's job (ibSelector over this snapshot), and the product is a SEPARATE ibSelectorTree
// (querySelectorTree.h). L3 names no runtime type but ibValue: cells are ibValue, columns are
// id/name/type. Turning the snapshot (or the folded tree) into a runtime model is the RUNTIME's
// job. (docs/query-language-arc.md §22.1, §22.1b)

#include "queryColumn.h"                 // ibBackendQueryColumn / ibTypeDescription / ibMetaID
#include "backend/compiler/value.h"      // ibValue

#include <map>
#include <unordered_map>   // balance opening — keyed by the key tuple, see ibValueSeqHash (value.h)
#include <vector>

// One column of the snapshot — keyed by its model id (the same GetColumnId the rows use).
struct ibQueryRamColumn
{
	ibMetaID          m_id;
	wxString          m_name;
	ibTypeDescription m_type;
};

class BACKEND_API ibQueryRamTable
{
public:
	using Row = std::map<ibMetaID, ibValue>;   // a row keyed by column model id

	// Move-only (symmetry with ibSelectorTree; a snapshot is moved through the composer, never copied
	// wholesale — copy is deep over the ibValue rows, so it is deleted to catch accidental copies).
	ibQueryRamTable() = default;
	ibQueryRamTable(ibQueryRamTable&&) = default;
	ibQueryRamTable& operator=(ibQueryRamTable&&) = default;
	ibQueryRamTable(const ibQueryRamTable&) = delete;
	ibQueryRamTable& operator=(const ibQueryRamTable&) = delete;

	void AddColumn(ibMetaID id, const wxString& name, const ibTypeDescription& type) { m_columns.push_back({ id, name, type }); }
	const std::vector<ibQueryRamColumn>& Columns() const { return m_columns; }

	long    AppendRow()                                       { m_rows.emplace_back(); return static_cast<long>(m_rows.size()) - 1; }
	void    SetCell(long row, ibMetaID id, const ibValue& v)  { if (row >= 0 && row < RowCount()) m_rows[static_cast<size_t>(row)][id] = v; }
	// Set by column NAME — resolves to that column's id. For derived columns read by alias
	// (register balance / turnover X_Balance / X_Turnover…). No-op if the name is unknown.
	void    SetByName(long row, const wxString& name, const ibValue& v) {
	            for (const ibQueryRamColumn& c : m_columns) if (c.m_name == name) { SetCell(row, c.m_id, v); return; } }
	ibValue GetCell(long row, ibMetaID id) const              { if (row < 0 || row >= RowCount()) return ibValue();
	                                                             const Row& r = m_rows[static_cast<size_t>(row)];
	                                                             const auto it = r.find(id); return it != r.end() ? it->second : ibValue(); }
	long    RowCount() const                                  { return static_cast<long>(m_rows.size()); }
	// A row taken back out. A fold that only knows a row is empty AFTER the last pass (an opening
	// balance rolled forward, a reversal that netted every figure to nothing) has no other way to
	// unsay it, and building a second table to copy the survivors into would say the same thing at
	// twice the cost.
	void    EraseRow(long row)                                { if (row >= 0 && row < RowCount())
	                                                                m_rows.erase(m_rows.begin() + static_cast<size_t>(row)); }

private:
	std::vector<ibQueryRamColumn> m_columns;
	std::vector<Row>              m_rows;      // flat rows — the whole snapshot
};

// One resource's five reported columns in a balance-and-turnover row, by RAM-table column id.
// The fold fills turnover / opening / closing from the receipt / expense pair.
struct ibBalanceFoldSlot
{
	ibMetaID m_receipt  = 0;   // in  — already aggregated for the period
	ibMetaID m_expense  = 0;   // out — already aggregated for the period
	ibMetaID m_turnover = 0;   // filled: receipt - expense
	ibMetaID m_opening  = 0;   // filled: the balance entering the period
	ibMetaID m_closing  = 0;   // filled: opening + turnover
};

// The balance a key carried INTO the interval: key VALUES -> (turnover slot id -> amount).
//
// The outer key is the tuple of the key columns' values, hashed and compared as values
// (ibValueSeqHash / ibValueSeqEqual, value.h). It used to be an identity STRING folded from those
// same values through GetHashKey — a text conversion per column per row, on both the side that
// builds this map and the side that looks into it, to express something the values say themselves.
using ibBalanceOpening = std::unordered_map<std::vector<ibValue>,
                                            std::map<ibMetaID, ibNumber>,
                                            ibValueSeqHash, ibValueSeqEqual>;

// Roll per-period turnovers forward into per-period BALANCES, in place.
//
// `table` holds one row per (key, period) with the receipt / expense pair already aggregated, and
// must be ordered by period within a key. `opening` gives the balance each key carried INTO the
// interval — outer key = the same key tuple the fold builds from `keyColumns`, inner key =
// the slot's m_turnover id (absent = zero). Then, per key,
// walking periods in order: opening = the previous period's closing, closing = opening + turnover.
//
// It lives here rather than on a register because it is the SAME operation for every register kind
// that reports balances over periods. An accumulation register signs its movements by record type,
// an accounting register by debit / credit side — but by the time rows reach this fold that
// difference is already spent: both arrive as a receipt / expense pair per period. Keeping the
// running step in one place is what lets the accounting register reuse the mechanism instead of
// re-deriving it, which is the whole reason it is not a private helper of one register.
//
// The step is inherently sequential (a period's opening IS the previous closing), but it walks
// PERIODS — tens of them — not movements, so its cost is independent of how deep the history goes.
BACKEND_API void FoldBalancesForward(ibQueryRamTable& table,
                                     const std::vector<ibMetaID>& keyColumns,
                                     ibMetaID periodColumn,
                                     const std::vector<ibBalanceFoldSlot>& slots,
                                     const ibBalanceOpening& opening);

#endif // __QUERY_RAM_TABLE_H__
