#ifndef __QUERY_ROW_CURSOR_H__
#define __QUERY_ROW_CURSOR_H__

// ibQueryRow / ibQueryRowCursor — ONE PASS OVER THE DETAIL ROWS, which is all a fold ever needed.
//
// A totals fold reads every row exactly once, in the order it arrives, and asks each one for the
// values of the level keys and of the aggregate inputs. It never looks back. What it was HANDED,
// though, was an ibQueryRamTable — the whole materialised detail — and from it a std::vector<long>
// of row indices per bucket, per level: the rows twice over, held for the lifetime of the fold, to
// support a random access nobody was asking for.
//
// So the fold takes a CURSOR. The tree it builds is unchanged; what changes is that memory becomes
// a function of the number of GROUPS rather than of the number of ROWS — the composition's own
// acceptance criterion (docs/data-composer.md), and the reason a report over a million movements
// stops being a question about RAM.
//
// A materialised table is still a perfectly good cursor (ibRamTableCursor) — the composed
// multi-source result, a computed register table and every unit test hand themselves over that way,
// and nothing downstream can tell the difference. The traffic runs the other way too: a fold that
// genuinely needs every row addressable (the row-keyed parent-ref hierarchy, where a row IS a node)
// drains the cursor with ibDrainToRamTable and says so.
//
// ibQueryRow is the row alone — the same abstraction seen by a per-row expression evaluator
// (EvalColumnExprRow), which likewise only ever read the CURRENT row through a (table, index) pair.
// (docs/query-language-arc.md §22.1b)

#include "queryRamTable.h"   // ibQueryRamTable / ibQueryRamColumn + ibValue + ibMetaID

// One row, whoever holds it: a cell by column model id. `Get(col)` is the ordinary spelling —
// the column object is what every caller has in hand.
class ibQueryRow
{
public:
	virtual ~ibQueryRow() = default;
	virtual ibValue Get(ibMetaID id) const = 0;
	ibValue Get(const ibBackendQueryColumn* col) const { return col != nullptr ? Get(col->GetColumnId()) : ibValue(); }

	// ⭐⭐ …AND BY THE NAME A COLUMN WAS PUBLISHED UNDER. An aggregate's figure has no column object to
	// ask for — both roads give it a synthetic id and a NAME — so a row that carries one answers for
	// it here. Same row, second question, rather than a second kind of row.
	//
	// 🛑 The alternative was a second evaluator: one walking a source row, one walking a result, with
	// the tree copied between them so the fold could be substituted before the first was called. Two
	// walks over one grammar is two sets of semantics waiting to disagree — the very thing this file
	// spends its comments guarding against. A row that can be asked by name removes the need for the
	// second walk entirely (2026-09-06, after Max: "you multiplied roads instead of collapsing them").
	//
	// Empty by default: a row whose columns have no published names has nothing to answer, and that
	// is a complete answer rather than a gap.
	virtual ibValue Get(const wxString& /*name*/) const { return ibValue(); }
};

// A forward cursor over rows. The cursor IS the current row — one object, one position, no
// "row handle" to keep valid past the next Next(). Starts BEFORE the first row (like every other
// cursor in the house: `while (c.Next()) …`).
class ibQueryRowCursor : public ibQueryRow
{
public:
	virtual bool Next() = 0;
	// The columns the rows carry (id / name / type) — what a fold copies into the tree it builds.
	virtual const std::vector<ibQueryRamColumn>& Columns() const = 0;
};

// A materialised table's row, addressed by index — for the paths that still hold the whole table
// (an expression evaluated over a composed JOIN row, the hierarchy folds).
class ibRamTableRow : public ibQueryRow
{
public:
	ibRamTableRow(const ibQueryRamTable& table, long row) : m_table(table), m_row(row) {}
	ibValue Get(ibMetaID id) const override { return m_table.GetCell(m_row, id); }
	// …and by name: the table already carries one per column (an aggregate's alias among them).
	ibValue Get(const wxString& name) const override {
		for (const ibQueryRamColumn& c : m_table.Columns())
			if (c.m_name.IsSameAs(name, false)) return m_table.GetCell(m_row, c.m_id);
		return ibValue();
	}

private:
	const ibQueryRamTable& m_table;
	long                   m_row;
};

// A materialised table, handed over as a cursor — the degenerate case, and the reason the fold
// needs no second entry point for callers that already have their rows in hand. Non-owning: the
// table must outlive the walk.
class ibRamTableCursor : public ibQueryRowCursor
{
public:
	explicit ibRamTableCursor(const ibQueryRamTable& table) : m_table(table) {}

	bool    Next() override                                   { return ++m_row < m_table.RowCount(); }
	ibValue Get(ibMetaID id) const override                   { return m_table.GetCell(m_row, id); }
	const std::vector<ibQueryRamColumn>& Columns() const override { return m_table.Columns(); }

private:
	const ibQueryRamTable& m_table;
	long                   m_row = -1;
};

// A cursor drained into a table — for a fold that genuinely needs every row addressable (the
// row-keyed hierarchy, where a row IS a node, so "rows" and "nodes" are the same count and holding
// them is the answer rather than a cost). Anything that calls this is paying detail-row memory ON
// PURPOSE and should say so in the journal where it does.
inline ibQueryRamTable ibDrainToRamTable(ibQueryRowCursor& rows)
{
	ibQueryRamTable table;
	for (const ibQueryRamColumn& c : rows.Columns())
		table.AddColumn(c.m_id, c.m_name, c.m_type);
	while (rows.Next()) {
		const long r = table.AppendRow();
		for (const ibQueryRamColumn& c : rows.Columns())
			table.SetCell(r, c.m_id, rows.Get(c.m_id));
	}
	return table;
}

#endif // __QUERY_ROW_CURSOR_H__
