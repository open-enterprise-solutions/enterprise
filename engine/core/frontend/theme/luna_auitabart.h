#ifndef __AUITABART_H__
#define __AUITABART_H__

#include <wx/aui/auibook.h>

class CLunaTabArt : public wxAuiTabArt {
public:
	CLunaTabArt();
	virtual ~CLunaTabArt();

	wxAuiTabArt* Clone() override;
	void SetFlags(unsigned int flags) override;
	void SetSizingInfo(const wxSize& tabCtrlSize,
		size_t tabCount) override;

	void SetNormalFont(const wxFont& font) override;
	void SetSelectedFont(const wxFont& font) override;
	void SetMeasuringFont(const wxFont& font) override;
	void SetColour(const wxColour& colour) override;
	void SetActiveColour(const wxColour& colour) override;

	void DrawBorder(
		wxDC& dc,
		wxWindow* wnd,
		const wxRect& rect) override;

	void DrawBackground(
		wxDC& dc,
		wxWindow* wnd,
		const wxRect& rect) override;

	void DrawTab(wxDC& dc,
		wxWindow* wnd,
		const wxAuiNotebookPage& pane,
		const wxRect& inRect,
		int closeButtonState,
		wxRect* outTabRect,
		wxRect* outButtonRect,
		int* xExtent) override;

	void DrawButton(
		wxDC& dc,
		wxWindow* wnd,
		const wxRect& inRect,
		int bitmapId,
		int buttonState,
		int orientation,
		wxRect* outRect) override;

	int GetIndentSize() override;

	int GetBorderWidth(
		wxWindow* wnd) override;

	int GetAdditionalBorderSpace(
		wxWindow* wnd) override;

	wxSize GetTabSize(
		wxDC& dc,
		wxWindow* wnd,
		const wxString& caption,
		const wxBitmap& bitmap,
		bool active,
		int closeButtonState,
		int* xExtent) override;

	int ShowDropDown(
		wxWindow* wnd,
		const wxAuiNotebookPageArray& items,
		int activeIdx) override;

	int GetBestTabCtrlSize(wxWindow* wnd,
		const wxAuiNotebookPageArray& pages,
		const wxSize& requiredBmpSize) override;

	// Provide opportunity for subclasses to recalculate colours
	virtual void UpdateColoursFromSystem() override;

protected:

	wxFont m_normalFont;
	wxFont m_selectedFont;
	wxFont m_measuringFont;
	wxColour m_baseColour;

	wxPen m_baseColourPen;
	wxPen m_borderPen;

	wxBrush m_baseColourBrush;
	wxColour m_activeColour;
	wxBitmap m_activeCloseBmp;
	wxBitmap m_disabledCloseBmp;
	wxBitmap m_activeLeftBmp;
	wxBitmap m_disabledLeftBmp;
	wxBitmap m_activeRightBmp;
	wxBitmap m_disabledRightBmp;
	wxBitmap m_activeWindowListBmp;
	wxBitmap m_disabledWindowListBmp;

	int m_fixedTabWidth;
	int m_tabCtrlHeight;
	unsigned int m_flags;
};

#endif // __AUITABART_H__
