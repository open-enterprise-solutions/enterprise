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

	virtual void OnColumns(const std::vector<ibQueryLowering::OutputColumn>& schema) override;
	virtual void OnRow(int level, bool hasChildren, const std::vector<ibValue>& values) override;
	virtual void OnComplete(bool totals) override;

	// How many rows the walk printed under the heading — 0 means the composition produced nothing,
	// which is a legitimate answer and NOT an error (the caller decides what to say).
	int GetRowsWritten() const { return m_rowsWritten; }

private:
	// The heading (title + parameter lines) as an area of its own.
	void WriteHeading();

	// The widest text written in each column, in characters — the report sizes its own columns
	// (a composed report has no author to drag a border, and a value clipped to the default width
	// reads as a different value).
	std::vector<size_t>         m_widest;

	// WHERE EACH SCHEMA ENTRY LANDS. Sheet column per schema index (-1 = not written), and the
	// schema indices of the DIMENSIONS in level order — they all share one column, so a row needs
	// the order to know which of them is its own. See OnColumns for why the layout is by ROLE.
	std::vector<int>            m_layout;
	std::vector<size_t>         m_dimColumns;

	ibBackendSpreadsheetObject* m_document = nullptr;
	wxString                    m_title;
	std::vector<wxString>       m_headerLines;
	int                         m_columnCount = 0;
	int                         m_rowsWritten = 0;
};

#endif // __SPREADSHEET_COMPOSE_DRIVER_H__
