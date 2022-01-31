#ifndef CALLTIP_H
#define CALLTIP_H

#include <wx/wx.h>
#include <wx/stc/stc.h>

#include "helpers/fontAscent.h"

class wxSTCCallTip;

/**
 */
class CCallTip 
{
	int startHighlight;    // character offset to start and...
	int endHighlight;      // ...end of highlighted text

	std::string val;
	wxFontWithAscent font;
	wxRect rectUp;      // rectangle of last up angle in the tip
	wxRect rectDown;    // rectangle of last down arrow in the tip

	int lineHeight;         // vertical line spacing
	int offsetMain;         // The alignment point of the call tip
	int tabSize;            // Tab size in pixels, <=0 no TAB expand
	bool useStyleCallTip;   // if true, STYLE_CALLTIP should be used

	wxStyledTextCtrl *m_owner;

	// Private so CCallTip objects can not be copied
	CCallTip(const CCallTip &);

	void DrawChunk(wxDC &dc, int &x, const char *s,
		int posStart, int posEnd, int ytext, wxRect rcClient,
		bool highlight, bool draw);
	int PaintContents(wxDC &dc, bool draw);
	bool IsTabCharacter(char c) const;
	int NextTabPos(int x) const;

public:
	
	wxSTCCallTip *m_callTip;
	wxEvtHandler *m_evtHandler;

	bool inCallTipMode;
	int posStartCallTip;
	wxColour colourBG;
	wxColour colourUnSel;
	wxColour colourSel;
	wxColour colourShade;
	wxColour colourLight;

	int clickPlace;

	int insetX; // text inset in x from calltip border
	int widthArrow;
	int borderHeight;
	int verticalOffset; // pixel offset up or down of the calltip with respect to the line

	CCallTip(wxStyledTextCtrl *owner);
	~CCallTip();

	void PaintCT(wxDC &dc);

	void MouseClick(wxPoint pt);

	/// Is the auto calltip list displayed?
	bool Active() const;
	
	/// Setup the calltip and return a rectangle of the area required.
	void Show(int currentPos, const wxString &defn);
	void Cancel();

	/// Set a range of characters to be displayed in a highlight style.
	/// Commonly used to highlight the current parameter.
	void SetHighlight(int start, int end);

	/// Set the tab size in pixels for the call tip. 0 or -ve means no tab expand.
	void SetTabSize(int tabSz);

	/// Used to determine which STYLE_xxxx to use for call tip information
	bool UseStyleCallTip() const { return useStyleCallTip; }

	// Modify foreground and background colours
	void SetForeBack(const wxColour &fore, const wxColour &back);

	///Process event 
	bool CallEvent(wxEvent& event);

	void OnKeyDown(wxKeyEvent& event);

	void OnProcessFocus(wxFocusEvent& event);
	void OnProcessChildFocus(wxChildFocusEvent& event);
	void OnProcessSize(wxSizeEvent& event);
	void OnProcessMouse(wxMouseEvent& event);
};

#include "helpers/popupWnd.h"

class wxSTCCallTip : public wxSTCPopupWindow 
{
public:
	wxSTCCallTip(wxWindow* parent, CCallTip* ct) :
		wxSTCPopupWindow(parent), m_ct(ct)
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

	virtual void Refresh(bool eraseBg = true, const wxRect *rect = NULL) override
	{
		if (rect == NULL)
			DrawBack(GetSize());

		wxSTCPopupWindow::Refresh(eraseBg, rect);
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

	CCallTip*      m_ct;
	wxBitmap      m_back;
};

#endif
