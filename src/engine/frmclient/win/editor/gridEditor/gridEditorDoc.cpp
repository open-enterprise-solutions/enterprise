#include "gridEditor.h"
#include "deskgui/univ/grid/gridextprivate.h"

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
// Asked HERE, at the point of failure and once for every load path: the loader is what knows the
// grid's state, and teaching every producer which of its cells not to touch would be the same rule
// spelled out in as many places as there are producers.
void GridApplyCellSpan(ibGrid& grid, int row, int col, int rowSpan, int colSpan)
{
	if (rowSpan < 0 || colSpan < 0)
		return;   // the description's own "this one is covered", where a writer states it
	// ⚠ THE ORDINARY CELL IS NOT ASKED, 1×1 onto a cell every loader has just made — a new table, rows and columns
	// appended — which holds no merge. Set, it was an event and the printing brakes moved, each brake an event with the
	// mouse's state read from the system: 13 % of a 100 000-line sheet's load (2026-11-16, load-million-rows.md § 12);
	// asked, the grid makes the cell's look from the description (ibGridEditorAttrProvider).
	if (rowSpan <= 1 && colSpan <= 1)
		return;
	int haveRows = 1, haveCols = 1;
	grid.GetCellSize(row, col, &haveRows, &haveCols);
	if (haveRows >= 1 && haveCols >= 1)
		grid.SetCellSize(row, col, rowSpan, colSpan, false);
	// The look asked for above is kept for this cell, made before the merge — and for an area being put, before the
	// document holds the cell (it is told first): dropped, so the cell is drawn from what it is now.
	grid.RefreshAttr(row, col);
}

} // namespace

int ibSpreadsheetRowHeight(const ibSpreadsheetObject& doc, int row, wxDC& dc, const wxString& langCode)
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
	//
	// ⚠ AND NO wxFont UNTIL A CELL IS MEASURED. The quick answer below is asked of the core's font; a wxFont is a
	// screen DC taken and given back to be made, and setting one on a DC compares face names through the system —
	// two fonts a row made and set for every row of a sheet whose rows all take the quick answer (2026-11-16,
	// load-million-rows.md § 12).
	const ibFont ordinaryFont = ibDefaultSpreadsheetFont();
	const double defaultPoints = ordinaryFont.m_pointSize;
	wxFont ordinary;
	int padding = 0;

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

		const bool wrap = desc.IsCellWrapped(row, col, [&] { return ibSpreadsheetTextWidth(doc, row, col, dc, langCode); });
		const double points = cell->m_font.IsOk() ? cell->m_font.m_pointSize : defaultPoints;

		// The quick answer, which is most cells of most sheets: one line in a font no larger than the
		// default fits the default row. Nothing is measured for it.
		const wxString text = doc.ComputeStringValueFromParameters(cell->m_value, cell->m_fillSetType, langCode);
		if (text.IsEmpty())
			continue;
		if (!wrap && text.Find(wxT('\n')) == wxNOT_FOUND && points <= defaultPoints)
			continue;

		if (!ordinary.IsOk()) {
			ordinary = typeConv::FontToWxFont(ordinaryFont);
			dc.SetFont(ordinary);
			padding = s_defaultRowHeight - dc.GetCharHeight();
		}
		dc.SetFont(cell->m_font.IsOk() ? typeConv::FontToWxFont(cell->m_font) : ordinary);

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

int ibSpreadsheetTextWidth(const ibSpreadsheetObject& doc, int row, int col, wxDC& dc, const wxString& langCode)
{
	const ibSpreadsheetCellDescription* cell = doc.GetSpreadsheetDesc().GetCell(row, col);
	if (cell == nullptr)
		return 0;
	const wxString text = doc.ComputeStringValueFromParameters(cell->m_value, cell->m_fillSetType, langCode);
	if (text.IsEmpty())
		return 0;

	// What the cell keeps around the text when the line is drawn AS IT STANDS: the grid line, the renderer's
	// pixel on each side and the one margin the text starts from (gridextctrl.cpp, DrawTextRectangle). A
	// line being BROKEN has a margin at each side — see chromeWrapping in ibSpreadsheetRowHeight.
	const int chrome = 4;
	dc.SetFont(typeConv::FontToWxFont(cell->m_font.IsOk() ? cell->m_font : ibDefaultSpreadsheetFont()));

	wxArrayString lines;
	ibGrid::ParseLines(text, lines);
	int width = 0;
	for (const wxString& one : lines) {
		wxCoord lineWidth = 0, lineHeight = 0;
		dc.GetTextExtent(one, &lineWidth, &lineHeight);
		width = wxMax(width, lineWidth + chrome);
	}
	return width;
}

int ibSpreadsheetColWidth(const ibSpreadsheetObject& doc, int col, wxDC& dc, const wxString& langCode)
{
	const ibSpreadsheetDescription& desc = doc.GetSpreadsheetDesc();

	int width = 0;
	for (int row = 0; row < desc.GetNumberRows(); row++) {

		const ibSpreadsheetCellDescription* cell = desc.GetCell(row, col);
		if (cell == nullptr || cell->IsEmptyValue())
			continue;

		int spanRows = 1, spanCols = 1;
		if (cell->GetSize(&spanRows, &spanCols) < 0 || spanCols > 1 || cell->m_textOrient == wxVERTICAL)
			continue;

		// A cell that wraps says nothing about the width: it is the width that decides how it wraps. One that breaks
		// only for want of room (Auto, IsCellWrapped) is asked like any other — given the width, it needs no break.
		if (cell->m_fitMode == ibSpreadsheetCellDescription::ibFitMode::Mode_Wrap)
			continue;

		width = wxMax(width, ibSpreadsheetTextWidth(doc, row, col, dc, langCode));
	}

	// An empty column keeps the width it has: fitting it to nothing would collapse it to a hairline.
	return width > 0 ? wxMax(width, WXGRID_MIN_COL_WIDTH) : desc.GetColSize(col);
}

#pragma region table

ibGridEditor::ibGridEditorTable::ibGridEditorTable(ibGridEditor* view, int numRows, int numCols)
	: m_view(view), m_numRows(numRows), m_numCols(numCols)
{
	// the look from the same cells the text comes from
	SetAttrProvider(new ibGridEditorAttrProvider(view));
}

const ibSpreadsheetCellDescription* ibGridEditor::ibGridEditorTable::GetCell(int row, int col) const
{
	return m_view->m_spreadsheetObject != nullptr ? m_view->m_spreadsheetObject->GetSpreadsheetDesc().GetCell(row, col) : nullptr;
}

ibSpreadsheetFillType ibGridEditor::ibGridEditorTable::GetFillType(int row, int col) const
{
	const ibSpreadsheetCellDescription* const cell = GetCell(row, col);
	return cell != nullptr ? cell->m_fillSetType : ibSpreadsheetFillType_StrText;
}

bool ibGridEditor::ibGridEditorTable::IsEmptyCell(int row, int col)
{
	// The document's answer — the same one a row's placement is worked out by (ibSpreadsheetDescription::IsEmptyCell).
	return m_view->m_spreadsheetObject == nullptr || m_view->m_spreadsheetObject->IsEmptyCell(row, col);
}

bool ibGridEditor::ibGridEditorTable::CanGetValueAs(int row, int col, const wxString& typeName)
{
	const ibSpreadsheetFillType type = GetFillType(row, col);

	if (type == ibSpreadsheetFillType_StrText)
		return typeName == s_strTypeTextOrString;
	else if (type == ibSpreadsheetFillType_StrTemplate)
		return typeName == s_strTypeTemplate;
	else if (type == ibSpreadsheetFillType_StrParameter)
		return typeName == s_strTypeParameter;

	return typeName == wxT("string");
}

bool ibGridEditor::ibGridEditorTable::CanSetValueAs(int row, int col, const wxString& typeName)
{
	return CanGetValueAs(row, col, typeName);
}

void ibGridEditor::ibGridEditorTable::GetValue(int row, int col, wxString& s)
{
	const ibSpreadsheetCellDescription* const cell = GetCell(row, col);
	if (cell == nullptr) {
		s.clear();
		return;
	}

	if (cell->m_fillSetType == ibSpreadsheetFillType_StrText || cell->m_fillSetType == ibSpreadsheetFillType_StrTemplate)
		s = ibBackendLocalization::GetTranslateGetRawLocText(cell->m_value);
	else if (cell->m_fillSetType == ibSpreadsheetFillType_StrParameter)
		s = cell->m_value;
}

void ibGridEditor::ibGridEditorTable::SetValue(int row, int col, const wxString& s)
{
	if (m_view->m_spreadsheetObject == nullptr)
		return;

	ibSpreadsheetDescription& desc = m_view->m_spreadsheetObject->GetSpreadsheetDesc();
	const ibSpreadsheetCellDescription* const cell = desc.GetCell(row, col);
	const wxString value = cell != nullptr ? wxString(cell->m_value) : wxString();
	if (s == value)
		return;

	// A caption and a template keep every language: what is typed is the user's language of it.
	wxString written = s;
	const ibSpreadsheetFillType type = cell != nullptr ? cell->m_fillSetType : ibSpreadsheetFillType_StrText;
	if (type == ibSpreadsheetFillType_StrText || type == ibSpreadsheetFillType_StrTemplate) {
		if (ibLocalization::IsLocalizationString(value) && !ibLocalization::IsLocalizationString(s)) {
			ibLocalizationEntryArray array;
			if (!ibLocalization::CreateLocalizationArray(value, array))
				return;
			ibLocalization::SetArrayTranslate(ibBackendLocalization::GetUserLanguage(), array, s);
			written = ibLocalization::GetRawLocText(array);
		}
		else if (!ibLocalization::IsLocalizationString(s)) {
			written = ibLocalization::CreateLocalizationRawLocText(ibBackendLocalization::GetUserLanguage(), s);
		}
	}

	desc.SetCellValue(row, col, written);
	OnGridTableNotify(row, col, written);   // the editor hands an edit on (OnGridTableModified)
}

void ibGridEditor::ibGridEditorTable::SetValueAsCustom(int row, int col, const wxString& typeName, void* value)
{
	if (value == nullptr || m_view->m_spreadsheetObject == nullptr)
		return;

	ibSpreadsheetFillType type = ibSpreadsheetFillType_StrText;
	if (stringUtils::IsSameAs(typeName, s_strTypeTemplate))
		type = ibSpreadsheetFillType_StrTemplate;
	else if (stringUtils::IsSameAs(typeName, s_strTypeParameter))
		type = ibSpreadsheetFillType_StrParameter;
	else if (!stringUtils::IsSameAs(typeName, s_strTypeTextOrString))
		return;

	m_view->m_spreadsheetObject->GetSpreadsheetDesc().SetCellFillType(row, col, type);
	SetValue(row, col, *static_cast<wxString*>(value));
}

void* ibGridEditor::ibGridEditorTable::GetValueAsCustom(int row, int col, const wxString& typeName)
{
	const ibSpreadsheetCellDescription* const cell = GetCell(row, col);
	const wxString raw = cell != nullptr ? wxString(cell->m_value) : wxString();

	// A caption and a template are asked for AS WRITTEN — every language, for the editor that edits
	// them. The stored form of a plain text is the language in force (CreateLocalizationRawLocText),
	// which the text fills used to take apart by hand and lost: a cell holding plain text came back empty.
	if (stringUtils::IsSameAs(typeName, s_strTypeTextOrString) || stringUtils::IsSameAs(typeName, s_strTypeTemplate))
		return new wxString(ibLocalization::CreateLocalizationRawLocText(ibBackendLocalization::GetUserLanguage(), raw));

	if (stringUtils::IsSameAs(typeName, s_strTypeParameter)) {
		const ibSpreadsheetFillType type = GetFillType(row, col);
		if (type == ibSpreadsheetFillType_StrText || type == ibSpreadsheetFillType_StrTemplate)
			return new wxString(ibBackendLocalization::GetTranslateGetRawLocText(raw));
		return new wxString(raw);
	}

	return nullptr;
}

bool ibGridEditor::ibGridEditorTable::InsertRows(size_t pos, size_t numRows)
{
	if (pos >= static_cast<size_t>(m_numRows))
		return AppendRows(numRows);
	wxFAIL_MSG(wxT("ibGridEditorTable: rows are not put inside the sheet - its cells are the document's"));
	return false;
}

bool ibGridEditor::ibGridEditorTable::AppendRows(size_t numRows)
{
	m_numRows += static_cast<int>(numRows);
	if (GetView()) {
		ibGridTableMessage msg(this, wxGRIDTABLE_NOTIFY_ROWS_APPENDED, static_cast<int>(numRows));
		GetView()->ProcessTableMessage(msg);
	}
	return true;
}

bool ibGridEditor::ibGridEditorTable::DeleteRows(size_t pos, size_t numRows)
{
	if (pos >= static_cast<size_t>(m_numRows)) {
		wxFAIL_MSG(wxT("ibGridEditorTable::DeleteRows: no such row"));
		return false;
	}
	numRows = wxMin(numRows, static_cast<size_t>(m_numRows) - pos);
	if (pos + numRows != static_cast<size_t>(m_numRows)) {
		wxFAIL_MSG(wxT("ibGridEditorTable: rows are not taken from inside the sheet - its cells are the document's"));
		return false;
	}
	m_numRows -= static_cast<int>(numRows);
	if (GetView()) {
		ibGridTableMessage msg(this, wxGRIDTABLE_NOTIFY_ROWS_DELETED, static_cast<int>(pos), static_cast<int>(numRows));
		GetView()->ProcessTableMessage(msg);
	}
	return true;
}

bool ibGridEditor::ibGridEditorTable::InsertCols(size_t pos, size_t numCols)
{
	if (pos >= static_cast<size_t>(m_numCols))
		return AppendCols(numCols);
	wxFAIL_MSG(wxT("ibGridEditorTable: columns are not put inside the sheet - its cells are the document's"));
	return false;
}

bool ibGridEditor::ibGridEditorTable::AppendCols(size_t numCols)
{
	m_numCols += static_cast<int>(numCols);
	if (GetView()) {
		ibGridTableMessage msg(this, wxGRIDTABLE_NOTIFY_COLS_APPENDED, static_cast<int>(numCols));
		GetView()->ProcessTableMessage(msg);
	}
	return true;
}

bool ibGridEditor::ibGridEditorTable::DeleteCols(size_t pos, size_t numCols)
{
	if (pos >= static_cast<size_t>(m_numCols)) {
		wxFAIL_MSG(wxT("ibGridEditorTable::DeleteCols: no such column"));
		return false;
	}
	numCols = wxMin(numCols, static_cast<size_t>(m_numCols) - pos);
	if (pos + numCols != static_cast<size_t>(m_numCols)) {
		wxFAIL_MSG(wxT("ibGridEditorTable: columns are not taken from inside the sheet - its cells are the document's"));
		return false;
	}
	m_numCols -= static_cast<int>(numCols);
	if (GetView()) {
		ibGridTableMessage msg(this, wxGRIDTABLE_NOTIFY_COLS_DELETED, static_cast<int>(pos), static_cast<int>(numCols));
		GetView()->ProcessTableMessage(msg);
	}
	return true;
}

ibGridCellAttr* ibGridEditor::ibGridEditorAttrProvider::GetAttr(int row, int col, ibGridCellAttr::wxAttrKind kind) const
{
	ibGridCellAttr* const held = ibGridCellAttrProvider::GetAttr(row, col, kind);

	// Asked for the cell's OWN attribute, the grid is about to change it (GetOrCreateCellAttr): what it holds.
	if (kind != ibGridCellAttr::Any || m_view->m_spreadsheetObject == nullptr)
		return held;

	const ibSpreadsheetDescription& desc = m_view->m_spreadsheetObject->GetSpreadsheetDesc();
	const ibSpreadsheetCellDescription* const cell = desc.GetCell(row, col);
	if (cell == nullptr)
		return held;

	ibGridCellAttr* const look = new ibGridCellAttr;
	look->SetAlignment(cell->m_alignHorz, cell->m_alignVert);
	look->SetTextOrient(cell->m_textOrient);
	look->SetFont(typeConv::FontToWxFont(cell->m_font));
	look->SetBackgroundColour(typeConv::ColourToWxColour(cell->m_backgroundColour));
	look->SetTextColour(typeConv::ColourToWxColour(cell->m_textColour));

	look->SetBorderLeft(static_cast<wxPenStyle>(cell->m_borderAt[0].m_style), typeConv::ColourToWxColour(cell->m_borderAt[0].m_colour), cell->m_borderAt[0].m_width);
	look->SetBorderRight(static_cast<wxPenStyle>(cell->m_borderAt[1].m_style), typeConv::ColourToWxColour(cell->m_borderAt[1].m_colour), cell->m_borderAt[1].m_width);
	look->SetBorderTop(static_cast<wxPenStyle>(cell->m_borderAt[2].m_style), typeConv::ColourToWxColour(cell->m_borderAt[2].m_colour), cell->m_borderAt[2].m_width);
	look->SetBorderBottom(static_cast<wxPenStyle>(cell->m_borderAt[3].m_style), typeConv::ColourToWxColour(cell->m_borderAt[3].m_colour), cell->m_borderAt[3].m_width);

	look->SetFitMode(ibToGridFitMode(cell->m_fitMode));
	look->SetReadOnly(cell->m_isReadOnly);

	// What the grid holds stands over it — first merged in is what is kept (ibGridCellAttr::MergeWith).
	ibGridCellAttr* attr = look;
	if (held != nullptr) {
		attr = new ibGridCellAttr;
		attr->SetKind(ibGridCellAttr::Merged);
		attr->MergeWith(held);
		attr->MergeWith(look);
		held->DecRef();
		look->DecRef();
	}

	// ⭐ THE AUTOMATIC PLACEMENT CHOSEN LAST, over whatever was merged: it runs on (Overflow) or breaks (Auto) by its
	// row, the cell after it and the room to the sheet's end — the same answer the row's height is worked out by
	// (IsCellWrapped), measured on the same DC (FitAutoRowHeights). A placement the grid holds itself (one set
	// through it, pasted, undone) was translated without the row, and would keep that.
	if (!attr->HasOverflowMode() || attr->GetFitMode().IsOverflow() || attr->GetFitMode().IsAuto()) {
		const auto textWidth = [&] {
			wxClientDC dc(m_view->GetGridWindow());
			return ibSpreadsheetTextWidth(*m_view->m_spreadsheetObject, row, col, dc, m_view->m_spreadsheetObject->GetLangCode());
		};
		attr->SetFitMode(desc.IsCellWrapped(row, col, textWidth) ? ibGridFitMode::Auto() : ibGridFitMode::Overflow());
	}
	return attr;
}

#pragma endregion

bool ibGridEditor::AssociateDocument(const std::shared_ptr<ibSpreadsheetObject>& doc)
{
	if (m_spreadsheetObject != doc) {

		if (m_spreadsheetObject != nullptr)
			m_spreadsheetObject->RemoveNotifier(m_notifier);

		m_spreadsheetObject = doc;
		m_notifier = m_spreadsheetObject->AddNotifier<ibGenericSpreadsheetNotifier>(this);
	}

	return true;
}

bool ibGridEditor::GetActiveDocument(std::shared_ptr<ibSpreadsheetObject>& doc) const
{
	doc = m_spreadsheetObject;
	return true;
}

void ibGridEditor::FitAutoRowHeights(int fromRow, int toRow)
{
	if (m_spreadsheetObject == nullptr)
		return;

	const ibSpreadsheetDescription& desc = m_spreadsheetObject->GetSpreadsheetDesc();
	const int firstRow = wxMax(0, fromRow), lastRow = wxMin(toRow, ibGrid::GetNumberRows() - 1);
	if (lastRow < firstRow)
		return;

	if (m_rowsFitted.size() <= static_cast<size_t>(lastRow))
		m_rowsFitted.resize(lastRow + 1, false);

	// The heights first, the grid told after: rows that keep the height they have change nothing and ask for no
	// repaint — the grid paints the whole window when a row in view changed its height (ibGridWindow::OnPaint). No
	// wxFont and no DC until a row is measured: rows fitted already are most of every paint.
	std::vector<std::pair<int, int>> heights;
	std::unique_ptr<wxClientDC> dc;
	wxString langCode;
	for (int row = firstRow; row <= lastRow; row++) {
		// A row folded away or hidden is fitted when it is shown: the size it is drawn at now is none.
		if (m_rowsFitted[row] || ibGrid::GetRowSize(row) == 0)
			continue;
		m_rowsFitted[row] = true;

		// A height of its own stays exactly as it was set — a blank's band is a fixed piece of paper.
		if (desc.HasRowSize(row))
			continue;

		if (dc == nullptr) {
			dc = std::make_unique<wxClientDC>(GetGridWindow());
			langCode = m_spreadsheetObject->GetLangCode();
		}
		const int height = ibSpreadsheetRowHeight(*m_spreadsheetObject, row, *dc, langCode);
		if (height != ibGrid::GetRowSize(row))
			heights.emplace_back(row, height);
	}
	if (heights.empty())
		return;

	// One refresh for the lot, not one per row; and while this runs, a row size is not a height somebody
	// chose — the grid announces every one of them and OnGridRowSize would write it down (gridEditor.h).
	// ⚠ NO FREEZE: this runs inside the grid's paint, which draws once with the heights set here — a whole
	// sheet fitted at once, with a repaint between rows, was the flicker the window used to be held still for
	// (Max, 2026-09-22).
	m_quietSizing = true;
	ibGrid::BeginBatch();
	for (const auto& [row, height] : heights)
		ibGrid::SetRowSize(row, height, 1.0f, false);
	ibGrid::EndBatch();
	m_quietSizing = false;
}

void ibGridEditor::RequestAutoRowHeights(int fromRow, int toRow)
{
	if (fromRow < 0 || toRow < fromRow)
		return;

	const int lastMarked = wxMin(toRow, static_cast<int>(m_rowsFitted.size()) - 1);
	if (fromRow <= lastMarked)
		std::fill(m_rowsFitted.begin() + fromRow, m_rowsFitted.begin() + lastMarked + 1, false);

	// The rows drawn again, the whole of each: the rows in view are fitted before they are, and every cell of them is
	// drawn as it stands now — the cell before a changed one runs on over it or breaks by it (IsCellWrapped). In a
	// batch the grid draws itself whole when it ends (ibGrid::EndBatch).
	const int lastRow = wxMin(toRow, ibGrid::GetNumberRows() - 1);
	if (fromRow <= lastRow && ibGrid::GetNumberCols() > 0 && ibGrid::GetBatchCount() == 0)
		ibGrid::RefreshBlock(fromRow, 0, lastRow, ibGrid::GetNumberCols() - 1);
}

#pragma region file

// A template or a file: the document of the editor made from it — read the same way as one shown on a form.
bool ibGridEditor::LoadDocument(const ibSpreadsheetDescription& spreadsheetDesc)
{
	return LoadDocument(std::make_shared<ibSpreadsheetObject>(spreadsheetDesc));
}

bool ibGridEditor::LoadDocument(const std::shared_ptr<ibSpreadsheetObject>& doc)
{
	// The document first: the table reads the cells there (ibGridEditorTable).
	const bool associated = AssociateDocument(doc);
	const ibSpreadsheetDescription& spreadsheetDesc = doc->GetSpreadsheetDesc();

	// ⚠ THE SHEET COMES UP WHOLE — the window held still while it is read in, and the grid in a batch. A column's
	// width moves every column after it, and the window scrolled to fit paints what is pending first
	// (ibWindow::ScrollWindow): a composed report's cells came up column by column under the «Composing...» spinner,
	// 13 paints in its 0.85 s (Max, 2026-11-07).
	wxWindowUpdateLocker freeze(this);

	if (!spreadsheetDesc.IsEmptySpreadsheet())
	{
		ibGridUpdateLocker batch(this);

		ibGrid::SetTable(
			new ibGridEditorTable(this, spreadsheetDesc.GetNumberRows(), spreadsheetDesc.GetNumberCols()), true);

		ibGrid::SetEvtHandlerEnabled(false);

		// The cells are the document's (ibGridEditorTable): of a cell the grid keeps only a merge.
		for (int idx = 0; idx < spreadsheetDesc.GetCellCount(); idx++) {
			const ibSpreadsheetCellDescription* cell = spreadsheetDesc.GetCellByIdx(idx);
			if (cell != nullptr)
				GridApplyCellSpan(*this, cell->m_row, cell->m_col, cell->m_row_size, cell->m_col_size);
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
				ibGrid::AppendRows((int)row_size->m_row - ibGrid::GetNumberRows() + 1);   // rows — it said columns, and the row then set did not exist

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
	// The rows without a height of their own are sized to their text as they are first shown — those in view
	// before the first paint (FitAutoRowHeights); none of this sheet's has been yet.
	m_rowsFitted.clear();
	return associated;
}

bool ibGridEditor::SaveDocument(ibSpreadsheetDescription& spreadsheetDesc) const
{
	return SaveSpreadsheet(spreadsheetDesc);
}

bool ibGridEditor::SaveDocument(std::shared_ptr<ibSpreadsheetObject>& doc) const
{
	return SaveSpreadsheet(doc->GetSpreadsheetDesc());
}
void ibGridEditor::PutDocument(const std::shared_ptr<ibSpreadsheetObject>& doc, unsigned int groupLevel)
{
	ibGrid::SetEvtHandlerEnabled(false);

	if (m_table == nullptr) {
		ibGrid::SetTable(
			new ibGridEditorTable(this), true);
	}

	// ⭐ THE AREA STARTS WHERE THE DOCUMENT'S ROWS END: it tells this before it stores the area (ibSpreadsheetObject::
	// PutArea), and tells the sheet's end and a page break kept there itself after (SetRowBrake, AddRowBrake). The grid
	// counted from an end of its own, a row past the document's, and drew the sheet's end there.
	const int maxRowBrake = m_spreadsheetObject->GetNumberRows();

	ibGrid::AppendRows(doc->GetNumberRows());
	if (doc->GetNumberCols() > m_table->GetNumberCols())
		ibGrid::AppendCols(doc->GetNumberCols() - m_table->GetNumberCols());

	m_numRows = m_table->GetNumberRows();
	m_numCols = m_table->GetNumberCols();

	// ⭐ THE AREA'S CELLS ARE THE DOCUMENT'S — it stores them as it puts the area (ibSpreadsheetObject::PutArea, the
	// text in its language), and the grid reads them there (ibGridEditorTable). Of a cell the grid keeps a merge.
	const ibSpreadsheetDescription& area = doc->GetSpreadsheetDesc();
	for (int idx = 0; idx < area.GetCellCount(); idx++) {
		const ibSpreadsheetCellDescription* cell = area.GetCellByIdx(idx);
		if (cell != nullptr)
			GridApplyCellSpan(*this, maxRowBrake + cell->m_row, cell->m_col, cell->m_row_size, cell->m_col_size);
	}

	// A height only where the area's row has one; the rest have automatic height, fitted when they are shown —
	// by then the document has taken the area in (it is told about it before it stores it, RequestAutoRowHeights).
	for (int row = 0; row < doc->GetNumberRows(); row++)
		if (doc->GetSpreadsheetDesc().HasRowSize(row))
			SetRowSize(maxRowBrake + row, doc->GetRowSize(row));
	RequestAutoRowHeights(maxRowBrake, maxRowBrake + doc->GetNumberRows() - 1);

	for (int col = 0; col < doc->GetNumberCols(); col++)
		SetColSize(col, doc->GetColSize(col));

	if (groupLevel > 0 && doc->GetNumberRows() > 0) {
		AddRowGroup(maxRowBrake, maxRowBrake + doc->GetNumberRows() - 1, (int)groupLevel);
		if (m_rowOutlineWin) m_rowOutlineWin->Refresh();
		CalcDimensions();
	}

	ibGrid::EnableEditing(doc->IsEditable());
	ibGrid::SetEvtHandlerEnabled(true);
}

void ibGridEditor::JoinDocument(const std::shared_ptr<ibSpreadsheetObject>& doc, unsigned int groupLevel)
{
	ibGrid::SetEvtHandlerEnabled(false);

	if (m_table == nullptr) {
		ibGrid::SetTable(
			new ibGridEditorTable(this), true);
	}

	const int maxColBrake = m_spreadsheetObject->GetNumberCols();   // where the document's columns end — see PutDocument

	if (doc->GetNumberRows() > m_table->GetNumberRows())
		ibGrid::AppendRows(doc->GetNumberRows() - m_table->GetNumberRows());
	ibGrid::AppendCols(doc->GetNumberCols());

	m_numRows = m_table->GetNumberRows();
	m_numCols = m_table->GetNumberCols();

	// the area's cells are the document's, of a cell the grid keeps a merge — see PutDocument
	const ibSpreadsheetDescription& area = doc->GetSpreadsheetDesc();
	for (int idx = 0; idx < area.GetCellCount(); idx++) {
		const ibSpreadsheetCellDescription* cell = area.GetCellByIdx(idx);
		if (cell != nullptr)
			GridApplyCellSpan(*this, cell->m_row, maxColBrake + cell->m_col, cell->m_row_size, cell->m_col_size);
	}

	// a height only where the area's row has one, the rest fitted afterwards — see PutDocument
	for (int row = 0; row < doc->GetNumberRows(); row++)
		if (doc->GetSpreadsheetDesc().HasRowSize(row))
			SetRowSize(row, doc->GetRowSize(row));
	RequestAutoRowHeights(0, doc->GetNumberRows() - 1);

	for (int col = 0; col < doc->GetNumberCols(); col++)
		SetColSize(maxColBrake + col, doc->GetColSize(col));

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

bool ibGridEditor::SaveSpreadsheet(ibSpreadsheetDescription& spreadsheetDesc) const
{
	spreadsheetDesc = m_spreadsheetObject->GetSpreadsheetDesc();
	return true;
}

#pragma endregion