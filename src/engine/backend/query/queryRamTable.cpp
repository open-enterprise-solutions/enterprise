////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : RAM table helpers — the balance roll-forward shared by registers
////////////////////////////////////////////////////////////////////////////

#include "queryRamTable.h"

void FoldBalancesForward(ibQueryRamTable& table,
                         const std::vector<ibMetaID>& keyColumns,
                         ibMetaID periodColumn,
                         const std::vector<ibBalanceFoldSlot>& slots,
                         const std::map<wxString, std::map<ibMetaID, ibNumber>>& opening)
{
	(void)periodColumn;   // rows arrive ordered by period within a key; the fold only needs that order

	// The running balance per key, carried across the rows of that key. Seeded from what each key
	// held ENTERING the interval — without that seed every key would appear to start at zero, which
	// is the classic way a stock report shows correct turnovers and nonsense balances.
	std::map<wxString, std::map<ibMetaID, ibNumber>> running = opening;

	auto keyOf = [&](long row) {
		wxString k;
		for (const ibMetaID id : keyColumns)
			k += table.GetCell(row, id).GetHashKey() + wxT("\x1f");   // identity, not presentation
		return k;
	};

	for (long r = 0; r < table.RowCount(); ++r) {
		const wxString key = keyOf(r);
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
