#ifndef _GRIDCOMMON_H__
#define _GRIDCOMMON_H__

#include "gridSections.h"
#include "gridCellRange.h"
#include "property/gridProperty.h"

class CGrid : public wxGrid
{
	CPropertyCell *m_propertyCell;

private:

	wxGridCellCoords m_idTopLeftCell;
	CSectionCtrl *m_sectionLeft, *m_sectionUpper;

	wxGridCellCoords m_topLeftSelectedCoords;
	wxGridCellCoords m_bottomRightSelectedCoords;

	bool m_bViewSection;

protected:

	bool GetWorkSection(int &nRangeFrom, int &nRangeTo, CSectionCtrl *&pSectionCtrl);

	int GetRowHeight() const
	{
		int nHeight = 0;
		for (int i = 0; i < m_numRows; i++)
			nHeight += wxGrid::GetRowHeight(i);

		return nHeight;
	}

	int GetRowHeight(int row) const
	{
		return wxGrid::GetRowHeight(row);
	}

	int GetColWidth() const
	{
		int nWidth = 0;
		for (int i = 0; i < m_numCols; i++)
			nWidth += wxGrid::GetColWidth(i);

		return nWidth;
	}

	int GetColWidth(int col) const
	{
		return wxGrid::GetColWidth(col);
	}

	wxGridCellCoords GetCellFromPoint(wxPoint point);
	CGridCellRange GetVisibleCellRange();
	CGridCellRange GetSelectedCellRange();

	wxPoint GetHorizRange(const wxString &sSectionName)
	{
		return m_sectionLeft->GetRange(sSectionName);
	}

	wxPoint GetVertRange(const wxString &sSectionName)
	{
		return m_sectionUpper->GetRange(sSectionName);
	}

	friend class CSectionCtrl;
	friend class CGridSectionTable;

public:

	// ctor and Create() create the grid window, as with the other controls
	CGrid();
	CGrid(wxWindow *parent,
		wxWindowID id, const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize);

	void ViewSection();
	bool IsViewSection() { return m_bViewSection; };

	CSectionCtrl *GetSectionLeft() { return m_sectionLeft; }
	CSectionCtrl *GetSectionUpper() { return m_sectionUpper; }

	// it doesn't really change parent, use Reparent() instead
	virtual void SetParent(wxWindowBase *parent);
	// change the real parent of this window, return true if the parent
	// was changed, false otherwise (error or newParent == oldParent)
	virtual bool Reparent(wxWindowBase *newParent) override;

	void AddSection();
	void RemoveSection();

	void MergeCells();

	virtual void DrawCellHighlight(wxDC& dc, const wxGridCellAttr *attr) override;

	virtual void DrawCornerLabel(wxDC& dc) override;

	virtual void DrawColLabels(wxDC& dc, const wxArrayInt& cols) override;
	virtual void DrawColLabel(wxDC& dc, int col) override;
	virtual void DrawRowLabels(wxDC& dc, const wxArrayInt& rows) override;
	virtual void DrawRowLabel(wxDC& dc, int row) override;

	virtual ~CGrid();

protected:

	//events:
	void OnMouseLeftDown(wxMouseEvent& event);
	void OnMouseDClick(wxMouseEvent& event);
	void OnMouseWheel(wxMouseEvent& event);

	// This seems to be required for wxMotif/wxGTK otherwise the mouse
	// cursor must be in the cell edit control to get key events
	//
	void OnKeyDown(wxKeyEvent& event);
	void OnKeyUp(wxKeyEvent& event);

	void OnSelectCell(wxGridEvent &event);
	void OnSelectCells(wxGridRangeSelectEvent &event);

	void OnScroll(wxScrollWinEvent& event);
	void OnPaint(wxPaintEvent&event);
	void OnSize(wxSizeEvent& event);

	wxDECLARE_EVENT_TABLE();
};

#endif 