#include "backend/composition/spreadsheetComposeDriver.h"

#include <algorithm>   // std::min — MSVC drags it in transitively, libstdc++ does not
#include <map>         // the per-level field counts the dimension layout is built from

namespace {
// One level of nesting, spelled in spaces. The indent is DECORATION over the outline group —
// the group is what actually folds; this is what makes the shape readable while everything is
// expanded, which is how a report is usually read.
constexpr int kIndentPerLevel = 3;
// HOW WIDE THE GAP BETWEEN TWO OUTPUTS IS. Two lines, because one reads as a row that failed to
// print rather than as the end of a report.
constexpr int kSectionGapLines = 2;

// COLUMN WIDTH FROM CONTENT. There is no device context here (this is the backend), so the width is
// counted in CHARACTERS and turned into pixels by an average glyph — near enough for a column that
// only has to stop clipping, and wrong in neither direction by more than a character or two.
// Clamped at both ends: never below the sheet's own default, never so wide that one long string
// pushes every other column off the screen.
constexpr int kPixelsPerChar = 7;
constexpr int kCellPadding   = 12;
constexpr int kMinColWidth   = 70;
constexpr int kMaxColWidth   = 420;

// ⚠ PROVISIONAL COLOURS, and deliberately quiet ones. A grouping row and the header are tinted
// so the structure reads at a glance while the report is scrolled — a neutral green that BLENDS
// rather than announces itself. The deeper the level, the paler the tint, so nesting is visible
// without a second mechanism.
//
// They belong in the palette (docs/ui-palette.md) rather than in a driver, and they are here
// only until the report gets its own palette roles — a report that prints must eventually take
// these from the theme, not from a constant.
const wxColour kHeaderFill(0xD4, 0xE4, 0xD4);
const wxColour kGroupFillOuter(0xE2, 0xEE, 0xE2);
const wxColour kGroupFillInner(0xF0, 0xF7, 0xF0);

// A DETAIL ROW IS NOT BLANK PAPER. Left untinted it comes out pure white between the tinted group
// rows, and pure white is the loudest thing on a page of soft greens — the eye lands on the last
// row instead of on the structure (Max, 2026-08-19: "the last row stands out all white; give it
// something very neutral so the white does not catch the eye").
const wxColour kDetailFill(0xFA, 0xFA, 0xF8);

// The tint for a grouping row at `level` — outermost is the strongest, and three levels down it
// has faded into the page. Anything deeper keeps the palest shade rather than disappearing.
wxColour ibGroupFillForLevel(int level)
{
	switch (level) {
	case 0:  return kGroupFillOuter;
	case 1:  return wxColour(0xE9, 0xF2, 0xE9);
	default: return kGroupFillInner;
	}
}

} // namespace

void ibSpreadsheetComposeDriver::OnColumns(const std::vector<ibQueryLowering::OutputColumn>& schema)
{
	if (m_document == nullptr)
		return;

	// ⭐ AN OUTPUT IS A SECTION OF ONE SHEET (Max). The composition hands its outputs to the SAME
	// driver, one after another, and each prints below the previous one — so the document is
	// cleared for the FIRST section only.
	//
	// Clearing on every section is what would make a second output erase the first: the report
	// would show whichever output happened to print last and look as if the others were never
	// declared. A NEW COMPOSITION still replaces the old one — the run starts with m_started false
	// (change a filter, press Compose again), and appending there would double the data.
	// ⚠ ONE FLAG ANSWERS "IS THIS THE FIRST SECTION", and everything that depends on it reads THAT
	// one. Clearing keyed on `m_started` while the heading and the freeze keyed on
	// `m_rowsWritten == 0` are the same question asked two ways — and they disagree the moment a
	// first output returns NO ROWS: the second one then prints the title again and freezes a second
	// time, which is the "scroll does nothing" fault by another road.
	const bool firstSection = !m_started;
	if (firstSection) {
		m_document->ClearSpreadsheet();
		m_rowsWritten = 0;
		m_started = true;
	}
	else {
		// ⭐ A CLEAR GAP BETWEEN SECTIONS (Max). One blank line is not a separation — it reads as a
		// row that failed to print. Two say "this report ended, another begins", which is what a
		// person needs to see before the next header.
		//
		// Written at level 0 and left WHITE: the gap belongs to no grouping, and tinting it would
		// make it look like a heading of its own.
		for (int line = 0; line < kSectionGapLines; ++line) {
			wxObjectDataPtr<ibBackendSpreadsheetObject> gap(new ibBackendSpreadsheetObject());
			gap->SetCellValue(0, 0, wxEmptyString);
			m_document->PutArea(gap, 0);
		}
	}

	// ⭐⭐ THE LAYOUT IS THE REPORT'S, NOT THE QUERY'S. A schema is a list of columns; a report is
	// three different things laid out three different ways, and the roles the lowering stamps say
	// which is which:
	//
	//   * DIMENSIONS stack into ONE column, read DOWN the page — each level indented under the one
	//     above. Giving every level its own column spreads a two-level report across the screen and
	//     leaves both columns mostly empty.
	//   * MEASURES take a column each, numbers to the right — those are the figures the eye scans.
	//   * DETAILS (a query with no TOTALS at all) are the ordinary case: a column each.
	m_layout.assign(schema.size(), -1);
	m_dimLevel.assign(schema.size(), -1);

	// ⭐ A LEVEL MAY BE MADE OF SEVERAL FIELDS, AND THEY ARE WELDED TOGETHER (Max): they belong to
	// ONE heading, so they are written SIDE BY SIDE on its row — not one under another, which would
	// read as three levels where the author declared one.
	//
	// So the dimension area is as wide as the widest level, and a field's column is its position
	// INSIDE its level. Counting dimension columns as if each were a level — which is what this did
	// — put the second field of one level where the next level belonged, and the last level fell off
	// the page: exactly the "the date disappears" report.
	size_t widestLevel = 1;
	{
		std::map<int, size_t> perLevel;
		for (const ibQueryLowering::OutputColumn& column : schema)
			if (column.m_role == ibQueryLowering::ibColumnRole::Dimension)
				widestLevel = std::max(widestLevel, ++perLevel[column.m_level]);
	}

	int next = 0;
	const int dimBase = next;
	std::map<int, int> filledInLevel;   // level -> how many of its fields are already placed
	bool anyDimension = false;
	for (size_t i = 0; i < schema.size(); ++i)
		if (schema[i].m_role == ibQueryLowering::ibColumnRole::Dimension) { anyDimension = true; break; }
	if (anyDimension)
		next += static_cast<int>(widestLevel);
	// ⭐ HOW WIDE THE DIMENSION AREA IS — 0 when this output has no dimensions at all (resources with
	// no grouping: one grand total over everything). A total line writes its caption INSIDE that
	// area; with no area there is no free column, and writing into column 0 anyway would overwrite
	// the first FIGURE with the word "Total".
	m_dimWidth = anyDimension ? static_cast<int>(widestLevel) : 0;

	for (size_t i = 0; i < schema.size(); ++i) {
		switch (schema[i].m_role) {
		case ibQueryLowering::ibColumnRole::Dimension: {
			const int level = schema[i].m_level;
			m_dimLevel[i] = level;
			m_layout[i] = dimBase + filledInLevel[level]++;   // its place INSIDE its own level
			break;
		}
		default:
			m_layout[i] = next++;            // a measure or a detail — one column each
			break;
		}
	}
	m_columnCount = next;
	m_widest.assign(static_cast<size_t>(m_columnCount), 0);

	// HAS THIS OUTPUT ANY FIGURES? The grand-total row (depth 0) carries no dimension value — it
	// stands for everything — so with no measures it has nothing at all to say, and printing it
	// leaves a blank stripe above the first heading.
	m_hasMeasures = false;
	for (const ibQueryLowering::OutputColumn& column : schema)
		if (column.m_role == ibQueryLowering::ibColumnRole::Measure) { m_hasMeasures = true; break; }

	// THE TITLE AND THE PARAMETER LINES BELONG TO THE REPORT, not to each section — repeating them
	// over every output would say the same thing three times on one page.
	if (firstSection)
		WriteHeading();

	// THE HEADER IS AS TALL AS THE DIMENSIONS ARE DEEP: one line per level, each naming its own
	// level, stacked in the same column they will be read in. A measure names itself on the first
	// line and its column stays clear underneath. Written as ONE area, put at level 0 — a heading
	// is not inside any grouping.
	int deepestLevel = 0;
	for (const int level : m_dimLevel)
		deepestLevel = std::max(deepestLevel, level + 1);
	const int headerRows = std::max(1, deepestLevel);
	wxObjectDataPtr<ibBackendSpreadsheetObject> header(new ibBackendSpreadsheetObject());
	for (size_t i = 0; i < schema.size(); ++i) {
		const int col = m_layout[i];
		if (col < 0)
			continue;
		int row = 0;
		if (schema[i].m_role == ibQueryLowering::ibColumnRole::Dimension)
			row = std::max(0, m_dimLevel[i]);   // one header line per LEVEL, not per dimension column
		header->SetCellValue(row, col, schema[i].m_name);
		m_widest[static_cast<size_t>(col)] = std::max(m_widest[static_cast<size_t>(col)], schema[i].m_name.length());
	}
	for (int row = 0; row < headerRows; ++row)
		for (int col = 0; col < m_columnCount; ++col)
			header->SetCellBackgroundColour(row, col, kHeaderFill);

	m_document->PutArea(header, 0);

	// Everything down to and including the column titles stays put while the rows scroll under it.
	//
	// ⚠ THE FIRST SECTION ONLY. Freezing again on the next output would pin everything printed so
	// far — the whole previous report plus this header — and the sheet stops scrolling: the rows
	// keep coming, the view does not move, which is exactly "with two reports the scroll does
	// nothing". A frozen area is the page's, not the section's.
	if (firstSection)
		m_document->SetRowFreeze(m_document->GetNumberRows());
}

// THE HEADING — title, then one line per parameter. Written across the table's whole width (a merged
// cell), because a title that stops at the first column reads as a value of that column.
void ibSpreadsheetComposeDriver::WriteHeading()
{
	if (m_title.IsEmpty() && m_headerLines.empty())
		return;   // nothing to say — the table starts at the top, exactly as before

	const int span = (m_columnCount > 0) ? m_columnCount : 1;
	wxObjectDataPtr<ibBackendSpreadsheetObject> heading(new ibBackendSpreadsheetObject());
	int row = 0;

	if (!m_title.IsEmpty()) {
		heading->SetCellValue(row, 0, m_title);
		heading->SetCellSize(row, 0, 1, span);
		++row;
	}

	for (const wxString& line : m_headerLines) {
		heading->SetCellValue(row, 0, line);
		heading->SetCellSize(row, 0, 1, span);
		++row;
	}

	heading->SetCellValue(row, 0, wxEmptyString);   // one blank line between the heading and the table

	m_document->PutArea(heading, 0);
}

void ibSpreadsheetComposeDriver::OnRow(int level, bool hasChildren, const std::vector<ibValue>& values)
{
	if (m_document == nullptr)
		return;

	// THE GRAND TOTAL IS A ROW ONLY IF THERE IS A TOTAL. Depth 0 is the level above every heading:
	// it holds no dimension value, so with no measures declared it prints an empty tinted line
	// between the header and the first group — which reads as a drawing fault, not as a total.
	if (level == 0 && hasChildren && !m_hasMeasures)
		return;

	// ⭐ AND IT IS WRITTEN LAST (Max, 2026-08-21: "the totals must always be at the end"). The walk
	// is pre-order, so the root arrives BEFORE every heading — printed where it arrives it sits
	// above the first group, which is where a reader looks for the column titles, not for the sum
	// of everything. Held here and written by OnComplete, at the bottom of this output's section.
	if (level == 0 && hasChildren) {
		m_grandTotal = values;
		m_hasGrandTotal = true;
		return;
	}

	// ⚠ NO "TOTAL" LINE UNDER EVERY GROUP. It was tried and it is nonsense (Max, 2026-08-22): the
	// heading already carries the group's RESOURCES beside its name, so a line repeating them under
	// the group says the same thing twice — a group is "name + figures", one row. Only the GRAND
	// total stands on its own, at the end of the section (OnComplete).

	// WHICH DIMENSION THIS ROW IS. The walk is pre-order and `level` is the depth, so a row at level
	// N carries level N's own value — the levels above it are already written on the rows above, and
	// repeating them would print the parent's name on every child.
	const size_t ownDim = (level > 0) ? static_cast<size_t>(level - 1) : 0;

	wxObjectDataPtr<ibBackendSpreadsheetObject> row(new ibBackendSpreadsheetObject());

	for (size_t i = 0; i < values.size() && i < m_layout.size(); ++i) {
		const int col = m_layout[i];
		if (col < 0)
			continue;

		const bool isDimension = i < m_dimLevel.size() && m_dimLevel[i] >= 0;
		// EVERY FIELD OF THIS LEVEL, and only this level's. They are welded into one heading, so
		// they all go on this row, side by side; the levels above are already written above.
		if (isDimension && static_cast<size_t>(m_dimLevel[i]) != ownDim)
			continue;

		const ibValue& value = values[i];
		wxString text = value.GetString();
		// The indent rides on the FIRST field of the level — the column the grouping is read down.
		if (isDimension && level > 0 && m_layout[i] == 0)
			text = wxString(wxT(' '), level * kIndentPerLevel) + text;

		row->SetCellValue(0, col, text);

		// A CELL CARRIES ITS VALUE, and WHAT that means is the value's own business: the click ends in
		// ibValue::ShowValue (OpenCellDetailsParameter), and a value with nothing to show simply shows
		// nothing. Asking here which types are "openable" would be a second, poorer answer to a
		// question the value already answers (Max, 2026-08-19). The parameter travels with the area —
		// PutArea re-keys it into the document — so the binding survives the move.
		if (!value.IsEmpty()) {
			const wxString name = wxString::Format(wxT("Cell_%d"), col);
			row->SetParameter(name, value);
			row->SetCellDetailsParameter(0, col, name);
		}

		// A NUMBER READS DOWN ITS RIGHT EDGE. Measures are what the eye scans and compares, and
		// left-aligned figures cannot be compared without reading each one.
		if (value.GetType() == ibValueTypes::TYPE_NUMBER)
			row->SetCellAlignment(0, col, wxALIGN_RIGHT, wxALIGN_CENTER);

		if (static_cast<size_t>(col) < m_widest.size())
			m_widest[static_cast<size_t>(col)] = std::max(m_widest[static_cast<size_t>(col)], text.length());
	}

	// A GROUPING ROW IS TINTED ACROSS ITS WHOLE WIDTH AND BOLD — the tint says "a level starts here"
	// while scrolling, the weight still says it in print. A detail row is tinted too, faintly: left
	// white it would be the only pure white on a page of soft greens.
	const wxColour fill = hasChildren ? ibGroupFillForLevel(level) : kDetailFill;
	wxFont font = s_defaultSpreadsheetFont;
	if (hasChildren)
		font.SetWeight(wxFontWeight::wxFONTWEIGHT_BOLD);
	for (int col = 0; col < m_columnCount; ++col) {
		row->SetCellBackgroundColour(0, col, fill);
		if (hasChildren)
			row->SetCellFont(0, col, font);
	}

	// ⭐ AND THE ONLY POSITIONAL THING THIS DRIVER SAYS: how deep the row is. Where it lands, how far
	// the group it opens reaches, which line carries the fold marker — all of that follows from the
	// order the rows arrived in, and belongs to the document and the grid.
	m_document->PutArea(row, static_cast<unsigned int>(std::max(0, level)));
	++m_rowsWritten;
}

// The total line itself — the measures, plus a caption saying what they add up.
//
// ⚠ ASCII ONLY IN THE CAPTION for the same reason every literal here is: this file is read as ANSI
// by MSVC unless it carries a BOM.
void ibSpreadsheetComposeDriver::WriteTotalLine(int level, const std::vector<ibValue>& values, bool grand)
{
	if (m_document == nullptr || !m_hasMeasures)
		return;   // nothing to total — a line saying "Total" with no figure says nothing

	wxObjectDataPtr<ibBackendSpreadsheetObject> row(new ibBackendSpreadsheetObject());

	// THE CAPTION GOES INSIDE THE DIMENSION AREA — the column the groupings are read down, which is
	// where a reader looks for what a row IS.
	//
	// ⚠ WITH NO DIMENSIONS THERE IS NO SUCH AREA (resources and no grouping: one row over
	// everything). Column 0 is then a FIGURE, and writing the word "Total" into it would replace the
	// first number with a caption — so the line is figures only, which is unambiguous when it is the
	// only row of the section.
	if (m_dimWidth > 0) {
		const wxString caption = _("Grand total");
		row->SetCellValue(0, 0, caption);
		if (!m_widest.empty())
			m_widest[0] = std::max(m_widest[0], caption.length());
	}
	(void)grand;   // one total line exists now — the grand one; see the header

	// THE FIGURES, in their own columns — the same cells an ordinary row writes, so a total reads
	// down the same edge as the numbers it sums.
	for (size_t i = 0; i < values.size() && i < m_layout.size(); ++i) {
		const int col = m_layout[i];
		const bool isDimension = i < m_dimLevel.size() && m_dimLevel[i] >= 0;
		if (col < m_dimWidth || isDimension)
			continue;
		const ibValue& value = values[i];
		const wxString text = value.GetString();
		row->SetCellValue(0, col, text);
		if (!value.IsEmpty()) {
			const wxString name = wxString::Format(wxT("Cell_%d"), col);
			row->SetParameter(name, value);
			row->SetCellDetailsParameter(0, col, name);
		}
		if (value.GetType() == ibValueTypes::TYPE_NUMBER)
			row->SetCellAlignment(0, col, wxALIGN_RIGHT, wxALIGN_CENTER);
		if (static_cast<size_t>(col) < m_widest.size())
			m_widest[static_cast<size_t>(col)] = std::max(m_widest[static_cast<size_t>(col)], text.length());
	}

	// TINTED AND BOLD LIKE THE HEADING IT BELONGS TO — a total is the group's other half, so it reads
	// as part of it rather than as a stray row.
	const wxColour fill = ibGroupFillForLevel(level);
	wxFont font = s_defaultSpreadsheetFont;
	font.SetWeight(wxFontWeight::wxFONTWEIGHT_BOLD);
	for (int col = 0; col < m_columnCount; ++col) {
		row->SetCellBackgroundColour(0, col, fill);
		row->SetCellFont(0, col, font);
	}

	m_document->PutArea(row, static_cast<unsigned int>(std::max(0, level)));
	++m_rowsWritten;
}

void ibSpreadsheetComposeDriver::OnComplete(bool /*totals*/)
{
	if (m_document == nullptr)
		return;

	// THE GRAND TOTAL, at the bottom of the section it belongs to — written through the same total
	// line every group closes with, so a report has ONE shape of total row. The flag is cleared: the
	// next output has its own.
	if (m_hasGrandTotal) {
		const std::vector<ibValue> grand = m_grandTotal;
		m_hasGrandTotal = false;
		m_grandTotal.clear();
		WriteTotalLine(0, grand, /*grand*/true);
	}

	// EACH COLUMN AS WIDE AS WHAT IT HOLDS. A composed report has nobody to drag a border, and a
	// clipped value reads as a different value. Character count × an average glyph, clamped: never
	// narrower than the default, never so wide that one long string pushes the rest off-screen.
	for (int col = 0; col < m_columnCount && static_cast<size_t>(col) < m_widest.size(); ++col) {
		const int width = static_cast<int>(m_widest[static_cast<size_t>(col)]) * kPixelsPerChar + kCellPadding;
		m_document->SetColSize(col, std::min(std::max(width, kMinColWidth), kMaxColWidth));
	}

	// 🔒 A REPORT IS READ, NOT EDITED (Max, 2026-08-19: "by default the table comes up read-only, to
	// guarantee it stays as composed and that drill-down works"). Typing into a composed cell would
	// leave a document whose numbers no longer follow from the query that produced them, and whose
	// cell no longer matches the value bound behind it.
	m_document->EnableEditing(false);
}
