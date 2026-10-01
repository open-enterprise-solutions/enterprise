#include "gridEditor.h"
#include "frontend/win/ctrls/grid/gridextprivate.h"

#include <wx/wupdlock.h>   // wxWindowUpdateLocker — the Freeze/Thaw pair taken as a guard

namespace {

// ⭐⭐ APPLY A CELL'S SPAN, UNLESS AN EARLIER MERGE ALREADY COVERS THAT CELL.
//
// A cell a merge covers must not be given a span of its own, or the grid stops on
// `ibGrid::SetCellSize: setting cell size that is already part of another cell` — an assert, i.e. an
// int 3, at LOAD time, long after whatever produced the document has finished.
//
// Since 2026-08-28 the description states coverage itself — `SetCellSize` stamps the negative offset
// back to the main cell on everything under it — so most covered cells now say so and are skipped on
// the first line below. The rest of this still stands: a document written before that says nothing,
// and the ORDER a rectangle is copied in means a covered cell can be reached before its owner.
//
// 🛑 AND SUCH A DOCUMENT IS THE ORDINARY CASE, not a malformed one: `PutArea` copies an area as a
// RECTANGLE — GetOrCreateCell for every row × col — so every cell under a merge is materialised at
// 1×1 by the copy itself. A report heading never hit it because its area is one column wide; a
// cross-table's header is the first thing in the tree that merges INSIDE a wide area (dumps
// 2026-08-25 22:26 and 22:44, both on cell 2,2).
//
// Asked HERE, at the point of failure and once for all four load paths: the loader is what knows the
// grid's state, and teaching every producer which of its cells not to touch would be the same rule
// spelled out in as many places as there are producers.
void GridApplyCellSpan(ibGrid& grid, int row, int col, int rowSpan, int colSpan)
{
	if (rowSpan < 0 || colSpan < 0)
		return;   // the description's own "this one is covered", where a writer states it
	int haveRows = 1, haveCols = 1;
	grid.GetCellSize(row, col, &haveRows, &haveCols);
	if (haveRows >= 1 && haveCols >= 1)
		grid.SetCellSize(row, col, rowSpan, colSpan, false);
}

} // namespace

int ibSpreadsheetRowHeight(const ibBackendSpreadsheetObject& doc, int row, wxDC& dc, const wxString& langCode)
{
	const ibSpreadsheetDescription& desc = doc.GetSpreadsheetDesc();
	if (desc.HasRowSize(row))
		return desc.GetRowSize(row);

	// ⚠ MEASURED IN THE SCREEN'S PIXELS, not converted to 96 per inch: the grid draws a row height as that
	// many of its own pixels, and the printout's unit is a screen pixel too (CalculateScale) — so the text
	// has to be measured in the pixels it will be drawn in, and `dc` is a screen's.

	// ⭐ THE DEFAULT ROW IS ONE LINE OF THE DEFAULT FONT, and the padding is whatever that leaves — so a line
	// in the default font never makes a row grow, whatever the platform's font metrics come to. It may come
	// out below nothing (a scaled display, where that line is taller than the row): the row still does not
	// grow for it, and a larger line grows it by what it adds.
	dc.SetFont(s_defaultSpreadsheetFont);
	const int padding = s_defaultRowHeight - dc.GetCharHeight();
	const double defaultPoints = s_defaultSpreadsheetFont.GetFractionalPointSize();

	// ⚠ BROKEN AGAINST THE WIDTH THE DRAWING BREAKS IT AGAINST, or the two count lines apart and the row
	// comes out a line short. Of a column's width the cell keeps the grid line, the renderer's pixel on
	// each side (gridextctrl.cpp) and the text margin at each side (GRID_TEXT_MARGIN) — five in all. The
	// printout leaves a shade more room than that, so paper never breaks a line the screen did not: a row
	// measured here is enough for both.
	const int chromeWrapping = 5;

	int height = s_defaultRowHeight;
	for (int col = 0; col < desc.GetNumberCols(); col++) {

		const ibSpreadsheetCellDescription* cell = desc.GetCell(row, col);
		if (cell == nullptr || cell->IsEmptyValue())
			continue;

		// A cell under a merge says nothing of its own, and a block across several rows shares its height
		// among them — neither is this row's to answer for. Nor is text turned on its side, which runs
		// along the height instead of stacking in it.
		int spanRows = 1, spanCols = 1;
		if (cell->GetSize(&spanRows, &spanCols) < 0 || spanRows > 1 || cell->m_textOrient == wxVERTICAL)
			continue;

		const bool wrap = cell->m_fitMode == ibSpreadsheetCellDescription::ibFitMode::Mode_Wrap;
		const wxFont font = cell->m_font.IsOk() ? cell->m_font : s_defaultSpreadsheetFont;

		// The quick answer, which is most cells of most sheets: one line in a font no larger than the
		// default fits the default row. Nothing is measured for it.
		const wxString text = doc.ComputeStringValueFromParameters(cell->m_value, cell->m_fillSetType, langCode);
		if (text.IsEmpty())
			continue;
		if (!wrap && text.Find(wxT('\n')) == wxNOT_FOUND && font.GetFractionalPointSize() <= defaultPoints)
			continue;

		dc.SetFont(font);

		wxArrayString lines;
		ibGrid::ParseLines(text, lines);

		if (wrap) {
			int width = 0;   // the whole span
			for (int c = col; c < col + wxMax(spanCols, 1); c++)
				width += desc.GetColSize(c);

			wxArrayString wrapped;
			for (const wxString& one : lines)
				ibGrid::WrapTextLine(dc, one, width - chromeWrapping, wrapped);
			lines = wrapped;
		}

		const int need = static_cast<int>(lines.size()) * dc.GetCharHeight() + padding;
		height = wxMax(height, need);
	}

	return height;
}

int ibSpreadsheetColWidth(const ibBackendSpreadsheetObject& doc, int col, wxDC& dc, const wxString& langCode)
{
	const ibSpreadsheetDescription& desc = doc.GetSpreadsheetDesc();

	// What the cell keeps around the text when the line is drawn AS IT STANDS: the grid line, the renderer's
	// pixel on each side and the one margin the text starts from (gridextctrl.cpp, DrawTextRectangle). A
	// line being BROKEN has a margin at each side — see chromeWrapping above.
	const int chrome = 4;

	int width = 0;
	for (int row = 0; row < desc.GetNumberRows(); row++) {

		const ibSpreadsheetCellDescription* cell = desc.GetCell(row, col);
		if (cell == nullptr || cell->IsEmptyValue())
			continue;

		int spanRows = 1, spanCols = 1;
		if (cell->GetSize(&spanRows, &spanCols) < 0 || spanCols > 1 || cell->m_textOrient == wxVERTICAL)
			continue;

		// A cell that wraps says nothing about the width: it is the width that decides how it wraps.
		if (cell->m_fitMode == ibSpreadsheetCellDescription::ibFitMode::Mode_Wrap)
			continue;

		const wxString text = doc.ComputeStringValueFromParameters(cell->m_value, cell->m_fillSetType, langCode);
		if (text.IsEmpty())
			continue;

		dc.SetFont(cell->m_font.IsOk() ? cell->m_font : s_defaultSpreadsheetFont);

		wxArrayString lines;
		ibGrid::ParseLines(text, lines);
		for (const wxString& one : lines) {
			wxCoord lineWidth = 0, lineHeight = 0;
			dc.GetTextExtent(one, &lineWidth, &lineHeight);
			width = wxMax(width, lineWidth + chrome);
		}
	}

	// An empty column keeps the width it has: fitting it to nothing would collapse it to a hairline.
	return width > 0 ? wxMax(width, WXGRID_MIN_COL_WIDTH) : desc.GetColSize(col);
}

bool ibGridEditor::AssociateDocument(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc)
{
	if (m_spreadsheetObject != doc) {

		if (m_spreadsheetObject != nullptr)
			m_spreadsheetObject->RemoveNotifier(m_notifier);

		m_spreadsheetObject = doc;
		m_notifier = m_spreadsheetObject->AddNotifier<ibGenericSpreadsheetNotifier>(this);
	}

	return true;
}

bool ibGridEditor::GetActiveDocument(wxObjectDataPtr<ibBackendSpreadsheetObject>& doc) const
{
	doc = m_spreadsheetObject;
	return true;
}

void ibGridEditor::FitAutoRowHeights(int fromRow, int toRow)
{
	if (m_spreadsheetObject == nullptr)
		return;

	const ibSpreadsheetDescription& desc = m_spreadsheetObject->GetSpreadsheetDesc();
	const int lastRow = wxMin(toRow < 0 ? desc.GetNumberRows() - 1 : toRow, ibGrid::GetNumberRows() - 1);
	if (lastRow < fromRow)
		return;

	wxClientDC dc(GetGridWindow());
	const wxString langCode = m_spreadsheetObject->GetLangCode();

	// One refresh for the lot, not one per row; and while this runs, a row size is not a height somebody
	// chose — the grid announces every one of them and OnGridRowSize would write it down (gridEditor.h).
	//
	// ⚠ AND THE WINDOW IS HELD STILL BESIDES. The batch spares the grid's own refreshes, but every size
	// change still moves the rows below it, and the window repainted between them — a whole sheet fitted at
	// once read as a flicker (Max, 2026-09-22). Freeze/Thaw as a guard, so an exception cannot leave the
	// window frozen.
	wxWindowUpdateLocker freeze(this);

	m_quietSizing = true;
	ibGrid::BeginBatch();
	for (int row = wxMax(0, fromRow); row <= lastRow; row++) {
		// A height of its own stays exactly as it was set — a blank's band is a fixed piece of paper.
		if (desc.HasRowSize(row))
			continue;
		ibGrid::SetRowSize(row, ibSpreadsheetRowHeight(*m_spreadsheetObject, row, dc, langCode), 1.0f, false);
	}
	ibGrid::EndBatch();
	m_quietSizing = false;
}

void ibGridEditor::RequestAutoRowHeights(int fromRow, int toRow)
{
	if (fromRow < 0 || toRow < fromRow)
		return;

	m_autoHeightFrom = m_autoHeightFrom < 0 ? fromRow : wxMin(m_autoHeightFrom, fromRow);
	m_autoHeightTo = wxMax(m_autoHeightTo, toRow);
}

#pragma region file

bool ibGridEditor::LoadDocument(const ibSpreadsheetDescription& spreadsheetDesc)
{
	if (!LoadSpreadsheet(spreadsheetDesc))
		return false;

	wxObjectDataPtr<ibBackendSpreadsheetObject> doc(
		new ibBackendSpreadsheetObject(spreadsheetDesc));

	const bool associated = AssociateDocument(doc);
	FitAutoRowHeights();   // against the document just associated — the one the rows are asked of
	return associated;
}

bool ibGridEditor::LoadDocument(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc)
{
	const ibSpreadsheetDescription& spreadsheetDesc = doc->GetSpreadsheetDesc();

	if (!spreadsheetDesc.IsEmptySpreadsheet())
	{
		ibGrid::SetTable(
			new ibGridEditorStringTable(spreadsheetDesc.GetNumberRows(), spreadsheetDesc.GetNumberCols()), true);

		ibGrid::SetEvtHandlerEnabled(false);

		for (int row = 0; row < spreadsheetDesc.GetNumberRows(); row++) {

			for (int col = 0; col < spreadsheetDesc.GetNumberCols(); col++) {

				const ibSpreadsheetCellDescription* cell = spreadsheetDesc.GetCell(row, col);
				if (cell == nullptr)
					continue;

				ibGridCellAttrPtr attr = GetOrCreateCellAttrPtr(row, col);
				attr->SetAlignment(cell->m_alignHorz, cell->m_alignVert);

				GridApplyCellSpan(*this, row, col, cell->m_row_size, cell->m_col_size);

				attr->SetTextOrient(cell->m_textOrient);
				attr->SetFont(cell->m_font);
				attr->SetBackgroundColour(cell->m_backgroundColour);
				attr->SetTextColour(cell->m_textColour);

				attr->SetBorderLeft(cell->m_borderAt[0].m_style, cell->m_borderAt[0].m_colour, cell->m_borderAt[0].m_width);
				attr->SetBorderRight(cell->m_borderAt[1].m_style, cell->m_borderAt[1].m_colour, cell->m_borderAt[1].m_width);
				attr->SetBorderTop(cell->m_borderAt[2].m_style, cell->m_borderAt[2].m_colour, cell->m_borderAt[2].m_width);
				attr->SetBorderBottom(cell->m_borderAt[3].m_style, cell->m_borderAt[3].m_colour, cell->m_borderAt[3].m_width);

				attr->SetFitMode(ibToGridFitMode(cell->m_fitMode));
				attr->SetReadOnly(cell->m_isReadOnly);

				wxSharedPtr<wxString> ptr = wxSharedPtr<wxString>(new wxString(doc->ComputeStringValueFromParameters(cell->m_value, cell->m_fillSetType)));
				m_table->SetValueAsCustom(row, col, s_strTypeTextOrString, ptr.get());
			}
		}

		m_rowAreaAt.Clear();

		for (int idx = 0; idx < spreadsheetDesc.GetAreaNumberRows(); idx++) {

			const ibSpreadsheetAreaDescription* area = spreadsheetDesc.GetRowAreaByIdx(idx);

			if (area == nullptr)
				continue;

			//adding a new section
			ibGridCellArea entry;

			entry.m_start = area->m_start;
			entry.m_end = area->m_end;
			entry.m_areaLabel = area->m_label;

			m_rowAreaAt.push_back(entry);
		}

		m_colAreaAt.Clear();

		for (int idx = 0; idx < spreadsheetDesc.GetAreaNumberCols(); idx++) {

			const ibSpreadsheetAreaDescription* area = spreadsheetDesc.GetColAreaByIdx(idx);

			if (area == nullptr)
				continue;

			//adding a new section
			ibGridCellArea entry;

			entry.m_start = area->m_start;
			entry.m_end = area->m_end;
			entry.m_areaLabel = area->m_label;

			m_colAreaAt.push_back(entry);
		}

		// Outline groups from desc (independent of label areas).
		m_rowGroupAt.clear();
		for (int idx = 0; idx < spreadsheetDesc.GetGroupNumberRows(); idx++) {
			const ibSpreadsheetGroupDescription* g = spreadsheetDesc.GetRowGroupByIdx(idx);
			if (g == nullptr) continue;
			AddRowGroup((int)g->m_start, (int)g->m_end, (int)g->m_level, g->m_collapsed);
		}
		// The levels are in; the OUTLINE is what they mean — see ibGrid::NormalizeRowGroups.
		NormalizeRowGroups();
		m_colGroupAt.clear();
		for (int idx = 0; idx < spreadsheetDesc.GetGroupNumberCols(); idx++) {
			const ibSpreadsheetGroupDescription* g = spreadsheetDesc.GetColGroupByIdx(idx);
			if (g == nullptr) continue;
			AddColGroup((int)g->m_start, (int)g->m_end, (int)g->m_level, g->m_collapsed, g->m_head);
		}
		// …and the COLUMNS mean their levels the same way. This line was missing, so a column outline
		// only ever worked when its producer handed it exact ranges — see ibGrid::NormalizeColGroups.
		NormalizeColGroups();

		m_rowBrakeAt.Clear();

		for (int idx = 0; idx < spreadsheetDesc.GetBrakeNumberRows(); idx++)
			m_rowBrakeAt.push_back(spreadsheetDesc.GetRowBrakeByIdx(idx));

		m_colBrakeAt.Clear();

		for (int idx = 0; idx < spreadsheetDesc.GetBrakeNumberCols(); idx++)
			m_colBrakeAt.push_back(spreadsheetDesc.GetColBrakeByIdx(idx));

		for (int idx = 0; idx < spreadsheetDesc.GetSizeNumberRows(); idx++) {
			const ibSpreadsheetRowSizeDescription* row_size = spreadsheetDesc.GetRowSizeByIdx(idx);
			if (row_size == nullptr)
				continue;

			if ((int)row_size->m_row >= ibGrid::GetNumberRows())
				ibGrid::AppendCols((int)row_size->m_row - ibGrid::GetNumberRows() + 1);

			ibGrid::SetRowSize(row_size->m_row, row_size->m_height, 1.0f, false);
		}

		for (int idx = 0; idx < spreadsheetDesc.GetSizeNumberCols(); idx++) {
			const ibSpreadsheetColSizeDescription* col_size = spreadsheetDesc.GetColSizeByIdx(idx);
			if (col_size == nullptr)
				continue;

			if ((int)col_size->m_col >= ibGrid::GetNumberCols())
				ibGrid::AppendCols((int)col_size->m_col - ibGrid::GetNumberCols() + 1);

			ibGrid::SetColSize(col_size->m_col, col_size->m_width, 1.0f, false);
		}

		FreezeTo(spreadsheetDesc.GetRowFreeze(), spreadsheetDesc.GetColFreeze());
		EnableEditing(doc->IsEditable());

		ibGrid::SetEvtHandlerEnabled(true);
	}

	// ⭐ THE SHEET FILLS THE WINDOW — HERE, not at every callsite. A loaded document holds exactly the
	// rows and columns it was written with (a composed report may hold two), and the space beyond them
	// is empty SHEET rather than "outside the document" — without this the report ends in a gridless
	// void that reads as a rendering failure. Loading IS the moment that changes, so the trigger
	// belongs inside it: a caller cannot forget what it never has to remember (Max, 2026-08-19).
	FillVisibleArea();

	const bool associated = AssociateDocument(doc);
	FitAutoRowHeights();   // the rows without a height of their own, sized to their text before the first paint
	return associated;
}

bool ibGridEditor::SaveDocument(ibSpreadsheetDescription& spreadsheetDesc) const
{
	return SaveSpreadsheet(spreadsheetDesc);
}

bool ibGridEditor::SaveDocument(wxObjectDataPtr<ibBackendSpreadsheetObject>& doc) const
{
	return SaveSpreadsheet(doc->GetSpreadsheetDesc());
}

void ibGridEditor::PutDocument(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc, unsigned int groupLevel)
{
	ibGrid::SetEvtHandlerEnabled(false);

	if (m_table == nullptr) {
		ibGrid::SetTable(
			new ibGridEditorStringTable, true);
	}

	const int maxRowBrake = GetMaxRowBrake();
	const int maxColBrake = GetMaxColBrake();

	// ⭐ THE AREA IS READ IN THE LANGUAGE OF THE DOCUMENT IT LANDS IN — the language the document's own
	// PutArea renders the same cells in, through the same door, so the grid and the stored cell say
	// one thing.
	const wxString langCode = m_spreadsheetObject != nullptr ? m_spreadsheetObject->GetLangCode() : wxString();

	ibGrid::AppendRows(doc->GetNumberRows());
	if (doc->GetNumberCols() > m_table->GetNumberCols())
		ibGrid::AppendCols(doc->GetNumberCols() - m_table->GetNumberCols());

	m_numRows = m_table->GetNumberRows();
	m_numCols = m_table->GetNumberCols();

	for (int row = 0; row < doc->GetNumberRows(); row++) {

		for (int col = 0; col < doc->GetNumberCols(); col++) {

			const ibSpreadsheetCellDescription* cell = doc->GetSpreadsheetDesc().GetCell(row, col);
			if (cell == nullptr)
				continue;

			ibGridCellAttrPtr attr = GetOrCreateCellAttrPtr(maxRowBrake + row, col);
			attr->SetAlignment(cell->m_alignHorz, cell->m_alignVert);

			GridApplyCellSpan(*this, maxRowBrake + row, col, cell->m_row_size, cell->m_col_size);

			attr->SetTextOrient(cell->m_textOrient);
			attr->SetFont(cell->m_font);
			attr->SetBackgroundColour(cell->m_backgroundColour);
			attr->SetTextColour(cell->m_textColour);

			attr->SetBorderLeft(cell->m_borderAt[0].m_style, cell->m_borderAt[0].m_colour, cell->m_borderAt[0].m_width);
			attr->SetBorderRight(cell->m_borderAt[1].m_style, cell->m_borderAt[1].m_colour, cell->m_borderAt[1].m_width);
			attr->SetBorderTop(cell->m_borderAt[2].m_style, cell->m_borderAt[2].m_colour, cell->m_borderAt[2].m_width);
			attr->SetBorderBottom(cell->m_borderAt[3].m_style, cell->m_borderAt[3].m_colour, cell->m_borderAt[3].m_width);

			attr->SetFitMode(ibToGridFitMode(cell->m_fitMode));
			attr->SetReadOnly(cell->m_isReadOnly);

			wxSharedPtr<wxString> ptr = wxSharedPtr<wxString>(new wxString(doc->ComputeStringValueFromParameters(cell->m_value, cell->m_fillSetType, langCode)));
			m_table->SetValueAsCustom(maxRowBrake + row, col, s_strTypeTextOrString, ptr.get());
		}
	}

	// A height only where the area's row has one; the rest have automatic height, fitted once the document
	// has taken the area in (it is told about it before it stores it — see RequestAutoRowHeights).
	for (int row = 0; row < doc->GetNumberRows(); row++)
		if (doc->GetSpreadsheetDesc().HasRowSize(row))
			SetRowSize(maxRowBrake + row, doc->GetRowSize(row));
	RequestAutoRowHeights(maxRowBrake, maxRowBrake + doc->GetNumberRows() - 1);

	for (int col = 0; col < doc->GetNumberCols(); col++)
		SetColSize(col, doc->GetColSize(col));

	SetRowBrake(maxRowBrake + doc->GetNumberRows());

	if (maxColBrake < doc->GetNumberCols())
		SetColBrake(maxColBrake + doc->GetNumberCols());

	if (groupLevel > 0 && doc->GetNumberRows() > 0) {
		AddRowGroup(maxRowBrake, maxRowBrake + doc->GetNumberRows() - 1, (int)groupLevel);
		if (m_rowOutlineWin) m_rowOutlineWin->Refresh();
		CalcDimensions();
	}

	ibGrid::EnableEditing(doc->IsEditable());
	ibGrid::SetEvtHandlerEnabled(true);
}

void ibGridEditor::JoinDocument(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc, unsigned int groupLevel)
{
	ibGrid::SetEvtHandlerEnabled(false);

	if (m_table == nullptr) {
		ibGrid::SetTable(
			new ibGridEditorStringTable, true);
	}

	const int maxRowBrake = GetMaxRowBrake();
	const int maxColBrake = GetMaxColBrake();

	// the language of the document the area lands in — see PutDocument
	const wxString langCode = m_spreadsheetObject != nullptr ? m_spreadsheetObject->GetLangCode() : wxString();

	if (doc->GetNumberRows() > m_table->GetNumberRows())
		ibGrid::AppendRows(doc->GetNumberRows() - m_table->GetNumberRows());
	ibGrid::AppendCols(doc->GetNumberCols());

	m_numRows = m_table->GetNumberRows();
	m_numCols = m_table->GetNumberCols();

	for (int row = 0; row < doc->GetNumberRows(); row++) {

		for (int col = 0; col < doc->GetNumberCols(); col++) {

			const ibSpreadsheetCellDescription* cell = doc->GetSpreadsheetDesc().GetCell(row, col);
			if (cell == nullptr)
				continue;

			ibGridCellAttrPtr attr = GetOrCreateCellAttrPtr(row, maxColBrake + col);
			attr->SetAlignment(cell->m_alignHorz, cell->m_alignVert);

			GridApplyCellSpan(*this, row, maxColBrake + col, cell->m_row_size, cell->m_col_size);

			attr->SetTextOrient(cell->m_textOrient);
			attr->SetFont(cell->m_font);
			attr->SetBackgroundColour(cell->m_backgroundColour);
			attr->SetTextColour(cell->m_textColour);

			attr->SetBorderLeft(cell->m_borderAt[0].m_style, cell->m_borderAt[0].m_colour, cell->m_borderAt[0].m_width);
			attr->SetBorderRight(cell->m_borderAt[1].m_style, cell->m_borderAt[1].m_colour, cell->m_borderAt[1].m_width);
			attr->SetBorderTop(cell->m_borderAt[2].m_style, cell->m_borderAt[2].m_colour, cell->m_borderAt[2].m_width);
			attr->SetBorderBottom(cell->m_borderAt[3].m_style, cell->m_borderAt[3].m_colour, cell->m_borderAt[3].m_width);

			attr->SetFitMode(ibToGridFitMode(cell->m_fitMode));
			attr->SetReadOnly(cell->m_isReadOnly);

			wxSharedPtr<wxString> ptr = wxSharedPtr<wxString>(new wxString(doc->ComputeStringValueFromParameters(cell->m_value, cell->m_fillSetType, langCode)));
			m_table->SetValueAsCustom(row, maxColBrake + col, s_strTypeTextOrString, ptr.get());
		}
	}

	// a height only where the area's row has one, the rest fitted afterwards — see PutDocument
	for (int row = 0; row < doc->GetNumberRows(); row++)
		if (doc->GetSpreadsheetDesc().HasRowSize(row))
			SetRowSize(row, doc->GetRowSize(row));
	RequestAutoRowHeights(0, doc->GetNumberRows() - 1);

	for (int col = 0; col < doc->GetNumberCols(); col++)
		SetColSize(maxColBrake + col, doc->GetColSize(col));

	if (maxRowBrake < doc->GetNumberRows())
		SetRowBrake(maxRowBrake + doc->GetNumberRows());

	SetColBrake(maxColBrake + doc->GetNumberCols());

	if (groupLevel > 0 && doc->GetNumberCols() > 0) {
		AddColGroup(maxColBrake, maxColBrake + doc->GetNumberCols() - 1, (int)groupLevel);
		if (m_colOutlineWin) m_colOutlineWin->Refresh();
		CalcDimensions();
	}

	ibGrid::SetEvtHandlerEnabled(true);
}

void ibGridEditor::AppendRowOutlineGroup(unsigned int start, unsigned int end, unsigned int level)
{
	// ⚠ NOT normalised here. Shaping the outline needs the WHOLE sequence — whether a row heads
	// anything is answered by the rows that come after it — and this arrives one group at a time.
	// The document's own load path (LoadDocument) re-reads every group and shapes them together,
	// which is what the report goes through after it is composed.
	AddRowGroup((int)start, (int)end, (int)level);
	if (m_rowOutlineWin) m_rowOutlineWin->Refresh();
	CalcDimensions();
}

void ibGridEditor::AppendColOutlineGroup(unsigned int start, unsigned int end, unsigned int level)
{
	AddColGroup((int)start, (int)end, (int)level);
	if (m_colOutlineWin) m_colOutlineWin->Refresh();
	CalcDimensions();
}

bool ibGridEditor::LoadSpreadsheet(const ibSpreadsheetDescription& spreadsheetDesc)
{
	if (!spreadsheetDesc.IsEmptySpreadsheet())
	{
		ibGrid::SetTable(
			new ibGridEditorStringTable(spreadsheetDesc.GetNumberRows(), spreadsheetDesc.GetNumberCols()), true);

		ibGrid::SetEvtHandlerEnabled(false);

		for (int row = 0; row < spreadsheetDesc.GetNumberRows(); row++) {

			for (int col = 0; col < spreadsheetDesc.GetNumberCols(); col++) {

				const ibSpreadsheetCellDescription* cell = spreadsheetDesc.GetCell(row, col);
				if (cell == nullptr)
					continue;

				ibGridCellAttrPtr attr = GetOrCreateCellAttrPtr(row, col);
				attr->SetAlignment(cell->m_alignHorz, cell->m_alignVert);

				GridApplyCellSpan(*this, row, col, cell->m_row_size, cell->m_col_size);

				attr->SetTextOrient(cell->m_textOrient);
				attr->SetFont(cell->m_font);
				attr->SetBackgroundColour(cell->m_backgroundColour);
				attr->SetTextColour(cell->m_textColour);

				attr->SetBorderLeft(cell->m_borderAt[0].m_style, cell->m_borderAt[0].m_colour, cell->m_borderAt[0].m_width);
				attr->SetBorderRight(cell->m_borderAt[1].m_style, cell->m_borderAt[1].m_colour, cell->m_borderAt[1].m_width);
				attr->SetBorderTop(cell->m_borderAt[2].m_style, cell->m_borderAt[2].m_colour, cell->m_borderAt[2].m_width);
				attr->SetBorderBottom(cell->m_borderAt[3].m_style, cell->m_borderAt[3].m_colour, cell->m_borderAt[3].m_width);

				attr->SetFitMode(ibToGridFitMode(cell->m_fitMode));
				attr->SetReadOnly(cell->m_isReadOnly);

				if (cell->m_fillSetType == ibSpreadsheetFillType::ibSpreadsheetFillType_StrText) {
					wxSharedPtr<wxString> ptr = wxSharedPtr<wxString>(new wxString(cell->m_value));
					m_table->SetValueAsCustom(row, col, s_strTypeTextOrString, ptr.get());
				}
				else if (cell->m_fillSetType == ibSpreadsheetFillType::ibSpreadsheetFillType_StrTemplate) {
					wxSharedPtr<wxString> ptr = wxSharedPtr<wxString>(new wxString(cell->m_value));
					m_table->SetValueAsCustom(row, col, s_strTypeTemplate, ptr.get());
				}
				else if (cell->m_fillSetType == ibSpreadsheetFillType::ibSpreadsheetFillType_StrParameter) {
					wxSharedPtr<wxString> ptr = wxSharedPtr<wxString>(new wxString(cell->m_value));
					m_table->SetValueAsCustom(row, col, s_strTypeParameter, ptr.get());
				}
			}
		}

		m_rowAreaAt.Clear();

		for (int idx = 0; idx < spreadsheetDesc.GetAreaNumberRows(); idx++) {

			const ibSpreadsheetAreaDescription* area = spreadsheetDesc.GetRowAreaByIdx(idx);

			if (area == nullptr)
				continue;

			//adding a new section
			ibGridCellArea entry;

			entry.m_start = area->m_start;
			entry.m_end = area->m_end;
			entry.m_areaLabel = area->m_label;

			m_rowAreaAt.push_back(entry);
		}

		m_colAreaAt.Clear();

		for (int idx = 0; idx < spreadsheetDesc.GetAreaNumberCols(); idx++) {

			const ibSpreadsheetAreaDescription* area = spreadsheetDesc.GetColAreaByIdx(idx);

			if (area == nullptr)
				continue;

			//adding a new section
			ibGridCellArea entry;

			entry.m_start = area->m_start;
			entry.m_end = area->m_end;
			entry.m_areaLabel = area->m_label;

			m_colAreaAt.push_back(entry);
		}

		// Outline groups from desc (independent of label areas).
		m_rowGroupAt.clear();
		for (int idx = 0; idx < spreadsheetDesc.GetGroupNumberRows(); idx++) {
			const ibSpreadsheetGroupDescription* g = spreadsheetDesc.GetRowGroupByIdx(idx);
			if (g == nullptr) continue;
			AddRowGroup((int)g->m_start, (int)g->m_end, (int)g->m_level, g->m_collapsed);
		}
		// The levels are in; the OUTLINE is what they mean — see ibGrid::NormalizeRowGroups.
		NormalizeRowGroups();
		m_colGroupAt.clear();
		for (int idx = 0; idx < spreadsheetDesc.GetGroupNumberCols(); idx++) {
			const ibSpreadsheetGroupDescription* g = spreadsheetDesc.GetColGroupByIdx(idx);
			if (g == nullptr) continue;
			AddColGroup((int)g->m_start, (int)g->m_end, (int)g->m_level, g->m_collapsed, g->m_head);
		}
		// …and the COLUMNS mean their levels the same way. This line was missing, so a column outline
		// only ever worked when its producer handed it exact ranges — see ibGrid::NormalizeColGroups.
		NormalizeColGroups();

		m_rowBrakeAt.Clear();

		for (int idx = 0; idx < spreadsheetDesc.GetBrakeNumberRows(); idx++)
			m_rowBrakeAt.push_back(spreadsheetDesc.GetRowBrakeByIdx(idx));

		m_colBrakeAt.Clear();

		for (int idx = 0; idx < spreadsheetDesc.GetBrakeNumberCols(); idx++)
			m_colBrakeAt.push_back(spreadsheetDesc.GetColBrakeByIdx(idx));

		for (int idx = 0; idx < spreadsheetDesc.GetSizeNumberRows(); idx++) {
			const ibSpreadsheetRowSizeDescription* row_size = spreadsheetDesc.GetRowSizeByIdx(idx);
			if (row_size == nullptr)
				continue;

			if ((int)row_size->m_row >= ibGrid::GetNumberRows())
				ibGrid::AppendCols((int)row_size->m_row - ibGrid::GetNumberRows() + 1);

			ibGrid::SetRowSize((int)row_size->m_row, row_size->m_height, 1.0f, false);
		}

		for (int idx = 0; idx < spreadsheetDesc.GetSizeNumberCols(); idx++) {
			const ibSpreadsheetColSizeDescription* col_size = spreadsheetDesc.GetColSizeByIdx(idx);
			if (col_size == nullptr)
				continue;

			if ((int)col_size->m_col >= ibGrid::GetNumberCols())
				ibGrid::AppendCols((int)col_size->m_col - ibGrid::GetNumberCols() + 1);

			ibGrid::SetColSize(col_size->m_col, col_size->m_width, 1.0f, false);
		}

		FreezeTo(spreadsheetDesc.GetRowFreeze(), spreadsheetDesc.GetColFreeze());

		ibGrid::SetEvtHandlerEnabled(true);
	}

	return true;
}

bool ibGridEditor::SaveSpreadsheet(ibSpreadsheetDescription& spreadsheetDesc) const
{
	spreadsheetDesc = m_spreadsheetObject->GetSpreadsheetDesc();
	return true;
}

#pragma endregion