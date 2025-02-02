////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : grid printer 
////////////////////////////////////////////////////////////////////////////

#include "gridPrintout.h"

CGridPrintout::CGridPrintout(const wxString& title) : wxPrintout(title)
{
	m_grid = nullptr;
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

CGridPrintout::CGridPrintout(CGrid* gridCtrl, int style, const wxString& title) : wxPrintout(title)
{
	m_grid = gridCtrl;
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

void CGridPrintout::SetGrid(CGrid* gridCtrl)
{
	m_grid = gridCtrl;
}

CGrid* CGridPrintout::GetGrid() const
{
	return m_grid;
}

void CGridPrintout::SetStyle(int style)
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

int CGridPrintout::GetStyle() const
{
	return m_style;
}

bool CGridPrintout::OnPrintPage(int page)
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

bool CGridPrintout::HasPage(int page)
{
	return true;
}

void CGridPrintout::GetPageInfo(int* minPage, int* maxPage, int* selPageFrom, int* selPageTo)
{
	*minPage = m_minPage;
	*maxPage = m_maxPage;
	*selPageFrom = m_selPageFrom;
	*selPageTo = m_selPageTo;
}

bool CGridPrintout::DrawPage(wxDC* dc, int page)
{
	int columnPages = m_colsPerPage.Count();
	int rowPages = m_rowsPerPage.Count();

	int colIndex, rowIndex;

	page--;

	colIndex = page % columnPages;
	rowIndex = page / columnPages;

	int toCol;
	if (colIndex == m_colsPerPage.Count() - 1)
		toCol = m_grid->GetNumberCols();
	else
		toCol = m_colsPerPage.Item(colIndex + 1);
	int toRow;
	if (rowIndex == m_rowsPerPage.Count() - 1)
		toRow = m_grid->GetNumberRows();
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
			dc->DrawRectangle(countWidth, 0, m_grid->GetRowLabelSize(), m_grid->GetColLabelSize());
			countWidth += m_grid->GetRowLabelSize();
		}
		for (int i = m_colsPerPage.Item(colIndex); i < toCol; i++) {
			dc->SetBrush(*wxLIGHT_GREY_BRUSH);
			wxString str = m_grid->GetColLabelValue(i);
			wxRect rect = wxRect(countWidth, 0, m_grid->GetColSize(i), m_grid->GetColLabelSize());
			wxFont fnt = m_grid->GetLabelFont();
			DrawTextInRectangle(*dc, str, rect, fnt, *wxBLACK, wxALIGN_CENTER, wxALIGN_CENTER);
			countWidth += m_grid->GetColSize(i);
		}

		cellInitialH = m_grid->GetColLabelSize();
	}
	//////////////////////////////////////

	//draw row headers if requested //
	countHeight = cellInitialH;
	if ((m_showRl && colIndex == 0) || m_showRlAlways) {
		for (int i = m_rowsPerPage.Item(rowIndex); i < toRow; i++) {
			dc->SetBrush(*wxLIGHT_GREY_BRUSH);
			wxString str = m_grid->GetRowLabelValue(i);
			wxRect rct = wxRect(0, countHeight, m_grid->GetRowLabelSize(), m_grid->GetRowSize(i));
			wxFont fnt = m_grid->GetLabelFont();
			DrawTextInRectangle(*dc, str, rct, fnt, *wxBLACK, wxALIGN_CENTER, wxALIGN_CENTER);
			countHeight += m_grid->GetRowSize(i);
		}
		cellInitialW = m_grid->GetRowLabelSize();
	}
	////////////////////////////////

	// Draw cell content //
	countHeight = cellInitialH;
	for (int row = m_rowsPerPage.Item(rowIndex); row < toRow; row++) {
		countWidth = cellInitialW;
		for (int col = m_colsPerPage.Item(colIndex); col < toCol; col++) {
			int cell_rows, cell_cols;
			if (m_grid->GetCellSize(row, col, &cell_rows, &cell_cols) == wxGrid::CellSpan_Main) {
				int colSize = 0, rowSize = 0;
				for (int i = col; i < col + cell_cols; i++)
					colSize += m_grid->GetColSize(col);
				for (int i = row; i < row + cell_rows; i++)
					rowSize += m_grid->GetRowSize(row);
				wxRect rect(countWidth, countHeight, colSize, rowSize);
				dc->SetBrush(m_grid->GetCellBackgroundColour(row, col));
				dc->SetPen(*wxTRANSPARENT_PEN);
				dc->DrawRectangle(rect);
				int horz, vert;
				m_grid->GetCellAlignment(row, col, &horz, &vert);
				DrawTextInRectangle(*dc, m_grid->GetCellValue(row, col),
					rect,
					m_grid->GetCellFont(row, col),
					m_grid->GetCellTextColour(row, col),
					horz, vert,
					m_grid->GetCellTextOrientation(row, col)
				);
				wxColour borderColour;
				wxPenStyle leftBorder, rightBorder,
					topBorder, bottomBorder;
				m_grid->GetCellBorder(row, col,
					&leftBorder, &rightBorder,
					&topBorder, &bottomBorder,
					&borderColour
				);
				if (leftBorder != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
					dc->SetPen( { borderColour, 2, leftBorder });
					dc->DrawLine(rect.GetLeft(), rect.GetTop() - 1,
						rect.GetLeft(), rect.GetBottom() + 1);
				}
				if (rightBorder != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
					dc->SetPen({ borderColour, 2, rightBorder });
					dc->DrawLine(rect.GetRight() + 1, rect.GetTop() - 1,
						rect.GetRight() + 1, rect.GetBottom() + 1);
				}
				if (topBorder != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
					dc->SetPen({ borderColour, 2, topBorder });
					dc->DrawLine(rect.GetLeft() - 1, rect.GetTop(),
						rect.GetRight() + 1, rect.GetTop());
				}
				if (bottomBorder != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
					dc->SetPen({ borderColour, 2, bottomBorder });
					dc->DrawLine(rect.GetLeft() - 1, rect.GetBottom(),
						rect.GetRight() + 1, rect.GetBottom());
				}
			}
			else if (m_grid->GetCellSize(row, col, &cell_rows, &cell_cols) == wxGrid::CellSpan_None) {
				wxRect rect(countWidth, countHeight, m_grid->GetColSize(col), m_grid->GetRowSize(row));
				dc->SetBrush(m_grid->GetCellBackgroundColour(row, col));
				dc->SetPen(*wxTRANSPARENT_PEN);
				dc->DrawRectangle(rect);
				int horz, vert;
				m_grid->GetCellAlignment(row, col, &horz, &vert);
				DrawTextInRectangle(*dc, m_grid->GetCellValue(row, col),
					rect,
					m_grid->GetCellFont(row, col),
					m_grid->GetCellTextColour(row, col),
					horz, vert,
					m_grid->GetCellTextOrientation(row, col)
				);
				wxColour borderColour;
				wxPenStyle leftBorder, rightBorder,
					topBorder, bottomBorder;
				m_grid->GetCellBorder(row, col,
					&leftBorder, &rightBorder,
					&topBorder, &bottomBorder,
					&borderColour
				);
				if (leftBorder != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
					dc->SetPen({ borderColour, 2, leftBorder });
					dc->DrawLine(rect.GetLeft(), rect.GetTop() - 1,
						rect.GetLeft(), rect.GetBottom() + 1);
				}
				if (rightBorder != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
					dc->SetPen({ borderColour, 2, rightBorder });
					dc->DrawLine(rect.GetRight() + 1, rect.GetTop() - 1,
						rect.GetRight() + 1, rect.GetBottom() + 1);
				}
				if (topBorder != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
					dc->SetPen({ borderColour, 2, topBorder });
					dc->DrawLine(rect.GetLeft() - 1, rect.GetTop(),
						rect.GetRight() + 1, rect.GetTop());
				}
				if (bottomBorder != wxPenStyle::wxPENSTYLE_TRANSPARENT) {
					dc->SetPen({ borderColour, 2, bottomBorder });
					dc->DrawLine(rect.GetLeft() - 1, rect.GetBottom(),
						rect.GetRight() + 1, rect.GetBottom());
				}
			}

			countWidth += m_grid->GetColSize(col);
		}
		countHeight += m_grid->GetRowSize(row);
	}

	////////////////////////////////

	dc->SetBrush(*wxTRANSPARENT_BRUSH);
	dc->SetPen(*wxBLACK_PEN);
	dc->DrawRectangle(1, 1, m_maxWidth, m_maxHeight);

	return true;
}

void CGridPrintout::OnPreparePrinting()
{
	wxDC* dc = GetDC();
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
		widthCount = m_grid->GetRowLabelSize();

	for (int i = 0; i < m_grid->GetNumberCols(); i++) {
		widthCount += m_grid->GetColSize(i);
		if ((widthCount >= m_maxWidth) || 
			(m_colsPerPage.Last() != i && m_grid->IsColBrake(i))) {
			m_colsPerPage.Add(i);
			if (m_showRlAlways)
				widthCount = m_grid->GetRowLabelSize();
			else
				widthCount = 0;
			i--; 
		}
	}

	//Calculate pager per rows
	m_rowsPerPage.Clear();
	m_rowsPerPage.Add(0);
	
	if (m_showCl || m_showClAlways)
		heightCount = m_grid->GetColLabelSize();

	for (int i = 0; i < m_grid->GetNumberRows(); i++) {
		heightCount += m_grid->GetRowSize(i);
		if ((heightCount >= m_maxHeight) || 
			(m_rowsPerPage.Last() != i && m_grid->IsRowBrake(i))) {
			m_rowsPerPage.Add(i);
			if (m_showClAlways)
				heightCount = m_grid->GetColLabelSize();
			else
				heightCount = 0;
			i--;
		}
	}

	m_maxPage = m_rowsPerPage.GetCount() * m_colsPerPage.GetCount();
}

void CGridPrintout::CalculateScale(wxDC* dc)
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

void CGridPrintout::SetUserScale(float scale)
{
	m_userScale = scale;
	m_overallScale = m_screenScale * m_userScale;
}

void CGridPrintout::DrawTextInRectangle(wxDC& dc, const wxString& strValue, wxRect& rect, const wxFont& font, const wxColour& fontClr,
	int horizAlign, int vertAlign, int textOrientation)
{
	wxArrayString lines, naturalLines;
	StringToLines(strValue, naturalLines);
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

	DrawTextRectangle(dc, lines, rect, horizAlign, vertAlign, textOrientation);
}

void CGridPrintout::DrawTextRectangle(wxDC& dc, const wxArrayString& lines, const wxRect& rect,
	int horizAlign, int vertAlign, int textOrientation)
{
	long textWidth, textHeight;
	wxCoord lineWidth, lineHeight;

	dc.SetClippingRegion(rect);

	int countLine = lines.GetCount();

	if (countLine > 0) {
		int l;
		float x = 0.0, y = 0.0;

		if (textOrientation == wxHORIZONTAL)
			GetTextBoxSize(dc, lines, &textWidth, &textHeight);
		else
			GetTextBoxSize(dc, lines, &textHeight, &textWidth);

		switch (vertAlign)
		{
		case wxALIGN_BOTTOM:
			if (textOrientation == wxHORIZONTAL)
				y = rect.y + (rect.height - textHeight - 1);
			else
				x = rect.x + rect.width - textWidth;
			break;

		case wxALIGN_CENTRE:
			if (textOrientation == wxHORIZONTAL)
				y = rect.y + ((rect.height - textHeight) / 2);
			else
				x = rect.x + ((rect.width - textWidth) / 2);
			break;

		case wxALIGN_TOP:
		default:
			if (textOrientation == wxHORIZONTAL)
				y = rect.y + 1;
			else
				x = rect.x + 1;
			break;
		}

		// Align each line of a multi-line label
		for (l = 0; l < countLine; l++)
		{
			dc.GetTextExtent(lines[l], &lineWidth, &lineHeight);

			switch (horizAlign)
			{
			case wxALIGN_RIGHT:
				if (textOrientation == wxHORIZONTAL)
					x = rect.x + (rect.width - lineWidth - 1);
				else
					y = rect.y + lineWidth + 1;
				break;

			case wxALIGN_CENTRE:
				if (textOrientation == wxHORIZONTAL)
					x = rect.x + ((rect.width - lineWidth) / 2);
				else
					y = rect.y + rect.height - ((rect.height - lineWidth) / 2);
				break;

			case wxALIGN_LEFT:
			default:
				if (textOrientation == wxHORIZONTAL)
					x = rect.x + 1;
				else
					y = rect.y + rect.height - 1;
				break;
			}

			if (textOrientation == wxHORIZONTAL) {
				dc.DrawText(lines[l], (int)x, (int)y);
				y += lineHeight;
			}
			else {
				dc.DrawRotatedText(lines[l], (int)x, (int)y, 90.0);
				x += lineHeight;
			}
		}
	}

	dc.DestroyClippingRegion();
}

void CGridPrintout::GetTextBoxSize(wxDC& dc, const wxArrayString& lines, long* width, long* height)
{
	long w = 0;
	long h = 0;
	wxCoord lineW, lineH;

	unsigned int i;
	for (i = 0; i < lines.GetCount(); i++) {
		dc.GetTextExtent(lines[i], &lineW, &lineH);
		w = wxMax(w, lineW);
		h += lineH;
	}

	*width = w;
	*height = h;
}

void CGridPrintout::StringToLines(const wxString& value, wxArrayString& lines)
{
	int startPos = 0;
	int pos;
	wxString eol = wxTextFile::GetEOL(wxTextFileType_Unix);
	wxString tVal = wxTextFile::Translate(value, wxTextFileType_Unix);

	while (startPos < (int)tVal.Length()) {
		pos = tVal.Mid(startPos).Find(eol);
		if (pos < 0) {
			break;
		}
		else if (pos == 0) {
			lines.Add(wxEmptyString);
		}
		else {
			lines.Add(value.Mid(startPos, pos));
		}
		startPos += pos + 1;
	}

	if (startPos < (int)value.Length()) {
		lines.Add(value.Mid(startPos));
	}
}

wxArrayString CGridPrintout::GetTextLines(wxDC& dc, const wxString& data, const wxFont& font, const wxRect& rect)
{
	wxArrayString lines;

	wxCoord x = 0, y = 0, curr_x = 0;
	wxCoord max_x = rect.GetWidth();

	dc.SetFont(font);
	wxStringTokenizer tk(data, _T(" \n\t\r"));
	wxString thisline = wxEmptyString;

	while (tk.HasMoreTokens())
	{
		wxString tok = tk.GetNextToken();
		//FIXME: this causes us to print an extra unnecesary
		//       space at the end of the line. But it
		//       is invisible , simplifies the size calculation
		//       and ensures tokens are separated in the display
		tok += _T(" ");

		dc.GetTextExtent(tok, &x, &y);
		if (curr_x + x > max_x)
		{
			lines.Add(wxString(thisline));
			thisline = tok;
			curr_x = x;
		}
		else
		{
			thisline += tok;
			curr_x += x;
		}
	}
	//Add last line
	lines.Add(wxString(thisline));

	return lines;
}