#ifndef _LUNA_DOCKART_H__
#define _LUNA_DOCKART_H__

#include <wx/wx.h>
#include <wx/aui/aui.h>

// CLunaDockArt is an art provider class which does all of the drawing for
// wxAuiManager.  This allows the library caller to customize the dock art
// (probably by deriving from this class), or to completely replace all drawing
// with custom dock art (probably by writing a new stand-alone class derived
// from the wxAuiDockArt base class). The active dock art class can be set via
// wxAuiManager::SetDockArt()
wxColor wxAuiLightContrastColour(const wxColour& c);

// wxAuiBitmapFromBits() is a utility function that creates a
// masked bitmap from raw bits (XBM format)
wxBitmap wxAuiBitmapFromBits(const unsigned char bits[], int w, int h,
	const wxColour& color);

class CLunaDockArt : public wxAuiDockArt
{
public:

	CLunaDockArt();

	int GetMetric(int metricId) override;
	void SetMetric(int metricId, int newVal) override;
	wxColour GetColour(int id) override;
	void SetColour(int id, const wxColor& colour) override;
	void SetFont(int id, const wxFont& font) override;
	wxFont GetFont(int id) override;

	void DrawSash(wxDC& dc,
		wxWindow *window,
		int orientation,
		const wxRect& rect) override;

	void DrawBackground(wxDC& dc,
		wxWindow *window,
		int orientation,
		const wxRect& rect) override;

	void DrawCaption(wxDC& dc,
		wxWindow *window,
		const wxString& text,
		const wxRect& rect,
		wxAuiPaneInfo& pane) override;

	void DrawGripper(wxDC& dc,
		wxWindow *window,
		const wxRect& rect,
		wxAuiPaneInfo& pane) override;

	void DrawBorder(wxDC& dc,
		wxWindow *window,
		const wxRect& rect,
		wxAuiPaneInfo& pane) override;

	void DrawPaneButton(wxDC& dc,
		wxWindow *window,
		int button,
		int buttonState,
		const wxRect& rect,
		wxAuiPaneInfo& pane) override;

#if WXWIN_COMPATIBILITY_3_0
	wxDEPRECATED_MSG("This is not intended for the public API")
		void DrawIcon(wxDC& dc,
			const wxRect& rect,
			wxAuiPaneInfo& pane);
#endif

	virtual wxAuiDockArt* Clone() override
	{
		return new CLunaDockArt(*this);
	}

	virtual void UpdateColoursFromSystem() override;

protected:

	void DrawCaptionBackground(wxDC& dc, const wxRect& rect, bool active);
	void DrawIcon(wxDC& dc, wxWindow *window, const wxRect& rect, wxAuiPaneInfo& pane);
	void InitBitmaps();

protected:

	wxPen m_borderPen;
	wxPen m_sashPen;
	wxBrush m_sashBrush;
	wxBrush m_backgroundBrush;
	wxBrush m_gripperBrush;
	wxFont m_captionFont;
	wxBitmap m_inactiveCloseBitmap;
	wxBitmap m_inactivePinBitmap;
	wxBitmap m_inactiveMaximizeBitmap;
	wxBitmap m_inactiveRestoreBitmap;
	wxBitmap m_activeCloseBitmap;
	wxBitmap m_activePinBitmap;
	wxBitmap m_activeMaximizeBitmap;
	wxBitmap m_activeRestoreBitmap;
	wxPen m_gripperPen1;
	wxPen m_gripperPen2;
	wxPen m_gripperPen3;
	wxColour m_baseColour;
	wxColour m_activeCaptionColour;
	wxColour m_activeCaptionGradientColour;
	wxColour m_activeCaptionTextColour;
	wxColour m_inactiveCaptionColour;
	wxColour m_inactiveCaptionGradientColour;
	wxColour m_inactiveCaptionTextColour;
	int m_borderSize;
	int m_captionSize;
	int m_sashSize;
	int m_buttonSize;
	int m_gripperSize;
	int m_gradientType;
};

#endif 