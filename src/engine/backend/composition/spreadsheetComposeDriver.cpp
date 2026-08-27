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

// ⭐ THE SHAPE IS ASKED FOR ONCE, HERE, and everything downstream reads the answer off these three
// members rather than working it out again. The axis of a column is the OUTPUT INFO's answer
// (AxisOf) and not a comparison this file makes: a printer that re-derived it would be a second
// place holding the same rule, and the two drift.
void ibSpreadsheetComposeDriver::OnOutputBegin(const ibCompositionOutputInfo& info)
{
	m_columnTotalCells.clear();
	m_columnTotalSubtotals.clear();
	m_rowLevels = info.m_rowLevels;
	m_colLevels = 0;
	m_measureAt.clear();
	m_colPath.clear();
	m_colKeys.clear();
	m_colSubtotalKeys.clear();
	m_crossRows.clear();
	m_crossDetailRows = 0;
	m_detailsAcross = info.m_detailsAxis == ibTotalsAxis::Columns;

	std::vector<int> columnDepths;
	for (size_t i = 0; i < info.m_schema.size(); ++i) {
		switch (info.AxisOf(info.m_schema[i])) {
		case ibCompositionAxis::Columns:
			// COUNT LEVELS, NOT COLUMNS. A level may group by several fields, so counting entries
			// here would make a two-field column heading look like two levels deep — the same slip
			// that once dropped the last row level off the page.
			if (std::find(columnDepths.begin(), columnDepths.end(), info.m_schema[i].m_level) == columnDepths.end())
				columnDepths.push_back(info.m_schema[i].m_level);
			break;
		case ibCompositionAxis::None:
			if (info.m_schema[i].m_role == ibQueryLowering::ibColumnRole::Measure)
				m_measureAt.push_back(i);
			break;
		default:
			break;
		}
	}
	m_colLevels = columnDepths.size();

	// ⭐ AND THE TABLE LAYOUT IS ONLY TAKEN WHEN THERE IS SOMETHING TO LAY OUT ACROSS. An output with
	// no column axis is the ordinary report, printed as it arrives — the streaming path is not a
	// fallback here, it is the right answer for the shape that has a known width.
	// ⭐ …AND RECORDS READING ACROSS THE PAGE ARE A COLUMN LEVEL, though no DIMENSION column marks
	// them: their key is what each record says. Counted as one, because that is what they are — one
	// level of column headings — and without it a table whose columns are its records would come out
	// as an ordinary report, laid out down the page.
	// …ONE MORE LEVEL, not "at least one": the records are the DEEPEST column level, under whatever
	// groupings stand above them (Max's structure: Attribute2 → Attribute21 → Detail records). Read
	// as a floor of its own, the header gets its line and the groupings above it get their folds.
	if (m_detailsAcross)
		++m_colLevels;
	m_cross = (m_colLevels > 0) && (info.m_kind == ibCompositionOutputKind::Table);
	m_schema = info.m_schema;
	// ⭐ AND WHAT TO WRITE OVER EACH COLUMN — the composition's answer, not the query's name. Taken
	// whole, because the header is printed later (a table has no width until its last row arrives)
	// and the info does not outlive this call.
	m_titles.clear();
	m_titles.reserve(info.m_schema.size());
	for (size_t i = 0; i < info.m_schema.size(); ++i)
		m_titles.push_back(info.TitleOf(i));
	// ⭐ AND THE OUTPUT'S NAME IS TAKEN, which is the whole reason it travels. It was written by the
	// composer and read by nobody (audit § C8, "write-only"), so a report of two outputs printed two
	// blocks of figures with nothing to say which was which — and naming them is exactly what the
	// structure window offers a person to do.
	m_outputName = info.m_name;

	TakeSchema(info.m_schema);
}

// ⭐ WHICH HEADING THIS IS, asked of the walk instead of read off `hasChildren`. A row heading with
// no columns under it and a column heading are both "a node with children"; only the depth and the
// seam say which, and the seam is what OnOutputBegin was told.
void ibSpreadsheetComposeDriver::OnGroupBegin(int level, ibSelectorNodeKind /*kind*/, bool hasChildren,
	bool /*showsWhatIsUnder*/, const std::vector<ibValue>& values)
{
	if (!m_cross) {
		PrintRow(level, hasChildren, values);   // the ordinary report, printed as it arrives
		return;
	}
	OnCrossHeading(level, values);
}

// A COLUMN — a heading that reads ACROSS the page. The walk says so now (it knows each level's
// axis), where this driver used to work it out from the depth and the row-level count it had been
// told separately. Same drawing, one fewer thing to keep in step.
void ibSpreadsheetComposeDriver::OnColumn(int level, ibSelectorNodeKind kind,
	const std::vector<ibValue>& values)
{
	if (!m_cross)
		return;   // no column axis in this output — there is nowhere across to write
	if (kind == ibSelectorNodeKind::Detail)
		PrintCrossDetail(level, values);
	else
		OnCrossHeading(level, values);
}

// ⭐⭐ A HEADING IS CLOSED — everything under it has been written, so its figures are final.
//
// 🛑 THIS IS WHERE THE GRAND TOTAL BELONGS, and the lack of it is why the total used to be stashed
// in a field: a pre-order walk hands the root over FIRST, so printing it as it arrived put the sum
// of everything above the first group, where a reader looks for column titles (Max, 2026-08-21:
// "the totals must always be at the end"). Closing the root IS the end of the section, so the line
// goes here and nothing has to be remembered between two events.
void ibSpreadsheetComposeDriver::OnGroupEnd(int level, const std::vector<ibValue>& values)
{
	if (m_document == nullptr || m_cross)
		return;   // a table writes its totals with the table itself (WriteCrossTable)
	if (level != 0 || !m_hasMeasures)
		return;   // only the root carries the grand total, and only where there are figures to show
	WriteTotalLine(0, values, /*grand*/true);
	m_hasGrandTotal = false;   // …written here, so the end of the output has nothing left to do
}

// ⭐⭐ A DETAIL RECORD IS A LINE OF THE TABLE, WITH CELLS ACROSS IT (Max, 2026-08-26: "its own line,
// cells by the columns").
//
// 🛑 IT USED TO BE DROPPED, and the note here argued the drop: "a cell holds what was computed, not
// what it was computed from". That answered a question nobody asked. A detail record was never
// going INTO a cell — it is a ROW, exactly like a heading is, and what stands across a row are its
// columns. The fold hangs its cells under it (ibStreamingFold::Feed), and a row of a table whose
// figures happen to come from ONE source row is still a row of that table. What made the argument
// look right was the shape the fold had: the detail level came after the column keys, so a detail
// really did land inside a cell — a defect of the ladder, read as a fact about detail records.
//
// Its own values go where a heading's do: down the leftmost area, one line, indented past the last
// grouping. Empty ones are skipped — a record is identified by what it says, not by its blanks.
void ibSpreadsheetComposeDriver::OnRow(int level, const std::vector<ibValue>& values)
{
	if (!m_cross) {
		PrintRow(level, false, values);
		return;
	}
	PrintCrossDetail(level, values);
}

void ibSpreadsheetComposeDriver::PrintCrossDetail(int level, const std::vector<ibValue>& values)
{

	// ITS FIGURES, pulled out by role — the same as for a heading, because in a table a record IS
	// figured like one: COUNT is 1, SUM is the value (see ibStreamingFold::Finish).
	std::vector<ibValue> measures;
	measures.reserve(m_measureAt.size());
	for (const size_t slot : m_measureAt)
		measures.push_back(slot < values.size() ? values[slot] : ibValue());

	// ⭐⭐ A RECORD ON THE COLUMN AXIS IS A COLUMN, NOT A LINE. Same node, same kind, the other way
	// round: down the page each record is a line of the table, across it each one is a column of its
	// own — its key is what the record SAYS, and its figures are the cell where it meets the heading
	// it hangs under (Max, 2026-08-26: "exactly as in the rows, so in the columns").
	if (m_detailsAcross) {
		std::vector<ibValue> key;
		for (size_t i = 0; i < m_schema.size() && i < values.size(); ++i)
			if (m_schema[i].m_role == ibQueryLowering::ibColumnRole::Detail && !values[i].IsEmpty())
				key.push_back(values[i]);
		if (key.empty())
			key.push_back(ibValue(static_cast<int>(m_colKeys.size() + 1)));   // a record with nothing to show still owns a column

		// ⚠ IT IS THE DEEPEST COLUMN LEVEL, NOT A KEY OF ITS OWN. The groupings it hangs under are
		// already on `m_colPath`, and a record replaces only the floor it stands on — clearing the
		// path would take every record out of its group and flatten the header.
		if (m_colPath.size() >= m_colLevels)
			m_colPath.resize(m_colLevels - 1);
		m_colPath.push_back(key);
		const size_t at = ColumnKeyIndex(m_colPath);
		if (m_crossRows.empty())
			m_columnTotalCells[at] = measures;     // …under the root: what that column adds up to
		else
			m_crossRows.back().m_cells[at] = measures;
		++m_crossDetailRows;
		return;
	}

	CrossRow row;
	row.m_detail = true;
	// PAST THE LAST GROUPING, whatever the fold numbered it. The tint and the indent are what a
	// reader sees, and both are about where the line sits UNDER the headings — not about which
	// level of the config produced it.
	row.m_level  = static_cast<int>(m_rowLevels) + 1;

	// WHAT THE RECORD SAYS, side by side in the heading area — the fields the query projects, in
	// the order it projects them. The dimensions are already written on the headings above and the
	// measures stand in the cells, so neither is repeated here.
	for (size_t i = 0; i < m_schema.size() && i < values.size(); ++i) {
		if (m_schema[i].m_role != ibQueryLowering::ibColumnRole::Detail)
			continue;
		if (!values[i].IsEmpty())
			row.m_heading.push_back(values[i]);
	}

	row.m_measures = measures;

	m_colPath.clear();   // a new line starts a new sweep across the columns
	m_crossRows.push_back(std::move(row));
	++m_crossDetailRows;
}

void ibSpreadsheetComposeDriver::TakeSchema(const std::vector<ibQueryLowering::OutputColumn>& schema)
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

	// ⭐ A TABLE HAS NO WIDTH YET. Its columns are the DISTINCT column keys, and the first of them
	// has not arrived — so there is nothing to lay out, nothing to head, and nothing to freeze. All
	// of it is written by WriteCrossTable, where the width is finally known.
	//
	// The section above it still happened: clearing, and the gap after a previous output, are about
	// where this output STARTS, which is known now and stops being knowable later — and so is
	// WHETHER THIS IS THE FIRST section, which is what decides the freeze. Carried rather than
	// re-derived at print time: "is this the first section" asked a second way is exactly the fault
	// the flag above exists to prevent.
	if (m_cross) {
		m_crossFirstSection = firstSection;
		return;
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
	WidenTo(m_columnCount, firstSection);

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
	WriteOutputCaption();

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
		// WHAT A PERSON READS OVER THE COLUMN — the composition's title for it. The query's NAME is
		// what a script looks the column up by and may have been qualified to stay unique; the
		// header is not the place that difference should ever show (see m_titles).
		const wxString caption = ColumnTitle(i, schema);
		header->SetCellValue(row, col, caption);
		m_widest[static_cast<size_t>(col)] = std::max(m_widest[static_cast<size_t>(col)], caption.length());
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
// See the header: the widths are the SHEET's, so they are cleared once per composition and only
// ever grow after that.
void ibSpreadsheetComposeDriver::WidenTo(int columns, bool firstSection)
{
	const size_t want = static_cast<size_t>(std::max(0, columns));
	if (firstSection)
		m_widest.assign(want, 0);
	else if (m_widest.size() < want)
		m_widest.resize(want, 0);
}

// ⭐ THE OUTPUT NAMES ITSELF over its own block — under the report's heading and above its own
// column titles, because it captions one block and the heading captions the page. Written the same
// way for both layouts, so a table and a grouping are captioned by one rule and neither has its own.
//
// An output nobody named prints nothing at all: a blank caption line reads as a row that failed.
void ibSpreadsheetComposeDriver::WriteOutputCaption()
{
	if (m_document == nullptr || m_outputName.IsEmpty())
		return;

	wxObjectDataPtr<ibBackendSpreadsheetObject> caption(new ibBackendSpreadsheetObject());
	caption->SetCellValue(0, 0, m_outputName);
	wxFont font = s_defaultSpreadsheetFont;
	font.SetWeight(wxFontWeight::wxFONTWEIGHT_BOLD);
	caption->SetCellFont(0, 0, font);
	m_document->PutArea(caption, 0);
}

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

void ibSpreadsheetComposeDriver::PrintRow(int level, bool hasChildren, const std::vector<ibValue>& values)
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

// ===========================================================================
//  The cross-table — walked into a rectangle, printed when its width is known
// ===========================================================================

namespace {

// THE VALUES ONE LEVEL OWNS. A level may group by several fields; they are welded into one heading,
// so all of them belong to it and none of them to the levels above (which are already written).
std::vector<ibValue> ibValuesOfLevel(const std::vector<ibQueryLowering::OutputColumn>& schema,
	const std::vector<ibValue>& values, int dimLevel)
{
	std::vector<ibValue> own;
	for (size_t i = 0; i < schema.size() && i < values.size(); ++i)
		if (schema[i].m_role == ibQueryLowering::ibColumnRole::Dimension && schema[i].m_level == dimLevel)
			own.push_back(values[i]);
	return own;
}

// What one level of a key reads as in a header cell — its fields, side by side, because they are
// one heading and not several.
wxString ibHeadingText(const std::vector<ibValue>& values)
{
	wxString text;
	for (const ibValue& value : values) {
		if (!text.IsEmpty())
			text += wxT(" ");
		text += value.GetString();
	}
	return text;
}

} // namespace

size_t ibSpreadsheetComposeDriver::ColumnKeyIndex(const CrossKey& key)
{
	for (size_t i = 0; i < m_colKeys.size(); ++i)
		if (m_colKeys[i] == key)
			return i;
	m_colKeys.push_back(key);
	return m_colKeys.size() - 1;
}

void ibSpreadsheetComposeDriver::OnCrossHeading(int level, const std::vector<ibValue>& values)
{
	// THE FIGURES, PULLED OUT BY ROLE. A table stores measures, never the row as it arrived: its
	// cells are laid out per measure, and a row holds the dimension slots too. (The streaming layout
	// keeps the whole row and reads it through `m_layout` — the same values, a different question,
	// which is why the two do not share a member.)
	std::vector<ibValue> measures;
	measures.reserve(m_measureAt.size());
	for (const size_t at : m_measureAt)
		measures.push_back(at < values.size() ? values[at] : ibValue());

	// THE ROOT IS THE GRAND TOTAL, exactly as in the streaming layout — the walk is pre-order, so it
	// arrives first and is printed last.
	if (level <= 0) {
		m_crossGrandTotal = measures;
		m_hasGrandTotal   = true;
		return;
	}

	const size_t depth = static_cast<size_t>(level);

	// ⭐⭐ A COLUMN HEADING WITH NO ROW OPEN YET IS THE ROOT'S — the column TOTAL. The walk is
	// pre-order and the fold puts a heading's cells before its sub-headings, so everything that
	// arrives across the page before the first row heading belongs to the heading over everything.
	//
	// 🛑 THIS USED TO BE A SECOND READ of the whole output, with a flag saying which pass the driver
	// was in. It is the same figure either way — and asking the tree for it means the two can no
	// longer disagree about which column a total sits under, which was the one thing the second pass
	// had to be careful about.
	//
	// ⭐ AND THE COLUMN ORDER IS SETTLED HERE, by the total: the keys are numbered as the ROOT met
	// them, so every row afterwards lands in a column that already exists.
	if (m_crossRows.empty() && depth > m_rowLevels) {
		const size_t inColumns = depth - m_rowLevels;
		if (m_colPath.size() >= inColumns)
			m_colPath.resize(inColumns - 1);
		m_colPath.push_back(ibValuesOfLevel(m_schema, values, level - 1));

		// AN UPPER LEVEL TOTALS A PREFIX — and the bottom line needs that figure too, because the
		// table has a column for it (see BuildColumnSlots).
		if (inColumns != m_colLevels) {
			if (std::find(m_colSubtotalKeys.begin(), m_colSubtotalKeys.end(), m_colPath) == m_colSubtotalKeys.end())
				m_colSubtotalKeys.push_back(m_colPath);
			for (std::pair<CrossKey, std::vector<ibValue>>& kept : m_columnTotalSubtotals)
				if (kept.first == m_colPath) { kept.second = measures; return; }
			m_columnTotalSubtotals.emplace_back(m_colPath, measures);
			return;
		}
		m_columnTotalCells[ColumnKeyIndex(m_colPath)] = measures;
		return;
	}

	// A ROW HEADING. Its own figures ARE the row's total — the fold already computed them at this
	// node, so a table gets its row totals for nothing and needs no second pass for them.
	if (depth <= m_rowLevels) {
		m_colPath.clear();   // out of the columns of the row before: a new heading starts a new sweep
		CrossRow row;
		row.m_level    = level;
		row.m_heading  = ibValuesOfLevel(m_schema, values, level - 1);
		row.m_measures = measures;
		m_crossRows.push_back(row);
		return;
	}

	// A COLUMN HEADING, under the row heading that is open. Its depth INSIDE the column axis says
	// how much of the current key it replaces — everything from here down is new.
	const size_t inColumns = depth - m_rowLevels;             // 1-based within the column axis
	if (m_colPath.size() >= inColumns)
		m_colPath.resize(inColumns - 1);
	m_colPath.push_back(ibValuesOfLevel(m_schema, values, level - 1));
	// (A column with no row open at all is the ROOT's — the column total — and it was taken above.)

	// ⭐ THE DEEPEST COLUMN HEADING IS A CELL; THE ONES ABOVE IT ARE SUBTOTALS. A column axis of
	// Warehouse then Month has a figure per month AND a figure per warehouse, and the fold already
	// computed both — the upper node carries its own. They are kept under their PREFIX key, which is
	// what a subtotal is: the answer for everything that starts this way.
	if (inColumns == m_colLevels) {
		m_crossRows.back().m_cells[ColumnKeyIndex(m_colPath)] = measures;
		return;
	}
	std::vector<std::pair<CrossKey, std::vector<ibValue>>>& held = m_crossRows.back().m_subtotals;
	for (std::pair<CrossKey, std::vector<ibValue>>& kept : held)
		if (kept.first == m_colPath) { kept.second = measures; return; }
	held.emplace_back(m_colPath, measures);
	// AND THE PREFIX IS REMEMBERED FOR THE WHOLE TABLE, so a subtotal column exists even where the
	// row that first showed it has nothing else — the grid is one width for every row.
	if (std::find(m_colSubtotalKeys.begin(), m_colSubtotalKeys.end(), m_colPath) == m_colSubtotalKeys.end())
		m_colSubtotalKeys.push_back(m_colPath);
}

// ⭐⭐ THE WHOLE TABLE, WRITTEN ONCE, WHEN ITS WIDTH IS FINALLY KNOWN.
//
// The layout: the row headings on the left, then one BLOCK PER COLUMN KEY (a column per measure
// inside it), then the row total. The header is as tall as the column axis is deep, plus a line of
// measure names when there is more than one — because with two measures a single line would say
// "Warehouse A" over two figures and never say which is which.
//
// ⭐ AND THE UPPER HEADER LINES ARE MERGED AND MADE FOLDABLE — the same outline the rows have had
// since 2026-08-19, turned on its side. `AddColGroup` is what `PutArea` does for rows, so this is
// the mechanism that was already there rather than a second one (grid: ibGridColOutlineWindow).
// ⭐⭐ THE COLUMNS, IN THE ORDER THEY PRINT. The keys in the order they were first seen, and after
// each upper heading — the moment the next key stops sharing its prefix — that heading's own total.
//
// Deepest first when several close at once: `Warehouse/Dec` followed by `Store/Jan` closes the month
// group inside Warehouse and then Warehouse itself, and a total printed the other way round would
// put the wider figure inside the narrower one.
//
// ⚠ A PREFIX GETS A COLUMN ONLY IF SOMETHING TOTALLED IT (m_colSubtotalKeys). With a single-level
// column axis nothing does, and this returns exactly the keys — which is why the common table is
// laid out the same as before there were subtotals at all.
std::vector<ibSpreadsheetComposeDriver::ColumnSlot> ibSpreadsheetComposeDriver::BuildColumnSlots() const
{
	std::vector<ColumnSlot> slots;
	slots.reserve(m_colKeys.size() + m_colSubtotalKeys.size());

	const auto totalled = [this](const CrossKey& prefix) {
		return std::find(m_colSubtotalKeys.begin(), m_colSubtotalKeys.end(), prefix) != m_colSubtotalKeys.end();
	};

	// ⭐⭐ THE COLUMNS ARE ARRANGED BY THEIR PATH — a group is a BLOCK, and a block is what a header
	// can span and a fold can close.
	//
	// 🛑 THEY USED TO STAND IN FIRST-SEEN ORDER, and "this group opens here" was decided by comparing
	// a key with the one before it. That holds while the keys arrive grouped — which they do when the
	// column axis is made of GROUPINGS, because the fold walks them nested. It stops holding the
	// moment the deepest column level is the RECORDS: those arrive in the order the rows were read,
	// so two groups interleave, each opens many times, and its heading is printed over every fragment
	// (Max, 2026-08-26, looking at one typed-in value repeating across the header: "isn't that a
	// duplicate?"). It was not a duplicate — it was one group torn into pieces.
	//
	// The order of the GROUPS themselves is still first-seen, level by level: nothing is sorted by
	// value, so a table never rearranges what the data said — it only keeps together what belongs
	// together.
	std::vector<std::vector<CrossKey>> seen(std::max<size_t>(m_colLevels, 1));
	for (const CrossKey& key : m_colKeys)
		for (size_t d = 0; d < seen.size() && d < key.size(); ++d) {
			const CrossKey prefix(key.begin(), key.begin() + static_cast<std::ptrdiff_t>(d) + 1);
			if (std::find(seen[d].begin(), seen[d].end(), prefix) == seen[d].end())
				seen[d].push_back(prefix);
		}
	const auto rank = [&seen](const CrossKey& key, size_t d) -> size_t {
		if (d >= key.size())
			return static_cast<size_t>(-1);   // shorter than this floor — it sorts after what has one
		const CrossKey prefix(key.begin(), key.begin() + static_cast<std::ptrdiff_t>(d) + 1);
		return static_cast<size_t>(std::find(seen[d].begin(), seen[d].end(), prefix) - seen[d].begin());
	};

	std::vector<size_t> order(m_colKeys.size());
	for (size_t i = 0; i < order.size(); ++i)
		order[i] = i;
	std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
		for (size_t d = 0; d < seen.size(); ++d) {
			const size_t ra = rank(m_colKeys[a], d), rb = rank(m_colKeys[b], d);
			if (ra != rb)
				return ra < rb;
		}
		return false;
	});

	for (size_t k = 0; k < order.size(); ++k) {
		const size_t i = order[k];
		// ⭐⭐ A HEADING'S TOTAL IS ITS FIRST COLUMN, and that is what makes the fold possible at all.
		//
		// A row group folds because the group HAS a row of its own — its heading — which stays behind
		// when the children are hidden. A column group has no column of its own: its heading lives in
		// a merged cell ABOVE the children and collapses with them. Its total column IS that missing
		// column: expanded it shows what the group adds up to, collapsed it is all that is left, and
		// the fold marker sits on it.
		//
		// Read off the reference report Max found (2026-08-26): two collapsed periods show exactly one
		// column each, carrying the period's total, and the expanded one shows its total followed by
		// its single shop — a duplicate figure, and the whole reason it earns a column.
		//
		// 🛑 SO IT IS PRINTED EVEN OVER A SINGLE CHILD, which reads as a repeat until the group is
		// collapsed. Both readings were tried in one night: totals last (the children first, "the
		// order a report reads down the page") and no total over one child ("the group shows it
		// anyway") — and each left a group nothing could fold.
		for (size_t depth = 1; depth < m_colLevels; ++depth) {
			if (depth > m_colKeys[i].size())
				break;
			const CrossKey prefix(m_colKeys[i].begin(), m_colKeys[i].begin() + depth);
			// OPENED = this is the first column PRINTED, or the one printed before it did not start
			// this way. Asked in the printed order, not in the order the keys were met — that is the
			// whole point of arranging them above.
			bool opened = (k == 0);
			if (!opened) {
				const CrossKey& previous = m_colKeys[order[k - 1]];
				opened = previous.size() < depth
					|| !std::equal(prefix.begin(), prefix.end(), previous.begin());
			}
			if (!opened || !totalled(prefix))
				continue;
			ColumnSlot sub;
			sub.m_key      = prefix;
			sub.m_subtotal = true;
			slots.push_back(sub);
		}

		ColumnSlot slot;
		slot.m_key = m_colKeys[i];
		slot.m_at  = i;
		slots.push_back(slot);
	}
	return slots;
}

void ibSpreadsheetComposeDriver::WriteCrossTable()
{
	// HOW WIDE THE ROW-HEADING AREA IS — the widest row level, exactly as the streaming layout
	// measures it: a level's fields are welded side by side, so the area fits the widest heading.
	size_t rowDimWidth = 0;
	{
		std::map<int, size_t> perLevel;
		for (const ibQueryLowering::OutputColumn& column : m_schema)
			if (column.m_role == ibQueryLowering::ibColumnRole::Dimension
			    && column.m_level >= 0 && static_cast<size_t>(column.m_level) < m_rowLevels)
				rowDimWidth = std::max(rowDimWidth, ++perLevel[column.m_level]);
		// …AND A DETAIL LINE IS AS WIDE AS WHAT IT SAYS. Its fields are welded side by side exactly
		// as a level's are, and an area measured off the headings alone would clip the record to
		// them — silently, which is the one thing a report must not do with data it read.
		for (const CrossRow& line : m_crossRows)
			if (line.m_detail)
				rowDimWidth = std::max(rowDimWidth, line.m_heading.size());
	}
	const std::vector<ColumnSlot> slots = BuildColumnSlots();

	const int dimWidth  = static_cast<int>(std::max<size_t>(rowDimWidth, 1));
	const int measures  = static_cast<int>(m_measureAt.size());
	const int keys      = static_cast<int>(slots.size());
	const int perKey    = std::max(measures, 1);   // a table with no resources still has one column per key
	const int totalCols = dimWidth + keys * perKey + (measures > 0 ? measures : 0);

	m_columnCount = totalCols;
	m_dimWidth    = dimWidth;
	WidenTo(m_columnCount, m_crossFirstSection);

	// The report's own heading spans the table, so it is written now — the width it needs is the
	// number just worked out, and before this moment there was none. The output's caption follows
	// it, in that order: the report is what this page IS, the output is one block of it.
	if (m_crossFirstSection)
		WriteHeading();
	WriteOutputCaption();

	// ---- the header -------------------------------------------------------
	const int headerRows = static_cast<int>(m_colLevels) + (measures > 1 ? 1 : 0);
	wxObjectDataPtr<ibBackendSpreadsheetObject> header(new ibBackendSpreadsheetObject());
	// WHICH HEADER CELLS A MERGE COVERS — kept as it is built, because "is this cell somebody else's"
	// is not a question the document can be asked afterwards without reading its spans back out.
	std::vector<std::vector<bool>> covered(static_cast<size_t>(std::max(1, headerRows)),
		std::vector<bool>(static_cast<size_t>(std::max(1, totalCols)), false));
	const auto mergeAt = [&](int row, int col, int rowSpan, int colSpan) {
		if (rowSpan <= 1 && colSpan <= 1)
			return;
		header->SetCellSize(row, col, rowSpan, colSpan);
		for (int r = row; r < row + rowSpan && r < std::max(1, headerRows); ++r)
			for (int c = col; c < col + colSpan && c < totalCols; ++c)
				if (r != row || c != col)
					covered[static_cast<size_t>(r)][static_cast<size_t>(c)] = true;
	};

	// ⭐ THE ROW HEADINGS SHARE ONE COLUMN, SO THEIR NAMES SHARE ONE CELL. Every row level is read
	// down the same column — that is what the indent is for — so the caption over it has to name all
	// of them, joined: `Ref / YTFDS`.
	//
	// 🛑 IT WROTE THEM BY THEIR POSITION WITHIN THEIR OWN LEVEL, which is the right rule for the
	// LAYOUT (a level of two fields takes two columns) and the wrong one for the CAPTION: level 0's
	// first field and level 1's first field are both "column 0", so each level overwrote the one
	// above and the header showed the DEEPEST name alone (Max, 2026-08-26: "the group heading is not
	// shown").
	{
		std::map<int, wxString> caption;   // column within the dimension area -> its names, joined
		std::map<int, int> filled;
		for (size_t i = 0; i < m_schema.size(); ++i) {
			const ibQueryLowering::OutputColumn& column = m_schema[i];
			if (column.m_role != ibQueryLowering::ibColumnRole::Dimension
			    || column.m_level < 0 || static_cast<size_t>(column.m_level) >= m_rowLevels)
				continue;
			const int col = filled[column.m_level]++;
			if (col >= dimWidth)
				continue;
			wxString& into = caption[col];
			if (!into.IsEmpty())
				into += wxT(" / ");
			into += ColumnTitle(i);
		}
		for (const std::pair<const int, wxString>& named : caption) {
			header->SetCellValue(std::max(0, headerRows - 1), named.first, named.second);
			if (static_cast<size_t>(named.first) < m_widest.size())
				m_widest[static_cast<size_t>(named.first)] =
					std::max(m_widest[static_cast<size_t>(named.first)], named.second.length());
		}
	}

	// ⭐⭐ A COLUMN NAMES ITSELF ON ITS OWN LINE, AND ONLY THERE. The heading of a level is printed
	// over that level's TOTAL column — stretched down to the measures — and the children that follow
	// carry only their own names. Read off a reference report Max showed (2026-08-25): the responsible
	// person stands over their total, and above each of their orders the upper line is BLANK.
	//
	// 🛑 IT WAS A MERGE ACROSS THE WHOLE RUN, which said the same thing a second way: "Warehouse"
	// spread over its months AND its total. That reads well until two headings sit side by side —
	// then the run and the totals disagree about where one ends, and the fold markers with them.
	// A node states what it adds up to and then breaks itself down; that is the whole rule.
	// ⭐⭐ A HEADING COVERS ITS WHOLE GROUP — its own total AND every child under it (Max, 2026-08-25:
	// "the grouping has to run to the END; it must include all the groupings under it, and they open
	// up beneath it"). So each line of the header is a run of slots that agree down to that level,
	// merged into one cell; the levels below break that run into its parts.
	//
	// 🛑 I HAD IT NAMING ONLY ITS OWN TOTAL COLUMN, read off that screenshot — and that is a
	// different statement: it leaves the children with nothing above them, so nothing on the page
	// says whose they are. The reference has the heading over the whole band; the total is simply
	// the first column INSIDE it.
	for (size_t depthAt = 0; depthAt < m_colLevels; ++depthAt) {
		size_t runStart = 0;
		while (runStart < slots.size()) {
			size_t runEnd = runStart;
			while (runEnd + 1 < slots.size()) {
				const CrossKey& a = slots[runStart].m_key;
				const CrossKey& b = slots[runEnd + 1].m_key;
				bool same = true;
				for (size_t f = 0; f <= depthAt && same; ++f)
					same = (f < a.size() && f < b.size() && a[f] == b[f]);
				if (!same)
					break;
				++runEnd;
			}
			const int  first = dimWidth + static_cast<int>(runStart) * perKey;
			const int  span  = static_cast<int>(runEnd - runStart + 1) * perKey;
			const CrossKey& key = slots[runStart].m_key;
			// The level's value where the key reaches this deep; where it stops, this slot is the
			// TOTAL of the level above — and that is the word its own line carries.
			const wxString text = (depthAt < key.size()) ? ibHeadingText(key[depthAt])
				: (slots[runStart].m_subtotal && depthAt == key.size() ? wxString(wxT("Total")) : wxString());

			if (!text.IsEmpty()) {
				header->SetCellValue(static_cast<int>(depthAt), first, text);
				if (static_cast<size_t>(first) < m_widest.size())
					m_widest[static_cast<size_t>(first)] =
						std::max(m_widest[static_cast<size_t>(first)], text.length());
			}
			if (span > 1)
				mergeAt(static_cast<int>(depthAt), first, 1, span);
			runStart = runEnd + 1;
		}
	}

	// ⭐⭐ AND THE FOLD MARKS, STATED THE WAY A ROW STATES ITS OWN (Max, 2026-08-25: "add the groups
	// the way you did for rows"). A row says only how DEEP it is — `PutArea(row, level)` — and the
	// grid works out what folds what. So a column says the same: one entry per column, carrying its
	// depth, and `ibGrid::NormalizeColGroups` shapes them into bands.
	//
	// That reads directly off the layout, because the layout already has the shape the normaliser
	// expects: a heading's own total column comes FIRST and its children follow, deeper. The band it
	// makes is "everything under this heading", and the heading itself stays visible — which is what
	// a collapsed warehouse should look like.
	//
	// 🛑 IT USED TO HAND OVER FINISHED RANGES, reaching into the description for AddColGroup. That
	// worked only because nothing else ever produced column groups: the normaliser for them did not
	// exist, and a producer that stated depths — the way every row producer does — got one fold per
	// heading over itself and nothing else.
	// ⭐⭐ A BAND IS "THIS HEADING'S CHILDREN", AND ITS TOTAL STAYS. Now that the total CLOSES a group
	// rather than opening it, there is no line before the band to hang the button on — so the range
	// is stated finished, with the marker on its own first column (`m_head`). That is also what
	// collapsing should leave behind: the children fold away and the figure that sums them remains.
	//
	// 🛑 THE DEPTH-PER-COLUMN FORM CANNOT SAY THIS. `NormalizeColGroups` reads a list of headings and
	// folds what FOLLOWS each of them, which is only true while the heading comes first; fed this
	// order it would hand every total the NEXT group's children. The normaliser now steps aside when
	// a range says where its marker is (see it).
	// ⭐⭐ A FOLD BELONGS TO A HEADING THAT HAS CHILDREN — not to one that has a TOTAL. The two were
	// tied together while the total was what stayed visible on collapse; the reference report shows
	// otherwise (Max, 2026-08-26): its date headings fold, and there is not a total column in sight.
	// So the band is worked out from the KEYS, and what stays behind is the group's first child —
	// the column the marker sits on, and the only one that can carry it.
	// ⭐⭐ WHAT FOLDS IS THE CHILDREN; WHAT STAYS IS THE GROUP'S TOTAL COLUMN. A subtotal slot opens
	// its group, so it is both the anchor for the marker and the one column a collapsed group shows
	// — which is exactly what the reference does: a collapsed period is one column carrying its own
	// figure (Max, 2026-08-26).
	for (size_t s = 0; s < slots.size(); ++s) {
		if (!slots[s].m_subtotal)
			continue;
		const CrossKey& prefix = slots[s].m_key;

		// Everything after it that still starts with its key is what it folds.
		size_t last = s;
		for (size_t k = s + 1; k < slots.size(); ++k) {
			const CrossKey& other = slots[k].m_key;
			if (other.size() < prefix.size()
			    || !std::equal(prefix.begin(), prefix.end(), other.begin()))
				break;
			last = k;
		}
		if (last == s)
			continue;   // a total with nothing under it folds nothing

		const int head = dimWidth + static_cast<int>(s) * perKey;
		m_document->GetSpreadsheetDesc().AddColGroup(
			static_cast<unsigned>(head + perKey),                              // the children
			static_cast<unsigned>(dimWidth + static_cast<int>(last + 1) * perKey - 1),
			static_cast<unsigned>(prefix.size()), /*collapsed*/false, /*head*/head);
	}

	// The measure names, when there is more than one — otherwise the key above says it all.
	if (measures > 1) {
		for (int k = 0; k < keys; ++k)
			for (int m = 0; m < measures; ++m) {
				const int col = dimWidth + k * perKey + m;
				const wxString name = ColumnTitle(m_measureAt[static_cast<size_t>(m)]);
				header->SetCellValue(headerRows - 1, col, name);
				if (static_cast<size_t>(col) < m_widest.size())
					m_widest[static_cast<size_t>(col)] = std::max(m_widest[static_cast<size_t>(col)], name.length());
			}
	}

	// THE ROW TOTAL closes the table on the right. It is not a column key — it is what the row adds
	// up to — so it stands outside the key blocks and outside their folds.
	if (measures > 0) {
		const int first = dimWidth + keys * perKey;
		header->SetCellValue(0, first, wxT("Total"));
		if (measures > 1)
			mergeAt(0, first, static_cast<int>(m_colLevels), measures);
		if (static_cast<size_t>(first) < m_widest.size())
			m_widest[static_cast<size_t>(first)] = std::max<size_t>(m_widest[static_cast<size_t>(first)], 5);
	}

	// 🛑 TINT THE MAIN CELLS ONLY — NEVER THE ONES A MERGE COVERS. Touching a cell CREATES it in the
	// description, and the grid then loads it and asks for its size; a cell that some merge already
	// covers is marked with a negative span, and setting a size on it trips
	// `ibGrid::SetCellSize: setting cell size that is already part of another cell` — an int 3 in a
	// Debug build, at LOAD time, long after the report was composed (dump 2026-08-25 22:26, cell 2,2).
	//
	// This is the first place in the tree where a merged header and a full-width tint meet: the
	// streaming layout tints everything and merges nothing, the report heading merges and tints
	// nothing. A merged cell paints its whole span from its main cell anyway, so skipping the covered
	// ones costs nothing on screen.
	for (int row = 0; row < std::max(1, headerRows); ++row)
		for (int col = 0; col < totalCols; ++col)
			if (!covered[static_cast<size_t>(row)][static_cast<size_t>(col)])
				header->SetCellBackgroundColour(row, col, kHeaderFill);
	m_document->PutArea(header, 0);
	// THE FIRST SECTION ONLY — freezing again would pin everything printed so far and the sheet
	// stops scrolling (see OnColumns).
	if (m_crossFirstSection)
		m_document->SetRowFreeze(m_document->GetNumberRows());

	// ---- the rows ---------------------------------------------------------
	for (const CrossRow& source : m_crossRows) {
		wxObjectDataPtr<ibBackendSpreadsheetObject> row(new ibBackendSpreadsheetObject());

		// The heading, indented by its depth — the same indent the streaming layout uses, so a
		// nested row heading reads the same in both shapes.
		for (size_t f = 0; f < source.m_heading.size() && static_cast<int>(f) < dimWidth; ++f) {
			wxString text = source.m_heading[f].GetString();
			if (f == 0)
				text = wxString(wxT(' '), source.m_level * kIndentPerLevel) + text;
			row->SetCellValue(0, static_cast<int>(f), text);
			if (!source.m_heading[f].IsEmpty()) {
				const wxString name = wxString::Format(wxT("Cell_%d"), static_cast<int>(f));
				row->SetParameter(name, source.m_heading[f]);
				row->SetCellDetailsParameter(0, static_cast<int>(f), name);
			}
			if (f < m_widest.size())
				m_widest[f] = std::max(m_widest[f], text.length());
		}

		// THE CELLS. A pair that never occurred writes nothing — an empty cell is what "this never
		// happened" looks like, and a zero would state a measurement nobody made.
		auto writeFigures = [&](int firstCol, const std::vector<ibValue>& figures) {
			for (size_t m = 0; m < figures.size() && static_cast<int>(m) < std::max(measures, 1); ++m) {
				const int col = firstCol + static_cast<int>(m);
				if (col >= totalCols)
					break;
				const wxString text = figures[m].GetString();
				row->SetCellValue(0, col, text);
				if (figures[m].GetType() == ibValueTypes::TYPE_NUMBER)
					row->SetCellAlignment(0, col, wxALIGN_RIGHT, wxALIGN_CENTER);
				if (!figures[m].IsEmpty()) {
					const wxString name = wxString::Format(wxT("Cell_%d"), col);
					row->SetParameter(name, figures[m]);
					row->SetCellDetailsParameter(0, col, name);
				}
				if (static_cast<size_t>(col) < m_widest.size())
					m_widest[static_cast<size_t>(col)] = std::max(m_widest[static_cast<size_t>(col)], text.length());
			}
		};
		// BY SLOT, because a slot is what a column IS — a key's figures, or an upper heading's own.
		for (size_t s = 0; s < slots.size(); ++s) {
			const int at = dimWidth + static_cast<int>(s) * perKey;
			if (!slots[s].m_subtotal) {
				const auto cell = source.m_cells.find(slots[s].m_at);
				if (cell != source.m_cells.end())
					writeFigures(at, cell->second);
				continue;
			}
			for (const std::pair<CrossKey, std::vector<ibValue>>& kept : source.m_subtotals)
				if (kept.first == slots[s].m_key) { writeFigures(at, kept.second); break; }
		}
		if (measures > 0)
			writeFigures(dimWidth + keys * perKey, source.m_measures);

		// A HEADING IS TINTED BY ITS LEVEL AND BOLD; A RECORD IS NEITHER — same rule the streaming
		// layout follows (OnRow), so a table and a grouping dress their lines alike.
		const wxColour fill = source.m_detail ? kDetailFill : ibGroupFillForLevel(source.m_level);
		wxFont font = s_defaultSpreadsheetFont;
		font.SetWeight(wxFontWeight::wxFONTWEIGHT_BOLD);
		for (int col = 0; col < totalCols; ++col) {
			row->SetCellBackgroundColour(0, col, fill);
			if (!source.m_detail)
				row->SetCellFont(0, col, font);
		}
		m_document->PutArea(row, static_cast<unsigned int>(std::max(0, source.m_level)));
		++m_rowsWritten;
	}

	// ---- the bottom line: what each column adds up to, and the grand total in the corner --------
	//
	// ⭐ ONE ROW, NOT TWO. The column totals and the grand total are the same sentence — "and
	// altogether" — read across and then closed at the right. A separate grand-total line under
	// them would repeat the corner figure a row lower with nothing new to say.
	if (!m_columnTotalCells.empty() || m_hasGrandTotal) {
		wxObjectDataPtr<ibBackendSpreadsheetObject> totals(new ibBackendSpreadsheetObject());
		totals->SetCellValue(0, 0, wxT("Total"));

		int wrote = 0;   // …and how many cells this line actually filled — see the journal below
		auto writeAt = [&](int firstCol, const std::vector<ibValue>& figures) {
			for (size_t m = 0; m < figures.size() && static_cast<int>(m) < std::max(measures, 1); ++m) {
				++wrote;
				const int col = firstCol + static_cast<int>(m);
				if (col >= totalCols)
					break;
				const wxString text = figures[m].GetString();
				totals->SetCellValue(0, col, text);
				if (figures[m].GetType() == ibValueTypes::TYPE_NUMBER)
					totals->SetCellAlignment(0, col, wxALIGN_RIGHT, wxALIGN_CENTER);
				if (static_cast<size_t>(col) < m_widest.size())
					m_widest[static_cast<size_t>(col)] = std::max(m_widest[static_cast<size_t>(col)], text.length());
			}
		};
		for (size_t s = 0; s < slots.size(); ++s) {
			const int at = dimWidth + static_cast<int>(s) * perKey;
			if (!slots[s].m_subtotal) {
				const auto cell = m_columnTotalCells.find(slots[s].m_at);
				if (cell != m_columnTotalCells.end())
					writeAt(at, cell->second);
				continue;
			}
			for (const std::pair<CrossKey, std::vector<ibValue>>& kept : m_columnTotalSubtotals)
				if (kept.first == slots[s].m_key) { writeAt(at, kept.second); break; }
		}
		if (m_hasGrandTotal && measures > 0)
			writeAt(dimWidth + keys * perKey, m_crossGrandTotal);

		wxFont font = s_defaultSpreadsheetFont;
		font.SetWeight(wxFontWeight::wxFONTWEIGHT_BOLD);
		for (int col = 0; col < totalCols; ++col) {
			totals->SetCellBackgroundColour(0, col, kHeaderFill);
			totals->SetCellFont(0, col, font);
		}
		m_document->PutArea(totals, 0);
		++m_rowsWritten;
		// ⚠ WHAT THE BOTTOM LINE ACTUALLY FILLED. It is not enough to know that the second fold
		// returned figures — they still have to LAND, and a figure whose key does not match a slot
		// lands nowhere and says nothing about it. Journalled because that gap is invisible on screen:
		// an empty totals row looks exactly like a report that had no totals to show.
		ibJournalInfo(wxT("composer.cross"), wxT("totals row: %d cell(s) from %u key total(s) + %u subtotal(s) over %d slot(s)"),
			wrote, static_cast<unsigned>(m_columnTotalCells.size()),
			static_cast<unsigned>(m_columnTotalSubtotals.size()), static_cast<int>(slots.size()));
	}
	m_hasGrandTotal = false;
	m_crossGrandTotal.clear();

	// ⚠ WHAT THE TABLE ACTUALLY CAME OUT AS, said out loud — the two numbers a complaint is always
	// about ("nothing above the sub-groups", "the records are missing") read straight off this line.
	ibJournalInfo(wxT("composer.cross"), wxT("%d line(s) x %d column key(s), %d measure(s); %d column total(s), %d detail row(s)"),
		static_cast<int>(m_crossRows.size()), keys, measures,
		static_cast<int>(m_columnTotalCells.size()), m_crossDetailRows);
	m_crossDetailRows = 0;
	m_columnTotalCells.clear();
	m_columnTotalSubtotals.clear();   // the table is printed; whatever comes next is its own output

	// ⚠ EVERY COLUMN OF THE SHEET, not of this output. A narrower output would otherwise leave the
	// columns past its own edge at whatever the previous one set — and, worse, re-set the shared
	// ones to its own shorter text (see WidenTo).
	for (size_t col = 0; col < m_widest.size(); ++col) {
		const int width = static_cast<int>(m_widest[col]) * kPixelsPerChar + kCellPadding;
		m_document->SetColSize(static_cast<int>(col), std::min(std::max(width, kMinColWidth), kMaxColWidth));
	}
	m_document->EnableEditing(false);
}

void ibSpreadsheetComposeDriver::OnOutputEnd(bool /*totals*/)
{
	if (m_document == nullptr)
		return;

	if (m_cross) {
		WriteCrossTable();
		return;
	}

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
	// ⚠ EVERY COLUMN OF THE SHEET, not of this output. A narrower output would otherwise leave the
	// columns past its own edge at whatever the previous one set — and, worse, re-set the shared
	// ones to its own shorter text (see WidenTo).
	for (size_t col = 0; col < m_widest.size(); ++col) {
		const int width = static_cast<int>(m_widest[col]) * kPixelsPerChar + kCellPadding;
		m_document->SetColSize(static_cast<int>(col), std::min(std::max(width, kMinColWidth), kMaxColWidth));
	}

	// 🔒 A REPORT IS READ, NOT EDITED (Max, 2026-08-19: "by default the table comes up read-only, to
	// guarantee it stays as composed and that drill-down works"). Typing into a composed cell would
	// leave a document whose numbers no longer follow from the query that produced them, and whose
	// cell no longer matches the value bound behind it.
	m_document->EnableEditing(false);
}
