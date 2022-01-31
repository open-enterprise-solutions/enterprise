////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : grid window
////////////////////////////////////////////////////////////////////////////

#include "gridCommon.h"
#include "frontend/objinspect/objinspect.h"

#include <wx/generic/gridsel.h>
#include <wx/generic/private/grid.h>
#include <wx/renderer.h>

wxBEGIN_EVENT_TABLE(CGrid, wxGrid)
EVT_MOUSEWHEEL(CGrid::OnMouseWheel)
EVT_LEFT_DOWN(CGrid::OnMouseLeftDown)
EVT_LEFT_DCLICK(CGrid::OnMouseDClick)
EVT_KEY_DOWN(CGrid::OnKeyDown)
EVT_KEY_UP(CGrid::OnKeyUp)
EVT_GRID_SELECT_CELL(CGrid::OnSelectCell)
EVT_GRID_RANGE_SELECT(CGrid::OnSelectCells)
EVT_SCROLLWIN(CGrid::OnScroll)
EVT_SIZE(CGrid::OnSize)
wxEND_EVENT_TABLE()

class wxGridSectionTable : public wxGridStringTable {
	CGrid *m_grid;
public:

	wxGridSectionTable(CGrid *grid) : wxGridStringTable(50, 20), m_grid(grid) {}

	// overridden functions from wxGridStringTable
	//
	virtual void Clear() override
	{
		m_grid->m_sectionLeft->ClearSections();
		m_grid->m_sectionUpper->ClearSections();
		wxGridStringTable::Clear();
	}

	virtual bool InsertRows(size_t pos = 0, size_t numRows = 1) override
	{
		for (size_t i = 0; i < numRows; i++) m_grid->m_sectionLeft->InsertRow(pos + i);
		return wxGridStringTable::InsertRows(pos, numRows);
	}

	virtual bool AppendRows(size_t numRows = 1) override
	{
		m_grid->m_sectionLeft->InsertRow(GetNumberRows() + numRows);
		return wxGridStringTable::AppendRows(numRows);
	}

	virtual bool DeleteRows(size_t pos = 0, size_t numRows = 1) override
	{
		m_grid->m_sectionLeft->RemoveRow(numRows);
		return wxGridStringTable::DeleteRows(pos, numRows);
	}

	virtual bool AppendCols(size_t numCols = 1) override
	{
		m_grid->m_sectionUpper->InsertRow(GetNumberCols() + numCols);
		return wxGridStringTable::AppendCols(numCols);
	}

	virtual bool InsertCols(size_t pos = 0, size_t numCols = 1) override
	{
		for (size_t i = 0; i < numCols; i++) m_grid->m_sectionUpper->InsertRow(pos + i);
		return wxGridStringTable::InsertCols(pos, numCols);
	}

	virtual bool DeleteCols(size_t pos = 0, size_t numCols = 1) override
	{
		m_grid->m_sectionUpper->RemoveRow(numCols);
		return wxGridStringTable::DeleteCols(pos, numCols);
	}

	wxString GetColLabelValue(int col) override
	{
		wxString s;
		s << col + 1;
		return s;
	}
};

class wxGridCellSectionRenderer : public wxGridCellStringRenderer {
public:

	virtual void Draw(wxGrid& grid,
		wxGridCellAttr& attr,
		wxDC& dc,
		const wxRect& rect,
		int row, int col,
		bool isSelected) override
	{
		wxGridCellStringRenderer::Draw(grid, attr, dc, rect, row, col, isSelected);

		CGrid *m_grid = dynamic_cast<CGrid *>(&grid);
		wxString csSection;
		int nFind = m_grid->GetSectionLeft()->FindInSection(row, csSection);
		// top line
		if (nFind & 2)
		{
			//dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.SetPen(wxPen(wxColour(255, 0, 0), 1, wxPENSTYLE_SOLID));
			dc.DrawLine(rect.x, rect.y, rect.x + rect.width, rect.y);
		}
		// bottom line
		if (nFind & 4)
		{
			//dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.SetPen(wxPen(wxColour(255, 0, 0), 1, wxPENSTYLE_SOLID));
			dc.DrawLine(rect.x - rect.width, rect.y + rect.height, rect.x + rect.width, rect.y + rect.height - 1);
		}
		// left hand line
		nFind = m_grid->GetSectionUpper()->FindInSection(col, csSection);
		if (nFind & 2)
		{
			//dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.SetPen(wxPen(wxColour(255, 0, 0), 1, wxPENSTYLE_SOLID));
			dc.DrawLine(rect.x, rect.y, rect.x, rect.y + rect.height);
		}
		// right hand line
		if (nFind & 4)
		{
			//dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.SetPen(wxPen(wxColour(255, 0, 0), 1, wxPENSTYLE_SOLID));
			dc.DrawLine(rect.x + rect.width, rect.y - rect.height, rect.x + rect.width - 1, rect.y + rect.height);
		}
	}
};

bool CGrid::GetWorkSection(int &nRangeFrom, int &nRangeTo, CSectionCtrl *&pSectionCtrl)
{
	CGridCellRange selection = GetSelectedCellRange();

	if (selection.Count() > 0)
	{
		if (selection.GetRowSpan() < selection.GetColSpan())//гориз. секции
		{
			pSectionCtrl = m_sectionLeft;
			nRangeFrom = selection.GetMinRow();
			nRangeTo = selection.GetMaxRow();
		}
		else//верт. секции
		{
			pSectionCtrl = m_sectionUpper;
			nRangeFrom = selection.GetMinCol();
			nRangeTo = selection.GetMaxCol();
		}
		return true;
	}

	return false;
}

// ctor and Create() create the grid window, as with the other controls
CGrid::CGrid() : wxGrid()
{
	//set double buffered
	GetGridWindow()->SetDoubleBuffered(true);
}

CGrid::CGrid(wxWindow *parent,
	wxWindowID id, const wxPoint& pos,
	const wxSize& size) : wxGrid(parent, id, pos, size), 
	m_propertyCell(new CPropertyCell(this)), m_idTopLeftCell(1, 1)
{
	m_bViewSection = true;

	m_rowLabelWidth = WXGRID_DEFAULT_ROW_LABEL_WIDTH - 42;
	m_colLabelHeight = WXGRID_MIN_COL_WIDTH;

	m_defaultColWidth = WXGRID_DEFAULT_ROW_LABEL_WIDTH - 2;
	m_defaultRowHeight = WXGRID_MIN_ROW_HEIGHT;

	m_sectionLeft = new CSectionCtrl(this, eSectionMode::LEFT_MODE);
	m_sectionUpper = new CSectionCtrl(this, eSectionMode::UPPER_MODE);

	m_rowLabelWin->Bind(wxEVT_LEFT_DOWN, &CGrid::OnMouseLeftDown, this);
	m_colLabelWin->Bind(wxEVT_LEFT_DOWN, &CGrid::OnMouseLeftDown, this);
	m_rowLabelWin->Bind(wxEVT_LEFT_DCLICK, &CGrid::OnMouseDClick, this);
	m_colLabelWin->Bind(wxEVT_LEFT_DCLICK, &CGrid::OnMouseDClick, this);

	// Grid
	this->EnableEditing(true);
	this->EnableGridLines(true);
	this->EnableDragGridSize(false);
	this->SetMargins(0, 0);

	// Native col 
	//UseNativeColHeader(true);

	//set background stype 
	GetGridWindow()->SetBackgroundStyle(wxBG_STYLE_PAINT);

	//set double buffered
	GetGridWindow()->SetDoubleBuffered(true);

	//Cell renderer
	m_defaultCellAttr->SetRenderer(new wxGridCellSectionRenderer());

	// Cell Defaults
	this->SetDefaultCellAlignment(wxALIGN_LEFT, wxALIGN_TOP);
	SetTable(new wxGridSectionTable(this), true, wxGridSelectCells);

	SetDefaultRenderer(new wxGridCellSectionRenderer);
}

void CGrid::ViewSection()
{
	m_rowLabelWidth = WXGRID_DEFAULT_ROW_LABEL_WIDTH - 42 + m_sectionLeft->GetSize();
	m_colLabelHeight = WXGRID_MIN_COL_WIDTH + m_sectionUpper->GetSize();

	CalcDimensions();
}

void CGrid::SetParent(wxWindowBase *parent)
{
	wxGrid::SetParent(parent);
}

bool CGrid::Reparent(wxWindowBase *newParent)
{
	ViewSection();
	return wxGrid::Reparent(newParent);
}

void CGrid::AddSection()
{
	if (!IsEditable()) return;

	m_bViewSection = true;

	int nRangeFrom, nRangeTo;
	CSectionCtrl *pSectionCtrl;

	if (!GetWorkSection(nRangeFrom, nRangeTo, pSectionCtrl)) return;
	pSectionCtrl->Add(nRangeFrom, nRangeTo);

	ViewSection();
	Refresh();
}

void CGrid::RemoveSection()
{
	if (!IsEditable())
		return;

	m_bViewSection = false;

	int nRange1, nRange2;
	CSectionCtrl *pSectionCtrl;

	if (!GetWorkSection(nRange1, nRange2, pSectionCtrl)) return;
	pSectionCtrl->Remove(nRange1, nRange2);

	ViewSection();
	Refresh();
}

void CGrid::MergeCells()
{
	if (!IsEditable())
		return;

	CGridCellRange cellRange = GetSelectedCellRange();
	if (cellRange.IsValid()) SetCellSize(cellRange.GetMinRow(), cellRange.GetMinCol(), cellRange.GetRowSpan(), cellRange.GetColSpan());
	Refresh();
}

// this struct simply combines together the default header renderers
//
// as the renderers ctors are trivial, there is no problem with making them
// globals
struct DefaultHeaderRenderers
{
	wxGridColumnHeaderRendererDefault colRenderer;
	wxGridRowHeaderRendererDefault rowRenderer;
	wxGridCornerHeaderRendererDefault cornerRenderer;
} gs_defaultHeaderRenderers;

void CGrid::DrawCellHighlight(wxDC& dc, const wxGridCellAttr *attr)
{
	// don't show highlight when the grid doesn't have focus
	//if (!HasFocus())
	//	return;

	int row = m_currentCellCoords.GetRow();
	int col = m_currentCellCoords.GetCol();

	if (GetColWidth(col) <= 0 || GetRowHeight(row) <= 0)
		return;

	wxRect rect = CellToRect(row, col);

	// hmmm... what could we do here to show that the cell is disabled?
	dc.SetBackgroundMode(wxBRUSHSTYLE_SOLID);
	dc.SetBrush(GetSelectionBackground());
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRectangle(rect);
}

void CGrid::DrawCornerLabel(wxDC& dc)
{
	wxRect rect(m_sectionLeft->GetSize(), m_sectionUpper->GetSize(), m_rowLabelWidth, m_colLabelHeight);

	wxGridCellAttrProvider * const
		attrProvider = m_table ? m_table->GetAttrProvider() : NULL;
	const wxGridCornerHeaderRenderer&
		rend = attrProvider ? attrProvider->GetCornerRenderer()
		: static_cast<wxGridCornerHeaderRenderer&>
		(gs_defaultHeaderRenderers.cornerRenderer);

	if (m_nativeColumnLabels)
	{
		rect.Deflate(1);

		wxRendererNative::Get().DrawHeaderButton(m_cornerLabelWin, dc, rect, 0);
	}
	else
	{
		rect.width++;
		rect.height++;

		rend.DrawBorder(*this, dc, rect);
	}

	wxString label = GetCornerLabelValue();
	if (!label.IsEmpty())
	{
		int hAlign, vAlign;
		GetCornerLabelAlignment(&hAlign, &vAlign);
		const int orient = GetCornerLabelTextOrientation();

		rend.DrawLabel(*this, dc, label, rect, hAlign, vAlign, orient);
	}
}

void CGrid::DrawColLabels(wxDC& dc, const wxArrayInt& cols)
{
	if (!m_numCols)
		return;

	const size_t numLabels = cols.GetCount();
	for (size_t i = 0; i < numLabels; i++)
	{
		DrawColLabel(dc, cols[i]);
	}
}

void CGrid::DrawColLabel(wxDC& dc, int col)
{
	if (GetColWidth(col) <= 0 || m_colLabelHeight <= 0)
		return;

	int colLeft = GetColLeft(col);

	wxRect rect(colLeft, m_sectionUpper->GetSize(), GetColWidth(col), m_colLabelHeight - m_sectionUpper->GetSize());
	wxGridCellAttrProvider * const
		attrProvider = m_table ? m_table->GetAttrProvider() : NULL;
	const wxGridColumnHeaderRenderer&
		rend = attrProvider ? attrProvider->GetColumnHeaderRenderer(col)
		: static_cast<wxGridColumnHeaderRenderer&>
		(gs_defaultHeaderRenderers.colRenderer);

	if (m_nativeColumnLabels)
	{
		wxRendererNative::Get().DrawHeaderButton
		(
			GetColLabelWindow(),
			dc,
			rect,
			0,
			IsSortingBy(col)
			? IsSortOrderAscending()
			? wxHDR_SORT_ICON_UP
			: wxHDR_SORT_ICON_DOWN
			: wxHDR_SORT_ICON_NONE
		);
		rect.Deflate(2);
	}
	else
	{
		// It is reported that we need to erase the background to avoid display
		// artefacts, see #12055.
		wxDCBrushChanger setBrush(dc, m_colLabelWin->GetBackgroundColour());
		dc.DrawRectangle(rect);

		rend.DrawBorder(*this, dc, rect);
	}

	int hAlign, vAlign;
	GetColLabelAlignment(&hAlign, &vAlign);
	const int orient = GetColLabelTextOrientation();

	wxRect rectSection(colLeft - 1, 0, GetColWidth(col) + 1, m_sectionUpper->GetSize());

	wxString csSection;
	int nFind = m_sectionUpper->FindInSection(col, csSection);
	if (nFind & 2)
	{
		dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW)));
		dc.DrawLine(rectSection.GetLeft(), rectSection.GetBottom(), rectSection.GetLeft(), rectSection.GetTop());

		rend.DrawLabel(*this, dc, csSection,
			rectSection, hAlign, vAlign, wxHORIZONTAL);
	}

	dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW)));
	dc.DrawLine(rectSection.GetLeft(), rectSection.GetTop(), rectSection.GetRight(), rectSection.GetTop());

	if (nFind & 4)
	{
		dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW)));
		dc.DrawLine(rectSection.GetRight(), rectSection.GetTop(), rectSection.GetRight(), rectSection.GetBottom());
	}

	rend.DrawLabel(*this, dc, GetColLabelValue(col), rect, hAlign, vAlign, orient);
}

void CGrid::DrawRowLabels(wxDC& dc, const wxArrayInt& rows)
{
	if (!m_numRows)
		return;

	const size_t numLabels = rows.GetCount();
	for (size_t i = 0; i < numLabels; i++)
	{
		DrawRowLabel(dc, rows[i]);
	}
}

void CGrid::DrawRowLabel(wxDC& dc, int row)
{
	if (GetRowHeight(row) <= 0 || m_rowLabelWidth <= 0)
		return;

	wxGridCellAttrProvider * const
		attrProvider = m_table ? m_table->GetAttrProvider() : NULL;

	// notice that an explicit static_cast is needed to avoid a compilation
	// error with VC7.1 which, for some reason, tries to instantiate (abstract)
	// wxGridRowHeaderRenderer class without it
	const wxGridRowHeaderRenderer&
		rend = attrProvider ? attrProvider->GetRowHeaderRenderer(row)
		: static_cast<const wxGridRowHeaderRenderer&>
		(gs_defaultHeaderRenderers.rowRenderer);

	wxRect rect(m_sectionLeft->GetSize(), GetRowTop(row), m_rowLabelWidth - m_sectionLeft->GetSize(), GetRowHeight(row));
	rend.DrawBorder(*this, dc, rect);

	//wxRect rectSection(0, GetRowTop(row), m_sectionLeft->GetSize(), GetRowHeight(row));
	//rend.DrawBorder(*this, dc, rectSection);

	int hAlign, vAlign;
	GetRowLabelAlignment(&hAlign, &vAlign);

	wxRect rectSection(0, GetRowTop(row) - 1, m_sectionLeft->GetSize(), GetRowHeight(row) + 1);

	wxString csSection;
	int nFind = m_sectionLeft->FindInSection(row, csSection);

	if (nFind & 2)
	{
		dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW)));
		dc.DrawLine(rectSection.GetLeft(), rectSection.GetTop(), rectSection.GetRight(), rectSection.GetTop());

		rend.DrawLabel(*this, dc, csSection,
			rectSection, hAlign, vAlign, wxHORIZONTAL);
	}

	dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW)));
	dc.DrawLine(rectSection.GetLeft(), rectSection.GetTop(), rectSection.GetLeft(), rectSection.GetBottom());

	if (nFind & 4)
	{
		wxRect rectText = rectSection;

		dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW)));
		dc.DrawLine(rectSection.GetLeft(), rectSection.GetBottom(), rectSection.GetRight(), rectSection.GetBottom());
	}

	rend.DrawLabel(*this, dc, GetRowLabelValue(row),
		rect, hAlign, vAlign, wxHORIZONTAL);
}

CGrid::~CGrid()
{
	objectInspector->ClearProperty();
	//delete property
	wxDELETE(m_propertyCell);
}

wxGridCellCoords CGrid::GetCellFromPoint(wxPoint point)
{
	wxGridCellCoords cellID; // return value 

	// calculate column index
	int xpos = 0;
	int col = m_idTopLeftCell.GetCol() - 1;
	while (col < m_numCols)
	{
		xpos += wxGrid::GetColWidth(col);
		if (xpos > point.x) break;
		col++;
	}

	if (col >= m_numCols)
		cellID.SetCol(-1);
	else
		cellID.SetCol(col);

	// calculate row index
	int ypos = 0;
	int row = m_idTopLeftCell.GetRow() - 1;
	while (row < m_numRows)
	{
		ypos += wxGrid::GetRowHeight(row);
		if (ypos > point.y) break;
		row++;
	}

	if (row >= m_numCols)
		cellID.SetRow(-1);
	else
		cellID.SetRow(row);

	return cellID;
}

CGridCellRange CGrid::GetVisibleCellRange()
{
	wxRect rect = GetClientRect();

	// calc bottom
	int bottom = 0, i = 0;

	for (i = m_idTopLeftCell.GetRow(); i < m_numRows; i++)
	{
		bottom += wxGrid::GetRowHeight(i);
		if (bottom >= rect.GetBottom())
		{
			bottom = rect.GetBottom();
			break;
		}
	}

	int maxVisibleRow = std::min(i, m_numRows - 1);

	// calc right
	int right = 0;
	for (i = m_idTopLeftCell.GetCol(); i < m_numCols; i++)
	{
		right += wxGrid::GetColWidth(i);
		if (right >= rect.GetRight())
		{
			right = rect.GetRight();
			break;
		}
	}

	int maxVisibleCol = std::min(i, m_numCols - 1);
	return CGridCellRange(m_idTopLeftCell.GetRow(), m_idTopLeftCell.GetCol(), maxVisibleRow, maxVisibleCol);
}

CGridCellRange CGrid::GetSelectedCellRange()
{
	return CGridCellRange(m_topLeftSelectedCoords.GetRow(), m_topLeftSelectedCoords.GetCol(), m_bottomRightSelectedCoords.GetRow(), m_bottomRightSelectedCoords.GetCol());
}

void CGrid::OnMouseLeftDown(wxMouseEvent& event)
{
	if (!IsEditable()) return;

	int n = m_sectionLeft->GetNSectionFromPoint(event.GetPosition());
	if (n >= 0)
	{
		CSection section = m_sectionLeft->GetSection(n);
		SelectBlock(0, 0, section.nRangeTo, m_numCols - 1);
	}

	n = m_sectionLeft->GetNSectionFromPoint(event.GetPosition());
	if (n >= 0)
	{
		CSection section = m_sectionLeft->GetSection(n);
		SelectBlock(0, 0, section.nRangeTo, m_numCols - 1);
	}

	event.Skip();
}

void CGrid::OnMouseDClick(wxMouseEvent& event)
{
	if (!IsEditable()) return;

	int n = m_sectionLeft->GetNSectionFromPoint(event.GetPosition());
	if (m_sectionLeft->EditName(n))
	{
		m_rowLabelWin->Refresh();
	}

	n = m_sectionUpper->GetNSectionFromPoint(event.GetPosition());
	if (m_sectionUpper->EditName(n))
	{
		m_colLabelWin->Refresh();
	}

	event.Skip();
}

void CGrid::OnMouseWheel(wxMouseEvent& event)
{
	int maxCol = 0, maxRow = 0;

	if (maxCol < m_currentCellCoords.GetCol()) maxCol = m_currentCellCoords.GetCol();
	if (maxRow < m_currentCellCoords.GetRow()) maxRow = m_currentCellCoords.GetRow();

	switch (event.GetWheelAxis())
	{
	case wxMouseWheelAxis::wxMOUSE_WHEEL_VERTICAL: if (maxRow == this->GetNumberRows() - 1) this->GetTable()->AppendRows(); break;
	case wxMouseWheelAxis::wxMOUSE_WHEEL_HORIZONTAL: if (maxCol == this->GetNumberCols() - 1) this->GetTable()->AppendCols(); break;
	}
	event.Skip();
}

// This seems to be required for wxMotif/wxGTK otherwise the mouse
// cursor must be in the cell edit control to get key events
//
void CGrid::OnKeyDown(wxKeyEvent& event)
{
	int maxCol = 0, maxRow = 0;

	if (maxCol < m_currentCellCoords.GetCol()) maxCol = m_currentCellCoords.GetCol();
	if (maxRow < m_currentCellCoords.GetRow()) maxRow = m_currentCellCoords.GetRow();

	switch (event.GetKeyCode())
	{
	case WXK_DOWN: if (maxRow == this->GetNumberRows() - 1) this->GetTable()->AppendRows(); break;
	case WXK_RIGHT: if (maxCol == this->GetNumberCols() - 1) this->GetTable()->AppendCols(); break;
	}

	event.Skip();
}

void CGrid::OnKeyUp(wxKeyEvent& event)
{
	event.Skip();
}

void CGrid::OnSelectCell(wxGridEvent &event)
{
	m_propertyCell->SetReadOnly(IsEditable()); 

	if (event.Selecting())
	{
		wxGridCellCoords coords;
		coords.Set(event.GetRow(), event.GetCol());

		wxGridCellAttr *attr = m_table->GetAttr(event.GetRow(), event.GetCol(), wxGridCellAttr::Cell);

		m_topLeftSelectedCoords = coords;

		if (attr) {
			int m_sizeRows, m_sizeCols;
			attr->GetSize(&m_sizeRows, &m_sizeCols);
			m_bottomRightSelectedCoords.Set(coords.GetRow() + m_sizeRows - 1, coords.GetCol() + m_sizeCols - 1);

			m_propertyCell->SetCellCoords(m_topLeftSelectedCoords, m_bottomRightSelectedCoords);
		}
		else
		{
			m_bottomRightSelectedCoords = coords;
			m_propertyCell->SetCellCoords(coords);
		}

		objectInspector->SelectObject(m_propertyCell);
	}

	m_gridWin->Refresh(); 

	this->SetFocus();
	event.Skip();
}

void CGrid::OnSelectCells(wxGridRangeSelectEvent &event)
{
	m_propertyCell->SetReadOnly(IsEditable());

	if (event.Selecting())
	{
		auto m_colSelection = GetSelectedCols();
		auto m_rowSelection = GetSelectedRows();

		if (m_colSelection.size() || m_rowSelection.size())
		{
			int minCol = m_numCols - 1, minRow = m_numRows - 1;

			for (auto col : m_colSelection) { if (col - 1 < minCol) minCol = col; }
			for (auto row : m_rowSelection) { if (row - 1 < minRow) minRow = row; }

			if (m_colSelection.size() == 0) minCol = 0;
			if (m_rowSelection.size() == 0) minRow = 0;

			m_topLeftSelectedCoords = wxGridCellCoords(minRow, minCol);
		}
		else
		{
			m_topLeftSelectedCoords = event.GetTopLeftCoords();
		}

		m_bottomRightSelectedCoords = event.GetBottomRightCoords();

		m_propertyCell->SetCellCoords(m_topLeftSelectedCoords, m_bottomRightSelectedCoords);

		objectInspector->SelectObject(m_propertyCell);
	}

	m_gridWin->Refresh();
	this->SetFocus();
	event.Skip();
}

void CGrid::OnScroll(wxScrollWinEvent& event)
{
	int nVertScroll = GetScrollPos(wxVERTICAL) * WXGRID_MIN_ROW_HEIGHT,
		nHorzScroll = GetScrollPos(wxHORIZONTAL) * WXGRID_MIN_COL_WIDTH;

	m_idTopLeftCell.Set(1, 1);

	int nRight = 0;
	while (nRight < nHorzScroll && m_idTopLeftCell.GetCol() < m_numCols - 1)
	{
		int col = m_idTopLeftCell.GetCol();
		nRight += wxGrid::GetColWidth(col);
		m_idTopLeftCell.SetCol(++col);
	}

	int nTop = 0;
	while (nTop < nVertScroll && m_idTopLeftCell.GetRow() < (m_numRows - 1))
	{
		int row = m_idTopLeftCell.GetRow();
		nTop += wxGrid::GetRowHeight(row);
		m_idTopLeftCell.SetRow(++row);
	}

	event.Skip();
}

void CGrid::OnSize(wxSizeEvent& event)
{
	ViewSection();
	event.Skip();
}