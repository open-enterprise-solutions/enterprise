////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : grid printer 
////////////////////////////////////////////////////////////////////////////

#include "gridPrintout.h"

ibGridEditorPrintout::ibGridEditorPrintout(const wxString& title) : wxPrintout(title)
{
	SetStyle(wxGP_SHOW_NONE);

	m_minPage = 1;
	m_maxPage = 1;
	m_selPageFrom = 1;
	m_selPageTo = 1;
	m_screenScale = 1.0;
	m_userScale = 1.0;
	m_topMargin = 50;
	m_bottomMargin = 50;
	m_leftMargin = 50;
	m_rightMargin = 50;
}

ibGridEditorPrintout::ibGridEditorPrintout(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc, int style, const wxString& title) : wxPrintout(title)
{
	m_doc = doc;
	SetStyle(style);

	m_minPage = 1;
	m_maxPage = 1;
	m_selPageFrom = 1;
	m_selPageTo = 1;
	m_screenScale = 1.0;
	m_userScale = 1.0;
	m_topMargin = 50;
	m_bottomMargin = 50;
	m_leftMargin = 50;
	m_rightMargin = 50;
}

void ibGridEditorPrintout::SetStyle(int style)
{
	m_style = style;

	m_showCl = false;
	m_showRl = false;
	m_showClAlways = false;
	m_showRlAlways = false;

	if (m_style & wxGP_DEFAULT) {
		m_showCl = true;
		m_showRl = true;
		m_showClAlways = true;
		m_showRlAlways = true;

		return;
	}
	if (m_style & wxGP_SHOW_CL)
		m_showCl = true;
	if (m_style & wxGP_SHOW_RL)
		m_showRl = true;
	if (m_style & wxGP_SHOW_CL_ALWAYS)
		m_showClAlways = true;
	if (m_style & wxGP_SHOW_RL_ALWAYS)
		m_showRlAlways = true;
}

int ibGridEditorPrintout::GetStyle() const
{
	return m_style;
}

// ⭐⭐ PRINT ASKS THE DOCUMENT THE SAME QUESTION THE SCREEN ASKS (Max, 2026-08-28: *"print must
// simply get the same thing the render gets"*). The document holds the translation too, and it
// knows how to hand out the string — `ComputeStringValueFromParameters` is that door, and the
// notifier that fills the grid goes through it (gridEditorDoc, `SetValueAsCustom`).
//
// 🛑 PRINTING DID NOT. It called the translator itself, on the RAW cell value, and threw the answer
// away — and that answer is `false` for anything that is not a localisation envelope, in which case
// the translator CLEARS the string. So every cell printed as nothing while the same cell on screen
// read fine: fills and rules on paper, not one character of text.
wxString ibGridEditorPrintout::CellText(int row, int col) const
{
	return m_doc->ComputeStringValueFromParameters(
		m_doc->GetCellValue(row, col), m_doc->GetCellFillType(row, col));
}

// ⭐⭐ A PAGE BREAK MAY NOT FALL INSIDE A MERGED CELL (Max, 2026-08-28: *"you have a merged cell and
// it somehow carries it over, tears it"*). The pagination measured row heights and knew nothing
// about spans, so a break could land in the middle of one — and then the sheet showed the damage
// twice: the main cell drew its whole height past the bottom of its page, and on the NEXT page its
// remaining rows are `CellSpan_Inside`, which the drawing loop skips entirely. A blank band.
//
// A covered cell carries the NEGATIVE offset to the cell that owns it (wxGrid's convention), so the
// place the break belongs is simply that owner: the whole block moves to the next page.
int ibGridEditorPrintout::RowBreakAt(int row) const
{
	int highest = row;
	for (int col = 0; col < m_doc->GetNumberCols(); col++) {
		int cell_rows = 0, cell_cols = 0;
		if (m_doc->GetCellSize(row, col, &cell_rows, &cell_cols) == ibGrid::CellSpan_Inside
		    && row + cell_rows < highest)
			highest = row + cell_rows;
	}
	return highest;
}

int ibGridEditorPrintout::ColBreakAt(int col) const
{
	int leftmost = col;
	for (int row = 0; row < m_doc->GetNumberRows(); row++) {
		int cell_rows = 0, cell_cols = 0;
		if (m_doc->GetCellSize(row, col, &cell_rows, &cell_cols) == ibGrid::CellSpan_Inside
		    && col + cell_cols < leftmost)
			leftmost = col + cell_cols;
	}
	return leftmost;
}

bool ibGridEditorPrintout::OnPrintPage(int page)
{
	wxDC* dc = GetDC();

	if (dc == nullptr)
		return false;

	CalculateScale(dc);

	m_overallScale = m_screenScale * m_userScale;

	dc->SetUserScale(m_overallScale, m_overallScale);
	dc->SetDeviceOrigin(50 * m_overallScale, 50 * m_overallScale);

	return DrawPage(dc, page);
}

// ⚠ THE PAGE HAS TO EXIST. This said yes to any number at all, while `DrawPage` indexes
// `m_colsPerPage` by it without a check — the bound was stated in one place and trusted in another.
bool ibGridEditorPrintout::HasPage(int page)
{
	return page >= m_minPage && page <= m_maxPage;
}

void ibGridEditorPrintout::GetPageInfo(int* minPage, int* maxPage, int* selPageFrom, int* selPageTo)
{
	*minPage = m_minPage;
	*maxPage = m_maxPage;
	*selPageFrom = m_selPageFrom;
	*selPageTo = m_selPageTo;
}

bool ibGridEditorPrintout::DrawPage(wxDC* dc, int page)
{
	int columnPages = m_colsPerPage.Count();

	int colIndex, rowIndex;

	page--;

	colIndex = page % columnPages;
	rowIndex = page / columnPages;

	int toCol;
	if (colIndex == m_colsPerPage.Count() - 1)
		toCol = m_doc->GetNumberCols();
	else
		toCol = m_colsPerPage.Item(colIndex + 1);
	int toRow;
	if (rowIndex == m_rowsPerPage.Count() - 1)
		toRow = m_doc->GetNumberRows();
	else
		toRow = m_rowsPerPage.Item(rowIndex + 1);

	int countWidth = 0;
	int countHeight = 0;

	int cellInitialH = 0;
	int cellInitialW = 0;

	//draw column headers if requested //
	if ((m_showCl && rowIndex == 0) || m_showClAlways) {
		if ((m_showRl && colIndex == 0) || m_showRlAlways) {
			dc->SetBrush(*wxLIGHT_GREY_BRUSH);
			dc->DrawRectangle(countWidth, 0, m_doc->GetRowLabelSize(), m_doc->GetColLabelSize());
			countWidth += m_doc->GetRowLabelSize();
		}
		for (int i = m_colsPerPage.Item(colIndex); i < toCol; i++) {
			dc->SetBrush(*wxLIGHT_GREY_BRUSH);
			wxString str = m_doc->GetColLabelValue(i);
			wxRect rect = wxRect(countWidth, 0, m_doc->GetColSize(i), m_doc->GetColLabelSize());
			wxFont fnt = m_doc->GetLabelFont();
			DrawTextInRectangle(*dc, str, rect, fnt, *wxBLACK, wxALIGN_CENTER, wxALIGN_CENTER);
			countWidth += m_doc->GetColSize(i);
		}

		cellInitialH = m_doc->GetColLabelSize();
	}
	//////////////////////////////////////

	//draw row headers if requested //
	countHeight = cellInitialH;
	if ((m_showRl && colIndex == 0) || m_showRlAlways) {
		for (int i = m_rowsPerPage.Item(rowIndex); i < toRow; i++) {
			dc->SetBrush(*wxLIGHT_GREY_BRUSH);
			wxString str = m_doc->GetRowLabelValue(i);
			wxRect rct = wxRect(0, countHeight, m_doc->GetRowLabelSize(), m_doc->GetRowSize(i));
			wxFont fnt = m_doc->GetLabelFont();
			DrawTextInRectangle(*dc, str, rct, fnt, *wxBLACK, wxALIGN_CENTER, wxALIGN_CENTER);
			countHeight += m_doc->GetRowSize(i);
		}
		cellInitialW = m_doc->GetRowLabelSize();
	}
	
	////////////////////////////////

	// Draw cell content //
	countHeight = cellInitialH;
	
	for (int row = m_rowsPerPage.Item(rowIndex); row < toRow; row++) {
		
		countWidth = cellInitialW;

		for (int col = m_colsPerPage.Item(colIndex); col < toCol; col++) {
			
			int cell_rows, cell_cols;
			if (m_doc->GetCellSize(row, col, &cell_rows, &cell_cols) == ibGrid::CellSpan_Main) {
			
				int colSize = 0, rowSize = 0;
				
				for (int i = col; i < col + cell_cols; i++)
					colSize += m_doc->GetColSize(i);
			
				for (int i = row; i < row + cell_rows; i++)
					rowSize += m_doc->GetRowSize(i);
				
				wxRect rect(countWidth, countHeight, colSize, rowSize);
				
				dc->SetBrush(m_doc->GetCellBackgroundColour(row, col));
				dc->SetPen(*wxTRANSPARENT_PEN);
				dc->DrawRectangle(rect.x + 1, rect.y, rect.width - 1, rect.height);
				
				int horz, vert;
				m_doc->GetCellAlignment(row, col, &horz, &vert);

				DrawTextInRectangle(*dc, CellText(row, col),
					rect,
					m_doc->GetCellFont(row, col),
					m_doc->GetCellTextColour(row, col),
					horz, vert,
					m_doc->GetCellTextOrient(row, col)
				);
			}
			else if (m_doc->GetCellSize(row, col, &cell_rows, &cell_cols) == ibGrid::CellSpan_None) {
				
				wxRect rect(countWidth, countHeight, m_doc->GetColSize(col), m_doc->GetRowSize(row));
				
				dc->SetBrush(m_doc->GetCellBackgroundColour(row, col));
				dc->SetPen(*wxTRANSPARENT_PEN);
				dc->DrawRectangle(rect.x + 1, rect.y, rect.width - 1, rect.height);
				
				int horz, vert;
				m_doc->GetCellAlignment(row, col, &horz, &vert);

				DrawTextInRectangle(*dc, CellText(row, col),
					rect,
					m_doc->GetCellFont(row, col),
					m_doc->GetCellTextColour(row, col),
					horz, vert,
					m_doc->GetCellTextOrient(row, col)
				);
			}

			countWidth += m_doc->GetColSize(col);
		}

		countHeight += m_doc->GetRowSize(row);
	}

	// Draw border content //
	countHeight = cellInitialH;

	for (int row = m_rowsPerPage.Item(rowIndex); row < toRow; row++) {

		countWidth = cellInitialW;

		for (int col = m_colsPerPage.Item(colIndex); col < toCol; col++) {

			int cell_rows, cell_cols;
			if (m_doc->GetCellSize(row, col, &cell_rows, &cell_cols) == ibGrid::CellSpan_Main) {

				int colSize = 0, rowSize = 0;

				for (int i = col; i < col + cell_cols; i++)
					colSize += m_doc->GetColSize(i);

				for (int i = row; i < row + cell_rows; i++)
					rowSize += m_doc->GetRowSize(i);

				wxRect rect(countWidth, countHeight, colSize, rowSize);

				ibSpreadsheetBorderDescription borderLeft = m_doc->GetCellBorderLeft(row, col);
				if (borderLeft.m_style != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
					dc->SetPen(wxPen(borderLeft.m_colour, borderLeft.m_width, borderLeft.m_style));
					dc->DrawLine(rect.GetLeft() + 1, rect.GetTop(), rect.GetLeft() + 1, rect.GetBottom() + 1);
				}

				ibSpreadsheetBorderDescription borderRight = m_doc->GetCellBorderRight(row, col);
				if (borderRight.m_style != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
					dc->SetPen(wxPen(borderLeft.m_colour, borderRight.m_width, borderRight.m_style));
					dc->DrawLine(rect.GetRight() + 2, rect.GetTop(), rect.GetRight() + 2, rect.GetBottom() + 1);
				}

				ibSpreadsheetBorderDescription borderTop = m_doc->GetCellBorderTop(row, col);
				if (borderTop.m_style != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
					dc->SetPen(wxPen(borderTop.m_colour, borderTop.m_width, borderTop.m_style));
					dc->DrawLine(rect.GetLeft() + 1 , rect.GetTop(), rect.GetRight() + 2, rect.GetTop());
				}

				ibSpreadsheetBorderDescription borderBottom = m_doc->GetCellBorderBottom(row, col);
				if (borderBottom.m_style != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
					dc->SetPen(wxPen(borderBottom.m_colour, borderBottom.m_width, borderBottom.m_style));
					dc->DrawLine(rect.GetLeft() + 1, rect.GetBottom() + 1, rect.GetRight() + 2, rect.GetBottom() + 1);
				}
			}
			else if (m_doc->GetCellSize(row, col, &cell_rows, &cell_cols) == ibGrid::CellSpan_None) {

				wxRect rect(countWidth, countHeight, m_doc->GetColSize(col), m_doc->GetRowSize(row));

				ibSpreadsheetBorderDescription borderLeft = m_doc->GetCellBorderLeft(row, col);
				if (borderLeft.m_style != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
					dc->SetPen(wxPen(borderLeft.m_colour, borderLeft.m_width, borderLeft.m_style));
					dc->DrawLine(rect.GetLeft() + 1, rect.GetTop(), rect.GetLeft() + 1, rect.GetBottom() + 1);
				}

				ibSpreadsheetBorderDescription borderRight = m_doc->GetCellBorderRight(row, col);
				if (borderRight.m_style != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
					dc->SetPen(wxPen(borderLeft.m_colour, borderRight.m_width, borderRight.m_style));
					dc->DrawLine(rect.GetRight() + 2, rect.GetTop(), rect.GetRight() + 2, rect.GetBottom() + 1);
				}

				ibSpreadsheetBorderDescription borderTop = m_doc->GetCellBorderTop(row, col);
				if (borderTop.m_style != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
					dc->SetPen(wxPen(borderTop.m_colour, borderTop.m_width, borderTop.m_style));
					dc->DrawLine(rect.GetLeft() + 1, rect.GetTop(), rect.GetRight() + 2, rect.GetTop());
				}

				ibSpreadsheetBorderDescription borderBottom = m_doc->GetCellBorderBottom(row, col);
				if (borderBottom.m_style != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
					dc->SetPen(wxPen(borderBottom.m_colour, borderBottom.m_width, borderBottom.m_style));
					dc->DrawLine(rect.GetLeft() + 1, rect.GetBottom() + 1, rect.GetRight() + 2, rect.GetBottom() + 1);
				}
			}

			countWidth += m_doc->GetColSize(col);
		}

		countHeight += m_doc->GetRowSize(row);
	}

	////////////////////////////////

	//dc->SetBrush(*wxTRANSPARENT_BRUSH);
	//dc->SetPen(*wxBLACK_PEN);
	//dc->DrawRectangle(1, 1, m_maxWidth, m_maxHeight);

	return true;
}

void ibGridEditorPrintout::OnPreparePrinting()
{
	wxDC* dc = GetDC();
	if (dc == nullptr)
		return;   // …as OnPrintPage already checks; this dereferenced it regardless

	CalculateScale(dc);
	m_overallScale = m_screenScale * m_userScale;

	dc->SetUserScale(m_overallScale, m_overallScale);
	dc->GetSize(&m_maxWidth, &m_maxHeight);

	m_maxWidth /= m_overallScale;
	m_maxHeight /= m_overallScale;

	m_maxWidth -= 100;
	m_maxHeight -= 100;

	long widthCount = 0, heightCount = 0;

	//Calculate pages per columns
	m_colsPerPage.Clear();
	m_colsPerPage.Add(0);

	if (m_showRl || m_showRlAlways)
		widthCount = m_doc->GetRowLabelSize();

	for (int i = 0; i < m_doc->GetMaxColBrake(); i++) {
		const int size = m_doc->GetColSize(i);
		const bool overflows = (widthCount + size) > m_maxWidth;
		const bool asked = m_colsPerPage.Last() != i && m_doc->IsColBrake(i);

		if (overflows || asked) {
			// Not here if that tears a merged cell — the block goes over whole.
			const int at = ColBreakAt(i);

			// ⚠ AND ONLY IF THE BREAK MOVES. A column wider than the whole sheet overflows the moment
			// it starts, so breaking before it puts it at the head of the next page where it overflows
			// again — the old loop added the same index forever and the page array grew without end.
			// A column that cannot share a page gets one to itself.
			if (at > m_colsPerPage.Last()) {
				m_colsPerPage.Add(at);
				widthCount = m_showRlAlways ? m_doc->GetRowLabelSize() : 0;
				i = at - 1;         // …and the walk resumes AT the break
				continue;
			}
		}
		widthCount += size;
	}

	//Calculate pager per rows
	m_rowsPerPage.Clear();
	m_rowsPerPage.Add(0);

	if (m_showCl || m_showClAlways)
		heightCount = m_doc->GetColLabelSize();

	for (int i = 0; i < m_doc->GetMaxRowBrake(); i++) {
		const int size = m_doc->GetRowSize(i);
		const bool overflows = (heightCount + size) > m_maxHeight;
		const bool asked = m_rowsPerPage.Last() != i && m_doc->IsRowBrake(i);

		if (overflows || asked) {
			const int at = RowBreakAt(i);
			if (at > m_rowsPerPage.Last()) {
				m_rowsPerPage.Add(at);
				heightCount = m_showClAlways ? m_doc->GetColLabelSize() : 0;
				i = at - 1;
				continue;
			}
		}
		heightCount += size;
	}

	m_maxPage = m_rowsPerPage.GetCount() * m_colsPerPage.GetCount();
}

void ibGridEditorPrintout::CalculateScale(wxDC* dc)
{
	// You might use THIS code to set the printer DC to roughly
	// reflect the screen text size. This page also draws lines of
	// actual length 5cm on the page.

	// Get the logical pixels per inch of screen and printer
	int ppiScreenX, ppiScreenY;
	GetPPIScreen(&ppiScreenX, &ppiScreenY);
	int ppiPrinterX, ppiPrinterY;
	GetPPIPrinter(&ppiPrinterX, &ppiPrinterY);

	// This scales the DC so that the printout roughly represents the
	// the screen scaling.
	float scale = (float)((float)ppiPrinterX / (float)ppiScreenX);

	// Now we have to check in case our real page size is reduced
	// (e.g. because we're drawing to a print preview memory DC)
	int pageWidth, pageHeight;
	int w, h;
	dc->GetSize(&w, &h);
	GetPageSizePixels(&pageWidth, &pageHeight);

	// If printer pageWidth == current DC width, then this doesn't
	// change. But w might be the preview bitmap width,
	// so scale down.
	float screenScale = scale * (float)(w / (float)pageWidth);

	m_screenScale = screenScale;
}

void ibGridEditorPrintout::SetUserScale(float scale)
{
	m_userScale = scale;
	m_overallScale = m_screenScale * m_userScale;
}

void ibGridEditorPrintout::DrawTextInRectangle(wxDC& dc, const wxString& strValue, wxRect& rect, const wxFont& font, const wxColour& fontClr,
	int horizAlign, int vertAlign, int textOrientation)
{
	wxArrayString lines, naturalLines;
	ibGridEditor::ParseLines(strValue, naturalLines);

	rect.x += 2;
	rect.width -= 2;

	for (unsigned int i = 0; i < naturalLines.Count(); i++) {
		wxArrayString wrappedLines = GetTextLines(dc, naturalLines.Item(i), font, rect);
		for (unsigned int j = 0; j < wrappedLines.Count(); j++) {
			lines.Add(wrappedLines.Item(j));
		}
	}

	dc.SetTextBackground(fontClr);
	dc.SetTextForeground(fontClr);

	dc.SetFont(font);

	ibGridEditor::DrawTextRectangle(dc,
		lines, rect, horizAlign, vertAlign, textOrientation);
}

wxArrayString ibGridEditorPrintout::GetTextLines(wxDC& dc, const wxString& data, const wxFont& font, const wxRect& rect)
{
	wxArrayString lines;

	// (The wrapping pass below is commented out; its measuring variables went with it.)
	dc.SetFont(font);

	//wxStringTokenizer tk(data, _T(" \n\t\r"));
	//wxString thisline = wxEmptyString;

	//while (tk.HasMoreTokens())
	//{
	//	wxString tok = tk.GetNextToken();
	//	//FIXME: this causes us to print an extra unnecesary
	//	//       space at the end of the line. But it
	//	//       is invisible , simplifies the size calculation
	//	//       and ensures tokens are separated in the display
	//	tok += _T(" ");

	//	dc.GetTextExtent(tok, &x, &y);
	//	if (curr_x + x > max_x)
	//	{
	//		lines.Add(wxString(thisline));
	//		thisline = tok;
	//		curr_x = x;
	//	}
	//	else
	//	{
	//		thisline += tok;
	//		curr_x += x;
	//	}
	//}
	//
	////Add last line
	//lines.Add(wxString(thisline));

	lines.Add(data);
	return lines;
}
