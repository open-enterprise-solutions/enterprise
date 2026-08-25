#include "gridEditor.h"
#include "frontend/win/ctrls/grid/gridextprivate.h"

namespace {

// ⭐⭐ APPLY A CELL'S SPAN, UNLESS AN EARLIER MERGE ALREADY COVERS THAT CELL.
//
// The description has no notion of "covered": `ibSpreadsheetDescription::SetCellSize` stamps the
// MAIN cell and nothing else, while the grid marks every cell a merge covers with a non-positive
// span. So a document carrying both a merge and the cells underneath it walks straight into
// `ibGrid::SetCellSize: setting cell size that is already part of another cell` — an assert, i.e. an
// int 3, at LOAD time, long after whatever produced the document has finished.
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

bool ibGridEditor::AssociatibDocument(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc)
{
	if (m_spreadsheetObject != doc) {

		if (m_spreadsheetObject != nullptr)
			m_spreadsheetObject->RemoveNotifier(m_notifier);

		m_spreadsheetObject = doc;
		m_notifier = m_spreadsheetObject->AddNotifier<ibGenericSpreadsheetNotifier>(this);
	}

	return true;
}

bool ibGridEditor::GetActivibDocument(wxObjectDataPtr<ibBackendSpreadsheetObject>& doc) const
{
	doc = m_spreadsheetObject;
	return true;
}

#pragma region file

bool ibGridEditor::LoadDocument(const ibSpreadsheetDescription& spreadsheetDesc)
{
	if (!LoadSpreadsheet(spreadsheetDesc))
		return false;

	wxObjectDataPtr<ibBackendSpreadsheetObject> doc(
		new ibBackendSpreadsheetObject(spreadsheetDesc));

	return AssociatibDocument(doc);
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

				attr->SetFitMode(cell->m_fitMode == ibSpreadsheetCellDescription::ibFitMode::Mode_Overflow ? ibGridFitMode::Overflow() : ibGridFitMode::Clip());
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

	return AssociatibDocument(doc);
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

			attr->SetFitMode(cell->m_fitMode == ibSpreadsheetCellDescription::ibFitMode::Mode_Overflow ? ibGridFitMode::Overflow() : ibGridFitMode::Clip());
			attr->SetReadOnly(cell->m_isReadOnly);

			wxSharedPtr<wxString> ptr = wxSharedPtr<wxString>(new wxString(doc->ComputeStringValueFromParameters(cell->m_value, cell->m_fillSetType)));
			m_table->SetValueAsCustom(maxRowBrake + row, col, s_strTypeTextOrString, ptr.get());
		}
	}

	for (int row = 0; row < doc->GetNumberRows(); row++)
		SetRowSize(maxRowBrake + row, doc->GetRowSize(row));

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

			attr->SetFitMode(cell->m_fitMode == ibSpreadsheetCellDescription::ibFitMode::Mode_Overflow ? ibGridFitMode::Overflow() : ibGridFitMode::Clip());
			attr->SetReadOnly(cell->m_isReadOnly);

			wxSharedPtr<wxString> ptr = wxSharedPtr<wxString>(new wxString(doc->ComputeStringValueFromParameters(cell->m_value, cell->m_fillSetType)));
			m_table->SetValueAsCustom(row, maxColBrake + col, s_strTypeTextOrString, ptr.get());
		}
	}

	for (int row = 0; row < doc->GetNumberRows(); row++)
		SetRowSize(row, doc->GetRowSize(row));

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

				attr->SetFitMode(cell->m_fitMode == ibSpreadsheetCellDescription::ibFitMode::Mode_Overflow ? ibGridFitMode::Overflow() : ibGridFitMode::Clip());
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