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

#include <utility>         // std::swap — a row re-keyed (RekeyRow)
#include <map>
#include <deque>           // the rows — see m_rows
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
	// A row keyed by column model id — ONE SORTED VECTOR, the engine's own row of values (rowValues.h),
	// not a tree. A tree gave every cell a heap node of its own: a payroll sheet of 120 thousand rows made
	// and freed half a million of them, and the table's own operations stood in a sixth of the stack
	// samples of the whole report (2026-09-12, Debug). A row holds a handful of cells, found by halving.
	using Row = ibRowMetaValues;

	// Move-only (symmetry with ibSelectorTree; a snapshot is moved through the composer, never copied
	// wholesale — copy is deep over the ibValue rows, so it is deleted to catch accidental copies).
	ibQueryRamTable() = default;
	ibQueryRamTable(ibQueryRamTable&&) = default;
	ibQueryRamTable& operator=(ibQueryRamTable&&) = default;
	ibQueryRamTable(const ibQueryRamTable&) = delete;
	ibQueryRamTable& operator=(const ibQueryRamTable&) = delete;

	void AddColumn(ibMetaID id, const wxString& name, const ibTypeDescription& type) { m_columns.push_back({ id, name, type }); }
	const std::vector<ibQueryRamColumn>& Columns() const { return m_columns; }

	// Room for a cell per declared column, made once — the cells then land without growing the row.
	long    AppendRow()                                       { m_rows.emplace_back().reserve(m_columns.size());
	                                                             return static_cast<long>(m_rows.size()) - 1; }
	void    SetCell(long row, ibMetaID id, const ibValue& v)  { if (row >= 0 && row < RowCount()) m_rows[static_cast<size_t>(row)][id] = v; }
	// Set by column NAME — resolves to that column's id. For derived columns read by alias
	// (register balance / turnover X_Balance / X_Turnover…). No-op if the name is unknown.
	void    SetByName(long row, const wxString& name, const ibValue& v) {
	            for (const ibQueryRamColumn& c : m_columns) if (c.m_name == name) { SetCell(row, c.m_id, v); return; } }
	// (The cells are found with find_value — the value where it lies, and no iterator made to reach it.)
	ibValue GetCell(long row, ibMetaID id) const              { if (row < 0 || row >= RowCount()) return ibValue();
	                                                             const ibValue* v = m_rows[static_cast<size_t>(row)].find_value(id);
	                                                             return v != nullptr ? *v : ibValue(); }
	// The cell WHERE IT LIES, or null — for a reader that only looks (a scan for references), so looking
	// costs no copy of the value.
	const ibValue* FindCell(long row, ibMetaID id) const      { if (row < 0 || row >= RowCount()) return nullptr;
	                                                             return m_rows[static_cast<size_t>(row)].find_value(id); }
	// ⭐ A CELL MOVED, NOT COPIED — for a stitch that is done with the table it reads from. Copying a value
	// is not free (a reference is counted up here and down again when the source table dies), and a list
	// passed from one table into the next paid that per cell, per table (2026-09-12).
	void    SetCell(long row, ibMetaID id, ibValue&& v)       { if (row >= 0 && row < RowCount()) m_rows[static_cast<size_t>(row)][id] = std::move(v); }
	ibValue TakeCell(long row, ibMetaID id)                   { if (row < 0 || row >= RowCount()) return ibValue();
	                                                             ibValue* v = m_rows[static_cast<size_t>(row)].find_value(id);
	                                                             return v != nullptr ? std::move(*v) : ibValue(); }
	// ⭐⭐ …AND A ROW MOVED WHOLE, when the two tables key their cells alike: a reordering (the sort's
	// rebuild) or a table whose rows are simply handed on. The row keeps every cell it had — a cell under
	// an id this table does not declare rides along unread, which is what it would have been anyway.
	long    AppendRowFrom(ibQueryRamTable& src, long row)     { m_rows.push_back(std::move(src.m_rows[static_cast<size_t>(row)]));
	                                                             return static_cast<long>(m_rows.size()) - 1; }
	// …every row of `src`, which is left with none (its columns stay — they describe nothing now).
	// ⚠ INTO AN EMPTY TABLE THE ROWS ARE EXCHANGED, not moved one by one: moving a row is not free (a
	// checked build gives every moved-to container a proxy of its own), and the tables this is used for
	// are usually just born — a leaf taken whole, the first branch of a union, a sorted rebuild.
	void    AppendRowsFrom(ibQueryRamTable& src)              { if (m_rows.empty()) { m_rows.swap(src.m_rows); return; }
	                                                             for (Row& row : src.m_rows) m_rows.push_back(std::move(row));
	                                                             src.m_rows.clear(); }

	// ⭐ THE ROWS PUT IN A NEW ORDER WHERE THEY STAND — row i becomes what was row `order[i]`, and only the
	// first `keep` of them stay. Swapped along the permutation's cycles, so no row is copied or moved and
	// nothing is allocated but one spare per cycle: the sort's rebuild made a second table row by row.
	void    ReorderRows(const std::vector<long>& order, long keep)
	{
		const size_t n = m_rows.size();
		std::vector<char> placed(n, 0);
		for (size_t i = 0; i < n && i < order.size(); ++i) {
			if (placed[i] || static_cast<size_t>(order[i]) == i) {
				placed[i] = 1;
				continue;   // already where it belongs
			}
			Row spare;
			spare.swap(m_rows[i]);   // what stood at the cycle's start, until the cycle comes back to it
			size_t at = i;
			for (;;) {
				placed[at] = 1;
				const size_t from = static_cast<size_t>(order[at]);
				if (from == i) { m_rows[at].swap(spare); break; }
				m_rows[at].swap(m_rows[from]);
				at = from;
			}
		}
		if (keep >= 0 && static_cast<size_t>(keep) < n)
			m_rows.resize(static_cast<size_t>(keep));
	}

	// ⭐⭐ …AND A ROW MOVED UNDER NEW KEYS — for a stitch whose source keys its cells by columns of its own
	// (a union's later branch, a nested query publishing its inner columns under its own). The cell held
	// under `rekey[k].first` lands under `rekey[k].second`: the value itself is moved, never copied. A cell
	// under an id the list does not name rides along unread, as it does in AppendRowFrom; a named cell the
	// row does not hold reads here as empty, exactly as a copy of an empty cell would. Each source id at
	// most once — a column read twice needs a copy, not a move.
	long    AppendRowRekeyed(ibQueryRamTable& src, long row, const std::vector<std::pair<ibMetaID, ibMetaID>>& rekey)
	{
		Row& r = src.m_rows[static_cast<size_t>(row)];
		Row::container_type cells;
		RekeyRow(r, rekey, cells);
		m_rows.push_back(std::move(r));
		return static_cast<long>(m_rows.size()) - 1;
	}
	// …every row of `src`: re-keyed where it stands, then handed on whole (AppendRowsFrom). One buffer
	// serves every row: each re-keyed row takes it, and hands back the memory it had (RekeyRow).
	void    AppendRowsRekeyed(ibQueryRamTable& src, const std::vector<std::pair<ibMetaID, ibMetaID>>& rekey)
	{
		if (!rekey.empty()) {
			Row::container_type cells;
			for (Row& r : src.m_rows)
				RekeyRow(r, rekey, cells);
		}
		AppendRowsFrom(src);
	}
	long    RowCount() const                                  { return static_cast<long>(m_rows.size()); }
	// A row taken back out. A fold that only knows a row is empty AFTER the last pass (an opening
	// balance rolled forward, a reversal that netted every figure to nothing) has no other way to
	// unsay it, and building a second table to copy the survivors into would say the same thing at
	// twice the cost.
	void    EraseRow(long row)                                { if (row >= 0 && row < RowCount())
	                                                                m_rows.erase(m_rows.begin() + static_cast<size_t>(row)); }

private:
	// One row's cells re-labelled — see AppendRowRekeyed. Built anew beside the old one rather than edited
	// where it stands, so a new key that is another pair's old one can never meet the cell still waiting to
	// be moved away from it: the named cells under their new ids, then the riders — except where a named
	// pair claims a rider's id, which it does whether or not it brought a cell (a pair with nothing to
	// bring states the cell empty; a rider does not answer for it). Sorted once, and a later pair wins a
	// key two pairs name, as a later insert would have.
	// `cells` is the caller's buffer: the new row is built in it and EXCHANGED with the old one, so the
	// next row is built in the memory this one gave back — a vector per row, and std::stable_sort's own
	// buffer per row besides, made the re-keying of 120 thousand rows twice as slow as the tree's node
	// moves it replaced (2026-09-12, Debug).
	static void RekeyRow(Row& r, const std::vector<std::pair<ibMetaID, ibMetaID>>& rekey, Row::container_type& cells)
	{
		if (rekey.empty())
			return;
		const auto named = [&rekey](ibMetaID id, bool asSource) {
			for (const std::pair<ibMetaID, ibMetaID>& k : rekey)
				if ((asSource ? k.first : k.second) == id)
					return true;
			return false;
		};
		cells.clear();
		cells.reserve(r.size() + rekey.size());
		for (const std::pair<ibMetaID, ibMetaID>& k : rekey)
			if (ibValue* const v = r.find_value(k.first))
				cells.emplace_back(k.second, std::move(*v));
		for (Row::value_type& cell : r)
			if (!named(cell.first, /*asSource*/ true) && !named(cell.first, /*asSource*/ false))
				cells.emplace_back(cell.first, std::move(cell.second));
		// A handful of cells: sorted by insertion, over the array itself (stable — of two pairs naming one
		// id, the later stays later).
		Row::value_type* const c = cells.data();
		const size_t n = cells.size();
		for (size_t i = 1; i < n; ++i)
			for (size_t j = i; j > 0 && c[j].first < c[j - 1].first; --j)
				std::swap(c[j], c[j - 1]);
		size_t kept = 0;
		for (size_t i = 0; i < n; ++i) {
			if (kept > 0 && c[kept - 1].first == c[i].first)
				c[kept - 1].second = std::move(c[i].second);   // the later pair of two naming one id
			else if (kept != i)
				c[kept++] = std::move(c[i]);
			else
				++kept;
		}
		cells.resize(kept);
		r.swap_sorted(cells);
	}

	std::vector<ibQueryRamColumn> m_columns;
	// ⚠ A DEQUE, NOT A VECTOR — rows arrive one at a time and nobody knows how many there will be. A
	// vector grew by reallocating and MOVING every row it held, and moving a row map is not free (the
	// moved-from map is left with a fresh empty head): a branch of 86 thousand rows did that again at
	// every doubling, and AppendRow stood in a quarter of the stack samples of that read (MEASURED
	// 2026-09-12, Debug). A deque adds a row where it stands and never moves the others.
	std::deque<Row>               m_rows;      // flat rows — the whole snapshot
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
