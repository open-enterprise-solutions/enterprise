#include "backend/composition/spreadsheetComposeDriver.h"

#include <algorithm>   // std::min — MSVC drags it in transitively, libstdc++ does not

namespace {
// One level of nesting, spelled in spaces. The indent is DECORATION over the outline group —
// the group is what actually folds; this is what makes the shape readable while everything is
// expanded, which is how a report is usually read.
constexpr int kIndentPerLevel = 3;

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

	// A NEW COMPOSITION REPLACES THE OLD ONE. Composing twice into the same document is the
	// ordinary case (change a filter, press Generate again), and appending would grow a report
	// that looks like it doubled its data.
	m_document->ClearSpreadsheet();
	m_rowsWritten = 0;

	// ⭐⭐ THE LAYOUT IS THE REPORT'S, NOT THE QUERY'S. A schema is a list of columns; a report is
	// three different things laid out three different ways, and the roles the lowering stamps say
	// which is which:
	//
	//   * DIMENSIONS stack into ONE column, read DOWN the page — each level indented under the one
	//     above. Giving every level its own column spreads a two-level report across the screen and
	//     leaves both columns mostly empty.
	//   * MEASURES take a column each, numbers to the right — those are the figures the eye scans.
	//   * DETAILS (a query with no TOTALS at all) are the ordinary case: a column each.
	m_dimColumns.clear();
	m_layout.assign(schema.size(), -1);

	int next = 0;
	int dimColumn = -1;
	for (size_t i = 0; i < schema.size(); ++i) {
		switch (schema[i].m_role) {
		case ibQueryLowering::ibColumnRole::Dimension:
			if (dimColumn < 0)
				dimColumn = next++;          // the first level opens the column the rest stack into
			m_layout[i] = dimColumn;
			m_dimColumns.push_back(i);       // …and the ORDER of the levels is how a row finds its own
			break;
		default:
			m_layout[i] = next++;            // a measure or a detail — one column each
			break;
		}
	}
	m_columnCount = next;
	m_widest.assign(static_cast<size_t>(m_columnCount), 0);

	WriteHeading();

	// THE HEADER IS AS TALL AS THE DIMENSIONS ARE DEEP: one line per level, each naming its own
	// level, stacked in the same column they will be read in. A measure names itself on the first
	// line and its column stays clear underneath. Written as ONE area, put at level 0 — a heading
	// is not inside any grouping.
	const int headerRows = std::max<int>(1, static_cast<int>(m_dimColumns.size()));
	wxObjectDataPtr<ibBackendSpreadsheetObject> header(new ibBackendSpreadsheetObject());
	for (size_t i = 0; i < schema.size(); ++i) {
		const int col = m_layout[i];
		if (col < 0)
			continue;
		int row = 0;
		if (schema[i].m_role == ibQueryLowering::ibColumnRole::Dimension) {
			const auto it = std::find(m_dimColumns.begin(), m_dimColumns.end(), i);
			row = static_cast<int>(std::distance(m_dimColumns.begin(), it));
		}
		header->SetCellValue(row, col, schema[i].m_name);
		m_widest[static_cast<size_t>(col)] = std::max(m_widest[static_cast<size_t>(col)], schema[i].m_name.length());
	}
	for (int row = 0; row < headerRows; ++row)
		for (int col = 0; col < m_columnCount; ++col)
			header->SetCellBackgroundColour(row, col, kHeaderFill);

	m_document->PutArea(header, 0);

	// Everything down to and including the column titles stays put while the rows scroll under it.
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

	// WHICH DIMENSION THIS ROW IS. The walk is pre-order and `level` is the depth, so a row at level
	// N carries level N's own value — the levels above it are already written on the rows above, and
	// repeating them would print the parent's name on every child.
	const size_t ownDim = (level > 0) ? static_cast<size_t>(level - 1) : 0;

	wxObjectDataPtr<ibBackendSpreadsheetObject> row(new ibBackendSpreadsheetObject());

	for (size_t i = 0; i < values.size() && i < m_layout.size(); ++i) {
		const int col = m_layout[i];
		if (col < 0)
			continue;

		const auto it = std::find(m_dimColumns.begin(), m_dimColumns.end(), i);
		const bool isDimension = it != m_dimColumns.end();
		if (isDimension && static_cast<size_t>(std::distance(m_dimColumns.begin(), it)) != ownDim)
			continue;   // another level's value belongs on another row

		const ibValue& value = values[i];
		wxString text = value.GetString();
		// The indent rides on the stacked dimension column — the one the grouping is read down.
		if (isDimension && level > 0)
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

void ibSpreadsheetComposeDriver::OnComplete(bool /*totals*/)
{
	if (m_document == nullptr)
		return;

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
