#ifndef __IB_CALL_TIP_H__
#define __IB_CALL_TIP_H__

#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <wx/stc/stc.h>

#include "helpers/fontAscent.h"

class wxSTCCallTip;

/**
 */
class ibCallTip
{
public:

	ibCallTip(wxStyledTextCtrl* owner);
	~ibCallTip();

	// Disable copy — owns m_callTip / m_evtHandler.
	ibCallTip(const ibCallTip&) = delete;
	ibCallTip& operator=(const ibCallTip&) = delete;

	void PaintCT(wxDC& dc);
	void MouseClick(wxPoint pt);

	/// Is the auto calltip list displayed?
	bool Active() const;

	/// Setup the calltip and return a rectangle of the area required.
	void Show(int currentPos, const wxString& defn);
	void Cancel();

	/// Set a range of characters to be displayed in a highlight style.
	/// Commonly used to highlight the current parameter.
	void SetHighlight(int start, int end);

	/// Set the tab size in pixels for the call tip. 0 or -ve means no tab expand.
	void SetTabSize(int tabSz);

	/// Used to determine which STYLE_xxxx to use for call tip information
	bool UseStyleCallTip() const { return m_useStyleCallTip; }

	// Modify foreground and background colours
	void SetForeBack(const wxColour& fore, const wxColour& back);

	///Process event
	bool CallEvent(wxEvent& event);

	void OnKeyDown(wxKeyEvent& event);

	void OnProcessFocus(wxFocusEvent& event);
	void OnProcessChildFocus(wxChildFocusEvent& event);
	void OnProcessSize(wxSizeEvent& event);
	void OnProcessMouse(wxMouseEvent& event);

private:

	void DrawChunk(wxDC& dc, int& x, const char* s,
		int posStart, int posEnd, int ytext, wxRect rcClient,
		bool highlight, bool draw);
	int  PaintContents(wxDC& dc, bool draw);
	bool IsTabCharacter(const char& c) const;
	int  NextTabPos(int x) const;

	int               m_startHighlight   = 0;     // character offset to start and...
	int               m_endHighlight     = 0;     // ...end of highlighted text

	std::string       m_val;
	wxFontWithAscent  m_font;
	wxRect            m_rectUp           = wxRect(0, 0, 0, 0);  // last up arrow rectangle in the tip
	wxRect            m_rectDown         = wxRect(0, 0, 0, 0);  // last down arrow rectangle in the tip

	int               m_lineHeight       = 1;     // vertical line spacing
	int               m_offsetMain       = 0;     // The alignment point of the call tip
	int               m_tabSize          = 0;     // Tab size in pixels, <=0 no TAB expand
	bool              m_useStyleCallTip  = false; // if true, STYLE_CALLTIP should be used

	wxStyledTextCtrl* m_owner            = nullptr;

	wxSTCCallTip*     m_callTip          = nullptr;
	wxEvtHandler*     m_evtHandler       = nullptr;

	bool              m_inCallTipMode    = false;
	int               m_posStartCallTip  = 0;

	wxColour          m_colourBG;
	wxColour          m_colourUnSel;
	wxColour          m_colourSel;
	wxColour          m_colourShade;
	wxColour          m_colourLight;

	int               m_clickPlace       = 0;

	int               m_insetX           = 5;     // text inset in x from calltip border
	int               m_widthArrow       = 14;
	int               m_borderHeight     = 2;     // border + empty line at top & bottom
	int               m_verticalOffset   = 1;     // pixel offset of the calltip vs the line
};

#include "helpers/popupWnd.h"

class wxSTCCallTip : public ibCodeEditorPopupWindow
{
public:
	wxSTCCallTip(wxWindow* parent, ibCallTip* ct) :
		ibCodeEditorPopupWindow(parent), m_ct(ct)
	{
		Bind(wxEVT_LEFT_DOWN, &wxSTCCallTip::OnLeftDown, this);
		Bind(wxEVT_SIZE, &wxSTCCallTip::OnSize, this);
		Bind(wxEVT_PAINT, &wxSTCCallTip::OnPaint, this);

#ifdef __WXMSW__
		Bind(wxEVT_ERASE_BACKGROUND, &wxSTCCallTip::OnEraseBackground, this);
		SetBackgroundStyle(wxBG_STYLE_ERASE);
#else
		SetBackgroundStyle(wxBG_STYLE_PAINT);
#endif

		SetName("wxSTCCallTip");
	}

	void DrawBack(const wxSize& size)
	{
		m_back = wxBitmap(size);

		wxMemoryDC mem(m_back);
		m_ct->PaintCT(mem);
	}

	virtual void Refresh(bool eraseBg = true, const wxRect* rect = nullptr) override
	{
		if (rect == nullptr)
			DrawBack(GetSize());

		ibCodeEditorPopupWindow::Refresh(eraseBg, rect);
	}

	void OnLeftDown(wxMouseEvent& event)
	{
		wxPoint pt = event.GetPosition();
		wxPoint p(pt.x, pt.y);
		m_ct->MouseClick(p);
		//m_swx->CallTipClick();
	}

	void OnSize(wxSizeEvent& event)
	{
		DrawBack(event.GetSize());
		event.Skip();
	}

#ifdef __WXMSW__

	void OnPaint(wxPaintEvent& WXUNUSED(evt))
	{
		wxRect upd = GetUpdateClientRect();
		wxMemoryDC mem(m_back);
		wxPaintDC dc(this);

		dc.Blit(upd.GetX(), upd.GetY(), upd.GetWidth(), upd.GetHeight(), &mem,
			upd.GetX(), upd.GetY());
	}

	void OnEraseBackground(wxEraseEvent& event)
	{
		event.GetDC()->DrawBitmap(m_back, 0, 0);
	}

#else

	void OnPaint(wxPaintEvent& WXUNUSED(evt))
	{
		wxAutoBufferedPaintDC dc(this);
		dc.DrawBitmap(m_back, 0, 0);
	}

#endif // __WXMSW__

private:

	ibCallTip* m_ct;
	wxBitmap   m_back;
};

#endif
