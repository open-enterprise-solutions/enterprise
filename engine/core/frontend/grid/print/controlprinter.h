#ifndef _CONTROL_PRINTER_H_
#define _CONTROL_PRINTER_H_

#include <wx/wx.h>
#include <wx/print.h>
#include <wx/dynarray.h>
#include <wx/grid.h>
#include <wx/dc.h>
#include <wx/tokenzr.h>
#include <wx/textfile.h>

#define wxGP_SHOW_NONE		0x0000 //never show row or column labels
#define wxGP_SHOW_CL		0x0001 //show column labels on first page/s
#define wxGP_SHOW_RL		0x0002 //show row labels on first page/s
#define wxGP_SHOW_CL_ALWAYS 0x0004 //show column labels	on all pages
#define wxGP_SHOW_RL_ALWAYS 0x0008 //show row labels on all pages
#define wxGP_DEFAULT		0x0010 //default, shows all labels on all the pages.

class wxGridPrintout : public wxPrintout
{
	wxGrid* m_grid;
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

	int m_topMargin;
	int m_bottomMargin;
	int m_leftMargin;
	int m_rightMargin;

	//styles
	bool m_showCl;
	bool m_showRl;
	bool m_showClAlways;
	bool m_showRlAlways;

	//cols and rows brakes;
	wxArrayInt m_colBrakes;
	wxArrayInt m_rowBrakes;

public:

	wxGridPrintout(const wxString& title = wxT("wxGridPrintout"));
	wxGridPrintout(wxGrid* gridCtrl, int style = wxGP_SHOW_NONE, const wxString& title = wxT("wxGridPrintout"));

	void SetGrid(wxGrid* gridCtrl);
	wxGrid* GetGrid()const;
	void SetStyle(int style);
	int GetStyle()const;
	void SetUserScale(float scale);
	void AddColBrake(int col);
	void AddRowBrake(int row);
	bool IsColBrake(int col);
	bool IsRowBrake(int row);

	virtual bool OnPrintPage(int page);
	virtual bool HasPage(int page);
	virtual void GetPageInfo(int *minPage, int *maxPage, int *selPageFrom, int *selPageTo);
	virtual bool DrawPage(wxDC *dc, int page);

protected:

	virtual void OnPreparePrinting();
	void CalculateScale(wxDC* dc); //calculates the scale so that the printout represents the screen

protected:

	static void DrawTextInRectangle(wxDC& dc, wxString& str, wxRect& rect, wxFont& font, wxBrush& fontbrush, int horizAlign = wxALIGN_LEFT, int vertAlign = wxALIGN_TOP, int textOrientation = wxHORIZONTAL);
	static void DrawTextRectangle(wxDC& dc, const wxArrayString& lines, const wxRect& rect, int horizAlign = wxALIGN_LEFT, int vertAlign = wxALIGN_TOP, int textOrientation = wxHORIZONTAL);
	static void GetTextBoxSize(wxDC& dc, const wxArrayString& lines, long *width, long *height);
	static void StringToLines(const wxString& value, wxArrayString& lines);
	static wxArrayString GetTextLines(wxDC& dc, const wxString& data, wxFont& font, const wxRect& rect);

};

#endif // _CONTROL_PRINTER_H_
