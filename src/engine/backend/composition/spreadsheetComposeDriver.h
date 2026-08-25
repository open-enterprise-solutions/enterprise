#ifndef __SPREADSHEET_COMPOSE_DRIVER_H__
#define __SPREADSHEET_COMPOSE_DRIVER_H__

// ---------------------------------------------------------------------------
// L5-1 — the REPORT driver: one composition walk written into a spreadsheet
// document. The sibling of ibListFetchDriver (which accumulates rows for a
// list); this one PRINTS them.
//
// ⭐⭐ IT WRITES AREAS AND STATES A LEVEL — nothing else.
//
// Every row is built as a one-row document and handed to PutArea(row, level).
// The level is the only positional thing the driver says, and it is the one
// thing it actually knows: how deep the grouping is. WHERE the row lands, how
// far the group it opens extends, which line the fold marker belongs on — all
// of that follows from the order the rows were put and is the document's own
// business (Max, 2026-08-19: "when you push the row you give it the level, that
// is all that is required of you; the number you work out from the totals — the
// grouping levels — plus folders").
//
// A driver that also tracked its own row cursor was saying the same thing twice,
// and the second telling drifted: an all-empty row writes no cell, the cursor
// and the document disagreed by one, and every fold marker sat a line off.
//
// WHAT THE REPORT LOOKS LIKE (see docs/report-engine.md §4d):
//   * DIMENSIONS stack into ONE column, indented per level;
//   * MEASURES take a column each, numbers right-aligned;
//   * a grouping row is tinted per level and bold;
//   * A CELL THAT STANDS FOR SOMETHING CARRIES IT. A reference / object / enum
//     value is stored as a parameter of the row area and the cell is bound to
//     it — PutArea re-keys it into the target document, so drill-down survives
//     the move. Plain numbers and strings are not bound: nothing to open.
// ---------------------------------------------------------------------------

#include "backend/composition/dataComposer.h"
#include "backend/backend_spreadsheet.h"

#include <map>      // a cross row's cells are sparse — column key index -> figures
#include <vector>

class BACKEND_API ibSpreadsheetComposeDriver : public ibCompositionDriver
{
public:
	explicit ibSpreadsheetComposeDriver(ibBackendSpreadsheetObject* document)
		: m_document(document) {}

	// THE HEADING — what this report is, and under what it was built. A report read a week later
	// without its parameters is a page of numbers nobody can defend: "sales by partner" means
	// nothing until it says WHICH PERIOD and WHICH FILTER produced it. Set before the walk; an
	// empty title and no lines simply produce no heading at all.
	void SetTitle(const wxString& title) { m_title = title; }
	void AddHeaderLine(const wxString& line) { m_headerLines.push_back(line); }

	// A REPORT ENDS WITH ITS TOTAL. Whatever the composition's text asks for, a printed report shows
	// what everything above it adds up to — so the walk is asked for the tree's root, and the row it
	// hands over is held and written last (Max, 2026-08-22: "the grand total is needed, at the
	// bottom").
	virtual bool WantsGrandTotal() const override { return true; }

	// …AND A TABLE IS CLOSED BY THE COLUMN TOTALS, for the same reason it is closed by the grand one:
	// a printed report shows what its columns add up to. The composition pays for the second fold
	// only when the output actually is a table — a plain grouping never asks.
	virtual bool WantsColumnTotals() const override { return true; }

	// ⭐ WHAT SHAPE IS COMING, asked before the first row. A cross-table is not a different KIND of
	// drawing — it is the same sheet, the same cells, the same bindings, laid out the other way — so
	// it is this driver in its other layout rather than a driver of its own. (A CHART would be the
	// other thing: a picture, and that is a driver question. See the note in valueDataComposition.)
	virtual void OnOutputBegin(const ibCompositionOutputInfo& info) override;

	virtual void OnColumns(const std::vector<ibQueryLowering::OutputColumn>& schema) override;
	virtual void OnRow(int level, bool hasChildren, const std::vector<ibValue>& values) override;
	virtual void OnComplete(bool totals) override;

	// ⭐⭐ A HEADING AND A DETAIL RECORD ARE DIFFERENT THINGS IN A TABLE, and the walk already says
	// which is which — so these are overridden rather than guessed at from `hasChildren` inside
	// OnRow. (Audit, docs/composer-arc-queue.md: no driver overrode any node verb and the base read
	// only the schema. A cross-table is the first reader the vocabulary has: a cell stands where two
	// headings meet, and a detail record stands under no column key at all.)
	//
	// Both fall straight through to the streaming path when the output has no column axis — the
	// ordinary report is unchanged, down to the order the cells are written in.
	virtual void OnGroup(int level, bool hasChildren, bool showsWhatIsUnder,
		const std::vector<ibValue>& values) override;
	virtual void OnDetail(int level, const std::vector<ibValue>& values) override;

	// How many rows the walk printed under the heading — 0 means the composition produced nothing,
	// which is a legitimate answer and NOT an error (the caller decides what to say).
	int GetRowsWritten() const { return m_rowsWritten; }

private:
	// ⭐⭐ A COLUMN'S WIDTH BELONGS TO THE SHEET, NOT TO THE OUTPUT. Outputs share one sheet, so
	// column 3 is the SAME column for all of them and has to fit whatever the widest of them puts
	// there. Reset per output, the second report sized the columns to its own text and the first
	// one's values were clipped in place — nothing said so, they simply stopped being readable
	// (Max, 2026-08-25: "with several reports you have to fit the widths so everything goes in").
	//
	// Cleared only when a NEW composition starts (the first section), grown otherwise: a later
	// output may be wider, and a shorter one must not shrink what an earlier one needed.
	void WidenTo(int columns, bool firstSection);
	// The heading (title + parameter lines) as an area of its own.
	void WriteHeading();
	// …and the output's own name over its block, under that heading. Both layouts call it, so a
	// table and a grouping are captioned by one rule.
	void WriteOutputCaption();

	// The widest text written in each column, in characters — the report sizes its own columns
	// (a composed report has no author to drag a border, and a value clipped to the default width
	// reads as a different value).
	std::vector<size_t>         m_widest;

	// WHERE EACH SCHEMA ENTRY LANDS — the sheet column per schema index (-1 = not written). See
	// OnColumns for why the layout is by ROLE rather than by schema order.
	std::vector<int>            m_layout;
	// WHICH LEVEL each schema entry belongs to (-1 = not a dimension). A level may hold several
	// fields, so this is what tells "the second field of level 1" from "the first field of level 2";
	// counting dimension columns instead is how the last level used to disappear.
	std::vector<int>            m_dimLevel;

	ibBackendSpreadsheetObject* m_document = nullptr;
	wxString                    m_title;
	// What THIS output is called — its caption over its own block. Empty for an output nobody named,
	// and then nothing is printed: a blank caption line would read as a row that failed.
	wxString                    m_outputName;
	std::vector<wxString>       m_headerLines;
	int                         m_columnCount = 0;
	int                         m_rowsWritten = 0;
	// HAS ANYTHING BEEN PRINTED YET? The first section clears the document; the ones after it print
	// below, because a composition's outputs share one sheet.
	bool                        m_started = false;
	// Does this output declare any measures? Without them the grand-total row has nothing to show.
	bool                        m_hasMeasures = false;
	// How many columns the DIMENSION area occupies (0 = this output has none). A total line writes
	// its caption inside that area and its figures outside it.
	int                         m_dimWidth = 0;
	// ⭐ THE GRAND TOTAL, HELD UNTIL THE END. The walk hands it over FIRST (it is the root of the
	// folded tree, and the walk is pre-order); a report reads it at the BOTTOM. Kept as the row it
	// arrived as and written by OnComplete through the ordinary row road, so nothing about how a
	// total looks is decided twice.
	std::vector<ibValue>        m_grandTotal;
	bool                        m_hasGrandTotal = false;

	// ⚠ NO PER-GROUP TOTAL LINE, and it was tried: a group's heading already carries its resources
	// beside its name, so a "Total …" row under every group repeats what the line above it says and
	// the report stops being readable (Max, 2026-08-22). One total row exists — the grand one, at
	// the end of the section.
	void WriteTotalLine(int level, const std::vector<ibValue>& values, bool grand);

	// -----------------------------------------------------------------------
	//  THE CROSS-TABLE LAYOUT
	//
	// ⭐⭐ A CROSS-TABLE CANNOT BE PRINTED AS IT ARRIVES, and that is its one real difference. A
	// grouping's width is known from the schema before the first row; a table's is the number of
	// DISTINCT column keys, which is known only when the last row has been read. So this layout
	// holds what it walked and prints in OnComplete.
	//
	// ⚠ AND WHAT IT HOLDS IS GROUPS, NOT ROWS — headings × column keys, never the detail records
	// underneath. That is the same bound the reports arc was built to: "the memory is the number of
	// GROUPS, not of rows". A cross-table IS a table of groups, so holding one does not walk that
	// back; holding the rows it was folded from would.
	// -----------------------------------------------------------------------

	// ⭐ A COLUMN KEY IS A KEY PER LEVEL, NOT A LIST OF VALUES — because a level may group by several
	// fields, welded into ONE heading. Flattening them would make a two-field column heading look
	// two levels deep, which is the same slip that once dropped the last row level off the page
	// (see the note beside the dimension layout in OnColumns).
	using CrossKey = std::vector<std::vector<ibValue>>;

	// ONE HEADING DOWN THE PAGE, with whatever was found across it.
	struct CrossRow
	{
		int                  m_level = 0;      // depth of the heading — indent and tint read off it
		std::vector<ibValue> m_heading;        // its own dimension values (its level's fields)
		std::vector<ibValue> m_measures;       // the heading's own figures = the row's total
		// column key index -> the figures where that column meets this row. Sparse on purpose: a
		// pair that never occurred has no cell, and an empty cell is what "never happened" looks
		// like — printing a zero there would state a fact nobody measured.
		std::map<size_t, std::vector<ibValue>> m_cells;
		// …and the SUBTOTALS, under the prefix they total. A column axis deeper than one level has a
		// figure per upper heading too, and the fold computed it at that node — so it costs nothing
		// to keep and would cost a third read to recover.
		//
		// ⚠ A LIST, NOT A MAP, and deliberately: a map would order the prefixes by `ibValue::operator<`,
		// which compares across types and RAISES where they do not compare — on a key made of a
		// reference and a date that is a throw in the middle of printing. Prefixes are few (one per
		// upper heading) and are found by walking, which needs only equality.
		std::vector<std::pair<CrossKey, std::vector<ibValue>>> m_subtotals;
	};

	// ⭐ ONE COLUMN OF THE PRINTED TABLE — either a column KEY, or the SUBTOTAL of an upper heading
	// that has just closed. They are one list because the grid is one row of columns: what makes a
	// slot a subtotal is what it is asked for, not where it sits.
	struct ColumnSlot
	{
		CrossKey m_key;                   // the full key, or the prefix a subtotal totals
		size_t   m_at       = 0;          // index into m_colKeys — meaningless for a subtotal
		bool     m_subtotal = false;
	};

	void OnCrossHeading(int level, const std::vector<ibValue>& values);
	void WriteCrossTable();
	// THE COLUMNS IN PRINTING ORDER — the keys as they came, with each upper heading's subtotal
	// inserted where that heading ends. Built once per table, because the header and every row have
	// to agree about which column is which.
	std::vector<ColumnSlot> BuildColumnSlots() const;
	// The schema, kept because a table's header is written at the END — when the width is finally
	// known — and by then the walk is over.
	std::vector<ibQueryLowering::OutputColumn> m_schema;
	// WHERE A COLUMN KEY SITS, adding it if this is its first sighting. First-seen order IS the
	// column order — the read already came back sorted (the query's ORDER BY, the level's own sort),
	// so re-sorting here would be a second opinion about an order somebody already stated.
	size_t ColumnKeyIndex(const CrossKey& key);

	bool   m_cross = false;      // does this output have a column axis at all?
	size_t m_rowLevels = 0;      // dimensions that read down the page; the rest read across
	size_t m_colLevels = 0;

	std::vector<size_t> m_measureAt;   // schema indices of the measures, in the order declared
	CrossKey              m_colPath;   // the column key being walked — one entry per column level
	std::vector<CrossKey> m_colKeys;   // the distinct column keys, in first-seen order
	// …and the distinct PREFIXES that carry a subtotal — the upper column headings. Held for the
	// whole table, not per row: a column exists or it does not, and one row having nothing under a
	// heading is not the heading going away.
	std::vector<CrossKey> m_colSubtotalKeys;
	std::vector<CrossRow> m_crossRows;
	// How many detail records a table dropped. Counted, not ignored: a report that quietly discards
	// rows is indistinguishable from one that never read them, and the journal is where that
	// difference belongs.
	int m_crossDetailsDropped = 0;
	// Was this table the first section on the sheet? Answered where the section began, because by
	// the time it prints, the answer has stopped being visible.
	bool m_crossFirstSection = false;

	// THE SECOND FOLD — the column totals (see ibCompositionDriver::WantsColumnTotals). The flag is
	// set from the output info rather than worked out, and the cells land in the bottom row of the
	// table by the same column key the first pass built.
	bool m_columnTotalsPass = false;
	std::map<size_t, std::vector<ibValue>> m_columnTotalCells;
	// …and the bottom line's figures for the SUBTOTAL columns — the same prefixes, one level up.
	std::vector<std::pair<CrossKey, std::vector<ibValue>>> m_columnTotalSubtotals;
	// The grand total AS MEASURES — a table lays its cells out per measure, so it stores the figures
	// rather than the row they arrived in. (The streaming layout keeps the whole row in m_grandTotal
	// and reads it through m_layout: same values, different question.)
	std::vector<ibValue> m_crossGrandTotal;
};

#endif // __SPREADSHEET_COMPOSE_DRIVER_H__
