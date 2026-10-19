#ifndef __INNER_FRAME__
#define __INNER_FRAME__

#include <wx/panel.h>
#include <wx/bitmap.h>

// The form's card in the visual editor: a caption over the panel the form is built in, resized by
// dragging its right edge, its bottom edge or the corner between them.
class ibInnerFrame : public wxPanel {
public:

	ibInnerFrame(wxWindow* parent, wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = 0);

	wxPanel* GetFrameContentPanel() const { return m_content; }

	void SetTitle(const wxString& title);
	void SetTitleStyle(long style);
	void ShowTitleBar(bool show);

protected:

	virtual wxSize DoGetBestSize() const override;

private:

	// The edge the mouse holds while the card is resized.
	enum class Edge { None, Right, Bottom, Corner };

	Edge GetEdgeAt(const wxPoint& point) const;
	wxSize LimitSize(const wxSize& size) const;
	void EndSizing();

	void OnPaintTitleBar(wxPaintEvent& event);
	void OnTitleBarLeftDown(wxMouseEvent& event);

	void OnMouseMotion(wxMouseEvent& event);
	void OnLeftDown(wxMouseEvent& event);
	void OnLeftUp(wxMouseEvent& event);
	void OnMouseCaptureLost(wxMouseCaptureLostEvent& event);

	wxPanel* m_titleBar;
	wxPanel* m_content;

	wxString m_title;
	long m_titleStyle;
	wxBitmap m_closeButton;
	wxBitmap m_maximizeButton;
	wxBitmap m_minimizeButton;

	Edge m_sizing = Edge::None;
	wxSize m_grabOffset;   // from the pointer to the card's bottom-right corner, as grabbed
};

// Sent when a person has resized the card.
wxDECLARE_EVENT(wxEVT_INNER_FRAME_RESIZED, wxCommandEvent);

#define EVT_INNER_FRAME_RESIZED(id, fn) \
	wx__DECLARE_EVT1(wxEVT_INNER_FRAME_RESIZED, id, wxCommandEventHandler(fn))

#endif //__INNER_FRAME__
