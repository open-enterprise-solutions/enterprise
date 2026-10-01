#ifndef __CONTROL_PRINTER_H__
#define __CONTROL_PRINTER_H__

#include <wx/wx.h>
#include <wx/print.h>
#include <wx/dynarray.h>
#include <wx/dc.h>
#include <wx/tokenzr.h>
#include <wx/textfile.h>

#include "gridEditor.h"
#include "frontend/docView/print/printPreview.h"   // ibFitToPageWidthPrintout — what the preview's box asks of it

#define wxGP_SHOW_NONE		0x0000 //never show row or column labels
#define wxGP_SHOW_CL		0x0001 //show column labels on first page/s
#define wxGP_SHOW_RL		0x0002 //show row labels on first page/s
#define wxGP_SHOW_CL_ALWAYS 0x0004 //show column labels	on all pages
#define wxGP_SHOW_RL_ALWAYS 0x0008 //show row labels on all pages
#define wxGP_DEFAULT		0x0010 //default, shows all labels on all the pages.

class FRONTEND_API ibGridEditorPrintout : public wxPrintout, public ibFitToPageWidthPrintout {
public:

	ibGridEditorPrintout(const wxString& title = wxT("ibGridEditorPrintout"));
	ibGridEditorPrintout(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc, int style = wxGP_SHOW_NONE, const wxString& title = wxT("ibGridEditorPrintout"));

	void SetStyle(int style);
	int GetStyle() const;
	void SetUserScale(float scale);

	// FIT TO THE PAGE'S WIDTH: a table wider than the page is shrunk until it goes across in one page;
	// a narrower one is left at its size. Rows follow the same scale, so more of them fit on a page.
	virtual void SetFitToPageWidth(bool fit) override { m_fitToPageWidth = fit; }
	virtual bool IsFitToPageWidth() const override { return m_fitToPageWidth; }
	virtual wxPrintout* Clone() const override;

	bool HasColValue(int col) const {
		for (int row = 0; row < m_doc->GetNumberRows(); row++) {
			if (m_doc->IsEmptyCell(row, col))
				return true;
		}
		return false;
	}

	bool HasRowValue(int row) const {
		for (int col = 0; col < m_doc->GetNumberCols(); col++) {
			if (m_doc->IsEmptyCell(row, col))
				return true;
		}
		return false;
	}

	virtual bool OnPrintPage(int page);
	virtual bool HasPage(int page);
	virtual void GetPageInfo(int* minPage, int* maxPage, int* selPageFrom, int* selPageTo);
	virtual bool DrawPage(wxDC* dc, int page);

protected:

	virtual void OnPreparePrinting();
	void CalculateScale(wxDC* dc); //calculates the scale so that the printout represents the screen

	// The page as this DC has it: the scale (the screen's, the user's, the fitting's), the margin and the
	// room between the margins. Asked by BOTH the pagination and the drawing, so the two cannot disagree -
	// the preview's DC and the printer's are different sizes, and each is laid out against its own.
	void ArrangePage(wxDC* dc);

	// How wide the content is, in the units the sheet is measured in - the row labels when they are
	// printed, and every column of the sheet, the last one included.
	int ContentWidth() const;

protected:

	// `wrap` is the CELL's own placement, asked of the document — the printout reads the document,
	// not the grid, so it has to be told here. It is the last parameter and defaults to off because
	// the column and row headers drawn through this never wrap.
	static void DrawTextInRectangle(wxDC& dc, const wxString& strValue, wxRect& rect, const wxFont& font, const wxColour& fontClr, int horizAlign = wxALIGN_LEFT, int vertAlign = wxALIGN_TOP, int textOrientation = wxHORIZONTAL, bool wrap = false);

	// WHAT THE CELL SAYS — the one place that turns a stored value into the text on paper.
	wxString CellText(int row, int col) const;

	// WHERE A PAGE MAY BREAK — the row/column asked for, or the start of the merged block it fell
	// inside of. A break has to be a place where nothing is torn in half.
	int RowBreakAt(int row) const;
	int ColBreakAt(int col) const;

	// THE HEIGHT A ROW PRINTS AT — the one the screen shows it at (ibSpreadsheetRowHeight): its own, or its
	// automatic height. Worked out once per printing (OnPreparePrinting), for the pagination and the drawing.
	int RowSize(int row) const;

private:

	wxObjectDataPtr<ibBackendSpreadsheetObject> m_doc;

	std::vector<int> m_rowHeights;

	int m_style;

	int m_minPage;
	int m_maxPage;
	int m_selPageFrom;
	int m_selPageTo;
	int m_maxWidth;
	int m_maxHeight;
	wxArrayInt m_rowsPerPage;
	wxArrayInt m_colsPerPage;
	float m_screenScale;
	float m_userScale;
	float m_overallScale;

	bool m_fitToPageWidth = false;
	float m_fitScale = 1.0f;      // below 1 only while fitting a table wider than the page

	// In the sheet's units (a screen pixel); ArrangePage scales them to the printer, never by the fitting.
	int m_topMargin;
	int m_bottomMargin;
	int m_leftMargin;
	int m_rightMargin;

	//styles
	bool m_showCl;
	bool m_showRl;
	bool m_showClAlways;
	bool m_showRlAlways;
};

#endif // _CONTROL_PRINTER_H_
