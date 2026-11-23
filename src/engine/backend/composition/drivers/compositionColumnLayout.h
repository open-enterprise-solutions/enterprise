#ifndef __COMPOSITION_COLUMN_LAYOUT_H__
#define __COMPOSITION_COLUMN_LAYOUT_H__

// ---------------------------------------------------------------------------
// THE TABLE'S COLUMN GROUPS, IN SHEET CELLS (docs/private/selected-field-groups.md §5.5).
//
// The law is the table's — deskgui/univ/dataview/datavlayout.cpp, docs/private/column-groups.md §1–§2 —
// read in cells instead of pixels: a leaf is one sheet column wide, a band is one sheet row. So a
// sheet printed from a list stands as the list does on screen.
//
//   * horizontal  — members side by side; a title takes one band of the header over all of them;
//   * vertical    — members one under another, each in a band of its own, its title too;
//   * in one cell — members side by side with no rule between them, and ONE header cell over them
//                   all — the group's — none of their own.
//
// The last member of a stack takes what height is left, the last of a row what width is left, so a
// line has no hole in it. Every printed line — a record, a heading, a total — is GetRowBandCount()
// sheet rows; the header GetHeaderBandCount().
// ---------------------------------------------------------------------------

#include "backend/composition/drivers/compositionDriver.h"   // ibCompositionColumnNode — the tree it is fed

#include <vector>

// WHERE A SCHEMA COLUMN IS WRITTEN inside a printed line: sheet columns across, bands down.
struct ibCompositionColumnPlacement
{
	size_t m_column = ibCompositionColumnNode::npos;   // the schema column
	int    m_col = 0, m_colSpan = 1;
	int    m_band = 0, m_bandSpan = 1;
	// MERGED with the column to its right (an in-cell group): one cell showing two values, so no rule
	// is drawn between them.
	bool   m_mergedRight = false;
};

// ONE CELL OF THE HEADER — a column's title (m_column set) or a group's (m_title).
struct ibCompositionHeaderCell
{
	size_t   m_column = ibCompositionColumnNode::npos;   // npos = a group's title
	ibString m_title;                                    // the group's; a column's is the driver's to word
	int      m_col = 0, m_colSpan = 1;
	int      m_band = 0, m_bandSpan = 1;
};

class BACKEND_API ibCompositionColumnLayout
{
public:
	// Lays the tree out from sheet column `firstColumn` on, under a header at least `headerBands` deep — the
	// dimension area's, one band per grouping level: a column standing alone takes the whole height either way.
	void Rebuild(const std::vector<ibCompositionColumnNode>& tree, int firstColumn, int headerBands);

	int GetRowBandCount() const { return m_rowBands; }         // sheet rows one printed line takes (>= 1)
	int GetHeaderBandCount() const { return m_headerBands; }   // sheet rows the header takes (>= 1)
	int GetColumnCount() const { return m_columnCount; }       // sheet columns the tree takes

	// Every column laid out, in the tree's order.
	const std::vector<ibCompositionColumnPlacement>& GetPlacements() const { return m_body; }
	// Every header cell — group titles AND column titles; a column under an in-cell group has none.
	const std::vector<ibCompositionHeaderCell>& GetHeaderCells() const { return m_header; }

private:
	struct ibLayoutNode;

	static void MeasureNode(ibLayoutNode& node);
	static bool OwnsHeaderCell(const ibLayoutNode& node);
	void PlaceNode(const ibLayoutNode& node, int col, int width, int band, int bandSpan,
		int headerBand, int headerSpan, bool merged);

	std::vector<ibCompositionColumnPlacement> m_body;
	std::vector<ibCompositionHeaderCell>      m_header;
	int m_rowBands = 1;
	int m_headerBands = 1;
	int m_columnCount = 0;
};

#endif // __COMPOSITION_COLUMN_LAYOUT_H__
