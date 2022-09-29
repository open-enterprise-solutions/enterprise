#ifndef _GRID_COMMON_H__
#define _GRID_COMMON_H__

#include "gridSections.h"
#include "property/gridProperty.h"

class CGrid : public wxGrid {
	CGridPropertyObject* m_gridProperty;
private:
	struct borderData_t {

		int m_borderWidth;
		wxPenStyle m_borderStyle;

		borderData_t(const wxPenStyle& borderStyle = wxPenStyle::wxPENSTYLE_TRANSPARENT) :
			m_borderStyle(borderStyle), m_borderWidth(1) {}
		borderData_t(const wxPenStyle& borderStyle, int borderWidth) :
			m_borderStyle(borderStyle), m_borderWidth(borderWidth) {}

		operator wxPenStyle() const {
			return m_borderStyle;
		}
	};
	struct gridAttrData_t : public wxClientData {
		wxOrientation m_textOrient = wxOrientation::wxHORIZONTAL;
		borderData_t m_leftBorder = wxPenStyle::wxPENSTYLE_TRANSPARENT;
		borderData_t m_rightBorder = wxPenStyle::wxPENSTYLE_TRANSPARENT;
		borderData_t m_topBorder = wxPenStyle::wxPENSTYLE_TRANSPARENT;
		borderData_t m_bottomBorder = wxPenStyle::wxPENSTYLE_TRANSPARENT;
		wxColour m_borderColour = *wxBLACK;
	};

	CSectionCtrl* m_sectionLeft, * m_sectionUpper;
	//cols and rows brakes;
	wxArrayInt m_colBrakes;
	wxArrayInt m_rowBrakes;
	bool m_sectionEnabled;
protected:

	bool GetWorkSection(int& nRangeFrom, int& nRangeTo, CSectionCtrl*& sectionCtrl);

	void EraseBrake() {
		EraseColBrake();
		EraseRowBrake();
	}

	void EraseColBrake() {
		m_colBrakes.Clear();
	}

	void EraseRowBrake() {
		m_rowBrakes.Clear();
	}

	// get the col/row coords
	int GetColWidth(int col) const {
		return wxGrid::GetColWidth(col);
	}

	int GetColLeft(int col) const {
		return wxGrid::GetColLeft(col);
	}

	int GetColRight(int col) const {
		return wxGrid::GetColRight(col);
	}

	// this function must be public for compatibility...
	int GetRowHeight() const
	{
		int nHeight = 0;
		for (int i = 0; i < m_numRows; i++)
			nHeight += wxGrid::GetRowHeight(i);

		return nHeight;
	}

	int GetRowHeight(int row) const {
		return wxGrid::GetRowHeight(row);
	}

	int GetRowTop(int row) const {
		return wxGrid::GetRowTop(row);
	}

	int GetRowBottom(int row) const {
		return wxGrid::GetRowBottom(row);
	}

	wxGridBlockCoords CGrid::GetSelectedCellRange() const
	{
		int row1 = m_numRows, col1 = m_numCols,
			row2 = 0, col2 = 0; bool hasBlocks = false;

		for (auto& coords : wxGrid::GetSelectedBlocks()) {
			if (row1 > coords.GetTopRow()) row1 = coords.GetTopRow();
			if (col1 > coords.GetLeftCol()) col1 = coords.GetLeftCol();
			if (row2 < coords.GetBottomRow()) row2 = coords.GetBottomRow();
			if (col2 < coords.GetRightCol()) col2 = coords.GetRightCol();
			hasBlocks = true;
		}

		if (!hasBlocks) {
			if (row1 > wxGrid::GetGridCursorRow()) row1 = wxGrid::GetGridCursorRow();
			if (col1 > wxGrid::GetGridCursorCol()) col1 = wxGrid::GetGridCursorCol();
			if (row2 < wxGrid::GetGridCursorRow()) row2 = wxGrid::GetGridCursorRow();
			if (col2 < wxGrid::GetGridCursorCol()) col2 = wxGrid::GetGridCursorCol();
		}

		return wxGridBlockCoords(
			row1, col1,
			row2, col2
		);
	}

	wxGridCellCoords GetCellFromPoint(const wxPoint& point) const;

	wxPoint GetHorizRange(const wxString& sectionName) const {
		return m_sectionLeft->GetRange(sectionName);
	}

	wxPoint GetVertRange(const wxString& sectionName) const {
		return m_sectionUpper->GetRange(sectionName);
	}

	friend class CSectionCtrl;
	friend class CGridSectionTable;

	gridAttrData_t* GetClientAttr(int row, int col) const {
		wxGridCellAttr* attr = GetOrCreateCellAttr(row, col);
		return dynamic_cast<gridAttrData_t*>(attr->GetClientObject());
	}

	gridAttrData_t* CreateAndGetClientAttr(int row, int col) const {
		gridAttrData_t* gridAttr = GetClientAttr(row, col);
		if (gridAttr == NULL) {
			gridAttr = new gridAttrData_t;
			wxGridCellAttr* attr = GetOrCreateCellAttr(row, col);
			attr->SetClientObject(gridAttr);
		}
		return gridAttr;
	}

	void CalcSection();

public:

	void SendPropertyModify() {
		wxGrid::SendEvent(wxEVT_GRID_EDITOR_HIDDEN, m_currentCellCoords);
	}

	void SendPropertyModify(const wxGridCellCoords& coords) {
		wxGrid::SendEvent(wxEVT_GRID_EDITOR_HIDDEN, coords);
	}

	//support printing 
	void AddBrake(int col, int row, bool erase = true) {
		if (erase)
			EraseBrake();
		m_colBrakes.Add(col);
		m_rowBrakes.Add(row);
	}

	void AddColBrake(int col) {
		m_colBrakes.Add(col);
	}

	void AddRowBrake(int row) {
		m_rowBrakes.Add(row);
	}

	bool IsColBrake(int col) const {
		for (unsigned int i = 0; i < m_colBrakes.Count(); i++) {
			if (col == m_colBrakes.Item(i))
				return true;
		}
		return false;
	}

	bool IsRowBrake(int row) const {
		for (unsigned int i = 0; i < m_rowBrakes.Count(); i++) {
			if (row == m_rowBrakes.Item(i))
				return true;
		}
		return false;
	}

	bool IsEmptyCell(int row, int col) const {
		wxGridCellAttrPtr& attr = GetCellAttrPtr(row, col);
		if (m_defaultCellAttr != attr.get())
			return false;
		return m_table ? m_table->IsEmptyCell(row, col) : true;
	}

	bool FreezeTo(int row, int col);
	bool FreezeTo(const wxGridCellCoords& coords) {
		return FreezeTo(coords.GetRow(), coords.GetCol());
	}

	////////////////////////////////////////////////////////////

	void SetCellTextOrientation(int row, int col, const wxOrientation& orient);
	wxOrientation GetCellTextOrientation(int row, int col) const;

	void SetCellBorder(int row, int col, const wxPenStyle& left, const wxPenStyle& right, const wxPenStyle& top, const wxPenStyle& bottom, const wxColour& borderColour);
	void GetCellBorder(int row, int col, wxPenStyle* left, wxPenStyle* right, wxPenStyle* top, wxPenStyle* bottom, wxColour* colour) const;

	////////////////////////////////////////////////////////////

	CSectionCtrl* GetSectionLeft() const {
		return m_sectionLeft;
	}

	CSectionCtrl* GetSectionUpper() const {
		return m_sectionUpper;
	}

	////////////////////////////////////////////////////////////

	// ctor and Create() create the grid window, as with the other controls
	CGrid();
	CGrid(wxWindow* parent,
		wxWindowID id, const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize);

	virtual ~CGrid();

	// change the real parent of this window, return true if the parent
	// was changed, false otherwise (error or newParent == oldParent)
	virtual bool Reparent(wxWindowBase* newParent) override {
		CalcSection();
		return wxGrid::Reparent(newParent);
	}

	void AddSection();
	void RemoveSection();

	void ShowCells() {
		wxGrid::EnableGridLines(!m_gridLinesEnabled);
	}

	void ShowHeader() {
		if (m_rowLabelWidth > 0) {
			wxGrid::SetRowLabelSize(0);
			wxGrid::SetColLabelSize(0);
		}
		else {
			wxGrid::SetRowLabelSize(WXGRID_DEFAULT_ROW_LABEL_WIDTH - 42);
			wxGrid::SetColLabelSize(WXGRID_MIN_COL_WIDTH);
			CalcSection();
		}
	}

	void ShowSection() {
		m_sectionEnabled = !m_sectionEnabled;
		CalcSection();
	}

	void MergeCells();
	void DockTable();

	void Copy();
	void Paste();

	virtual void DrawCellHighlight(wxDC& dc, const wxGridCellAttr* attr) override;
	virtual void DrawCornerLabel(wxDC& dc) override;

	virtual void DrawColLabels(wxDC& dc, const wxArrayInt& cols) override;
	virtual void DrawColLabel(wxDC& dc, int col) override;
	virtual void DrawRowLabels(wxDC& dc, const wxArrayInt& rows) override;
	virtual void DrawRowLabel(wxDC& dc, int row) override;

protected:

	friend class CGridCellSectionRenderer;

	//events:
	void OnMouseLeftDown(wxGridEvent& event);
	void OnMouseLeftDClick(wxGridEvent& event);
	void OnMouseRightDown(wxGridEvent& event);

	// This seems to be required for wxMotif/wxGTK otherwise the mouse
	// cursor must be in the cell edit control to get key events
	//
	void OnKeyDown(wxKeyEvent& event);
	void OnKeyUp(wxKeyEvent& event);

	void OnSelectCell(wxGridEvent& event);
	void OnSelectCells(wxGridRangeSelectEvent& event);

	void OnGridColSize(wxGridSizeEvent& event);
	void OnGridRowSize(wxGridSizeEvent& event);

	void OnGridEditorHidden(wxGridEvent& event);

	void OnCopy(wxCommandEvent& event);
	void OnPaste(wxCommandEvent& event);
	void OnDelete(wxCommandEvent& event);

	void OnRowHeight(wxCommandEvent& event);
	void OnColWidth(wxCommandEvent& event);
	void OnHideCell(wxCommandEvent& event);
	void OnShowCell(wxCommandEvent& event);

	void OnProperties(wxCommandEvent& event);

	void OnPaint(wxPaintEvent& event);

	void OnScroll(wxScrollWinEvent& event);
	void OnSize(wxSizeEvent& event);

	wxDECLARE_EVENT_TABLE();
};

#endif 