#include "callTip.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <stdexcept>
#include <string>

#include <wx/display.h>

inline int RoundXYPosition(float xyPos) {
	return int(xyPos + 0.5);
}

#define EXTENT_TEST wxT(" `~!@#$%^&*()-_=+\\|[]{};:\"\'<,>.?/1234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")

ibCallTip::ibCallTip(wxStyledTextCtrl* owner)
	: m_owner(owner)
{
#ifdef __APPLE__
	// proper apple colours for the default
	m_colourBG    = wxColour(0xff, 0xff, 0xc6);
	m_colourUnSel = wxColour(0, 0, 0);
#else
	m_colourBG    = wxColour(0xff, 0xff, 0xff);
	m_colourUnSel = wxColour(0x80, 0x80, 0x80);
#endif

	m_colourSel    = wxColour(0, 0, 0x80);
	m_colourShade  = wxColour(0, 0, 0);
	m_colourLight  = wxColour(0xc0, 0xc0, 0xc0);
}

ibCallTip::~ibCallTip()
{
	if (m_callTip) m_callTip->Destroy();
}

// Although this test includes 0, we should never see a \0 character.
static bool IsArrowCharacter(char ch)
{
	return (ch == 0) || (ch == '\001') || (ch == '\002');
}

// We ignore tabs unless a tab width has been set.
bool ibCallTip::IsTabCharacter(const char& ch) const
{
	return (m_tabSize > 0) && (ch == '\t');
}

int ibCallTip::NextTabPos(int x) const
{
	if (m_tabSize > 0) {              // paranoia... not called unless this is true
		x -= m_insetX;                // position relative to text
		x = (x + m_tabSize) / m_tabSize;  // tab "number"
		return m_tabSize * x + m_insetX;  // position of next tab
	}
	else {
		return x + 1;                 // arbitrary
	}
}

// Draw a section of the call tip that does not include \n in one colour.
// The text may include up to numEnds tabs or arrow characters.
void ibCallTip::DrawChunk(wxDC& dc, int& x, const char* s,
	int posStart, int posEnd, int ytext, wxRect rcClient,
	bool highlight, bool draw)
{
	s += posStart;
	int len = posEnd - posStart;

	// Divide the text into sections that are all text, or that are
	// single arrows or single tab characters (if tabSize > 0).
	int maxEnd = 0;
	const int numEnds = 10;
	int ends[numEnds + 2];
	for (int i = 0; i < len; i++)
	{
		if ((maxEnd < numEnds) &&
			(IsArrowCharacter(s[i]) || IsTabCharacter(s[i])))
		{
			if (i > 0)
				ends[maxEnd++] = i;
			ends[maxEnd++] = i + 1;
		}
	}
	ends[maxEnd++] = len;
	int startSeg = 0;
	int xEnd;
	for (int seg = 0; seg < maxEnd; seg++)
	{
		int endSeg = ends[seg];
		if (endSeg > startSeg) {
			if (IsArrowCharacter(s[startSeg])) {
				xEnd = x + m_widthArrow;
				bool upArrow = s[startSeg] == '\001';
				rcClient.SetLeft(x);
				rcClient.SetRight(xEnd);
				if (draw) {
					const int halfWidth = m_widthArrow / 2 - 3;
					const int quarterWidth = halfWidth / 2;
					const int centreX = x + m_widthArrow / 2 - 1;
					const int centreY = static_cast<int>(rcClient.GetTop() + rcClient.GetBottom()) / 2;

					dc.SetBrush(wxBrush(m_colourBG));
					dc.SetPen(*wxTRANSPARENT_PEN);
					dc.DrawRectangle(rcClient);

					wxRect rcClientInner(rcClient.GetLeft() + 1, rcClient.GetTop() + 1,
						rcClient.GetRight() - 2, rcClient.GetBottom() - 1);

					dc.SetBrush(wxBrush(m_colourUnSel));
					dc.SetPen(*wxTRANSPARENT_PEN);
					dc.DrawRectangle(rcClientInner);

					if (upArrow) {      // Up arrow
						wxPoint pts[] = {
							wxPoint(centreX - halfWidth, centreY + quarterWidth),
							wxPoint(centreX + halfWidth, centreY + quarterWidth),
							wxPoint(centreX, centreY - halfWidth + quarterWidth),
						};

						dc.SetPen(wxPen(m_colourBG));
						dc.SetBrush(wxBrush(m_colourBG));
						dc.DrawPolygon(3, pts);
					}
					else {            // Down arrow
						wxPoint pts[] = {
							wxPoint(centreX - halfWidth, centreY - quarterWidth),
							wxPoint(centreX + halfWidth, centreY - quarterWidth),
							wxPoint(centreX, centreY + halfWidth - quarterWidth),
						};

						dc.SetPen(wxPen(m_colourBG));
						dc.SetBrush(wxBrush(m_colourBG));
						dc.DrawPolygon(3, pts);

					}
				}
				m_offsetMain = xEnd;
				if (upArrow)
				{
					m_rectUp = rcClient;
				}
				else
				{
					m_rectDown = rcClient;
				}
			}
			else if (IsTabCharacter(s[startSeg]))
			{
				xEnd = NextTabPos(x);
			}
			else
			{
				dc.SetFont(m_font);
				int w, h;
				dc.GetTextExtent(wxString::FromUTF8(s + startSeg, endSeg - startSeg), &w, &h);

				xEnd = x + RoundXYPosition(w);

				if (draw)
				{
					rcClient.SetLeft(x);
					rcClient.SetRight(xEnd);

					dc.SetFont(m_font);
					dc.SetTextForeground(highlight ? m_colourSel : m_colourUnSel);
					dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);

					// ybase is where the baseline should be, but wxWin uses the upper left
					// corner, so I need to calculate the real position for the text...
					dc.DrawText(wxString::FromUTF8(s, len), rcClient.GetLeft(), static_cast<float>(ytext) - m_font.GetAscent());
					dc.SetBackgroundMode(wxBRUSHSTYLE_SOLID);
				}
			}
			x = xEnd;
			startSeg = endSeg;
		}
	}
}

int ibCallTip::PaintContents(wxDC& dc, bool draw)
{
	wxSize sz = m_callTip->GetClientSize();
	wxRect rcClientPos(0, 0, sz.x, sz.y);

	wxRect rcClientSize(0.0f, 0.0f, rcClientPos.GetRight() - rcClientPos.GetLeft(),
		rcClientPos.GetBottom() - rcClientPos.GetTop());
	wxRect rcClient(0.0f, 0.0f, rcClientSize.GetRight(), rcClientSize.GetBottom());

	// To make a nice small call tip wxWindow, it is only sized to fit most normal characters without accents
	int ascent = RoundXYPosition(m_font.GetAscent() - 0);

	// For each line...
	// Draw the definition in three parts: before highlight, highlighted, after highlight
	int ytext = static_cast<int>(rcClient.GetTop()) + ascent + 1;

	dc.SetFont(m_font);

	int w, h, d, e;
	dc.GetTextExtent(EXTENT_TEST, &w, &h, &d, &e);

	rcClient.SetBottom(ytext + d + 1);

	const char* chunkVal = m_val.c_str();
	bool moreChunks = true;
	int maxWidth = 0;

	while (moreChunks)
	{
		const char* chunkEnd = strchr(chunkVal, '\n');
		if (chunkEnd == nullptr)
		{
			chunkEnd = chunkVal + strlen(chunkVal);
			moreChunks = false;
		}
		int chunkOffset = static_cast<int>(chunkVal - m_val.c_str());
		int chunkLength = static_cast<int>(chunkEnd - chunkVal);
		int chunkEndOffset = chunkOffset + chunkLength;
		int thisStartHighlight = std::max(m_startHighlight, chunkOffset);
		thisStartHighlight = std::min(thisStartHighlight, chunkEndOffset);
		thisStartHighlight -= chunkOffset;
		int thisEndHighlight = std::max(m_endHighlight, chunkOffset);
		thisEndHighlight = std::min(thisEndHighlight, chunkEndOffset);
		thisEndHighlight -= chunkOffset;
		rcClient.SetTop(ytext - ascent - 1);

		int x = m_insetX;     // start each line at this inset

		DrawChunk(dc, x, wxString(chunkVal), 0, thisStartHighlight,
			ytext, rcClient, false, draw);
		DrawChunk(dc, x, wxString(chunkVal), thisStartHighlight, thisEndHighlight,
			ytext, rcClient, true, draw);
		DrawChunk(dc, x, wxString(chunkVal), thisEndHighlight, chunkLength,
			ytext, rcClient, false, draw);

		chunkVal = chunkEnd + 1;
		ytext += m_lineHeight;
		rcClient.SetBottom(rcClient.GetBottom() + m_lineHeight);
		maxWidth = std::max(maxWidth, x);
	}

	return maxWidth;
}

void ibCallTip::PaintCT(wxDC& dc)
{
	if (m_val.empty())
		return;

	// Fill the entire backing bitmap. The previous version threaded the
	// client size through wxRect::GetRight()/GetBottom(), which return
	// `x + w - 1` (last pixel inside), and treated those as width/height —
	// the resulting fill was (sz.x-2) × (sz.y-2), leaving a 2-pixel-wide
	// black strip at the bottom and right edge of the bitmap (wxBitmap
	// is default-initialized to black on MSW GDI).
	const wxSize sz = m_callTip->GetClientSize();

	dc.SetBrush(wxBrush(m_colourBG));
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRectangle(0, 0, sz.x, sz.y);

	m_offsetMain = m_insetX;    // initial alignment assuming no arrows
	PaintContents(dc, true);

#ifndef __APPLE__

	// OSX doesn't put borders on "help tags".
	// Draw a 1-pixel border on the inner edge of the bitmap. xMax/yMax
	// are the last-pixel-inside coordinates — wxDC::DrawLine takes a
	// pixel range and is inclusive of both endpoints.
	const int xMax = sz.x - 1;
	const int yMax = sz.y - 1;

	dc.SetPen(wxPen(m_colourLight));
	dc.DrawLine(0,    yMax, xMax, yMax);  // bottom
	dc.DrawLine(xMax, yMax, xMax, 0);     // right
	dc.DrawLine(xMax, 0,    0,    0);     // top
	dc.DrawLine(0,    0,    0,    yMax);  // left

#endif
}

void ibCallTip::MouseClick(wxPoint pt)
{
	m_clickPlace = 0;

	if (m_rectUp.Contains(pt))
		m_clickPlace = 1;

	if (m_rectDown.Contains(pt))
		m_clickPlace = 2;
}

bool ibCallTip::Active() const
{
	return m_inCallTipMode;
}

void ibCallTip::Show(int currentPos, const wxString& sTitleText)
{
	if (m_callTip) {
		wxDELETE(m_callTip);
		wxDELETE(m_evtHandler);
	}

	m_clickPlace = 0;
	m_val = sTitleText;

	m_callTip = new wxSTCCallTip(m_owner, this);

	m_evtHandler = new wxEvtHandler();
	m_evtHandler->Bind(wxEVT_KEY_DOWN, &ibCallTip::OnKeyDown, this);

	//focus kill/set
	m_evtHandler->Bind(wxEVT_SET_FOCUS, &ibCallTip::OnProcessFocus, this);
	m_evtHandler->Bind(wxEVT_KILL_FOCUS, &ibCallTip::OnProcessFocus, this);
	m_evtHandler->Bind(wxEVT_CHILD_FOCUS, &ibCallTip::OnProcessChildFocus, this);

	//on sizing
	m_evtHandler->Bind(wxEVT_SIZE, &ibCallTip::OnProcessSize, this);
	m_evtHandler->Bind(wxEVT_SIZING, &ibCallTip::OnProcessSize, this);

	//on mouse event
	m_evtHandler->Bind(wxEVT_LEFT_DCLICK, &ibCallTip::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_RIGHT_DCLICK, &ibCallTip::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_MIDDLE_DCLICK, &ibCallTip::OnProcessMouse, this);

	m_evtHandler->Bind(wxEVT_LEFT_UP, &ibCallTip::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_RIGHT_UP, &ibCallTip::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_MIDDLE_UP, &ibCallTip::OnProcessMouse, this);

	m_evtHandler->Bind(wxEVT_LEFT_DOWN, &ibCallTip::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_RIGHT_DOWN, &ibCallTip::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_MIDDLE_DOWN, &ibCallTip::OnProcessMouse, this);

	wxPoint pt = m_owner->PointFromPosition(currentPos);
	int textHeight = m_owner->TextHeight(m_owner->GetCurrentLine());

	wxBitmap bitmap(1, 1);
	wxMemoryDC dc(bitmap);

	m_startHighlight = 0;
	m_endHighlight = 0;
	m_inCallTipMode = true;
	m_posStartCallTip = currentPos;

	// Match the editor's currently displayed font size (base + zoom delta);
	// see ibAutoComplete::Start for the same pattern.
	wxFont baseFont = m_owner->StyleGetFont(wxSTC_STYLE_DEFAULT);
	baseFont.SetPointSize(std::max(1, baseFont.GetPointSize() + m_owner->GetZoom()));
	m_font = wxFontWithAscent(baseFont);
	dc.SetFont(m_font);

	// Look for multiple lines in the text
	// Only support \n here - simply means container must avoid \r!
	int numLines = 1;
	const char* newline;
	const char* look = m_val.c_str();
	m_rectUp = wxRect(0, 0, 0, 0);
	m_rectDown = wxRect(0, 0, 0, 0);
	m_offsetMain = m_insetX;            // changed to right edge of any arrows
	int width_ = PaintContents(dc, false) + m_insetX;
	while ((newline = strchr(look, '\n')) != nullptr)
	{
		look = newline + 1;
		numLines++;
	}

	m_lineHeight = RoundXYPosition(dc.GetCharHeight() + 1);

	// The returned rectangle is aligned to the right edge of the last
	// arrow encountered in the tip text, else to the tip text left edge.
	int height_ = m_lineHeight * numLines + m_borderHeight * 2;

	wxRect rc = { pt.x + m_offsetMain, pt.y - m_verticalOffset - height_, (pt.x + width_ + m_offsetMain) - (pt.x + m_offsetMain), (pt.y - m_verticalOffset) - (pt.y + m_verticalOffset - height_) };

	// If the call-tip window would be out of the client space
	wxSize sz = m_owner->GetClientSize();
	wxRect rcClient = { 0, 0, sz.x, sz.y };

	int offset = textHeight + rc.GetBottom() - rc.GetTop();

	// adjust so it displays above the text.
	if (rc.GetBottom() > rcClient.GetBottom() && rc.height < rcClient.height) {
		rc.SetTop(rc.GetTop() - offset);
		rc.SetBottom(rc.GetBottom() - offset);
	}

	// adjust so it displays below the text.
	if (rc.GetTop() < rcClient.GetTop() && rc.height < rcClient.height) {
		rc.SetTop(rc.GetTop() + offset);
		rc.SetBottom(rc.GetBottom() + offset);
	}

	wxPoint position = m_owner->GetScreenPosition();
	position.x = position.x + rc.GetLeft();
	position.y = position.y + rc.GetTop();

	const wxRect displayRect = wxDisplay(m_owner).GetClientArea();

	if (position.x < displayRect.GetLeft())
		position.x = displayRect.GetLeft();

	dc.SetFont(m_font);

	const int width = rc.width;
	if (width > displayRect.GetWidth())
	{
		// We want to show at least the beginning of the window.
		position.x = displayRect.GetLeft();
	}
	else if (position.x + width > displayRect.GetRight())
		position.x = displayRect.GetRight() - width;

	const int height = rc.height;
	if (position.y + height > displayRect.GetBottom())
		position.y = displayRect.GetBottom() - height;

	position = m_owner->ScreenToClient(position);

	m_callTip->SetSize(position.x, position.y, width, height);
	m_callTip->Show();
}

void ibCallTip::Cancel()
{
	m_inCallTipMode = false;

	if (m_callTip)
	{
		m_callTip->Destroy();
		m_callTip = nullptr;
	}

	if (m_evtHandler) wxDELETE(m_evtHandler);
}

void ibCallTip::SetHighlight(int start, int end)
{
	// Avoid flashing by checking something has really changed
	if ((start != m_startHighlight) || (end != m_endHighlight))
	{
		m_startHighlight = start;
		m_endHighlight = (end > start) ? end : start;
		if (m_callTip)
		{
			m_callTip->Refresh(false);
		}
	}
}

// Set the tab size (sizes > 0 enable the use of tabs). This also enables the
// use of the STYLE_CALLTIP.
void ibCallTip::SetTabSize(int tabSz)
{
	m_tabSize = tabSz;
	m_useStyleCallTip = true;
}

// It might be better to have two access functions for this and to use
// them for all settings of colours.
void ibCallTip::SetForeBack(const wxColour& fore, const wxColour& back)
{
	m_colourBG = back;
	m_colourUnSel = fore;
}

bool ibCallTip::CallEvent(wxEvent& event)
{
	if (!m_inCallTipMode) return false;
	bool result = m_evtHandler->ProcessEvent(event);
	if (m_evtHandler) return result;
	return false;
}

void ibCallTip::OnKeyDown(wxKeyEvent& event)
{
	switch (event.GetKeyCode())
	{
	case WXK_ESCAPE: Cancel(); break;
	case WXK_BACK: Cancel(); break;
	case WXK_NUMPAD_ENTER:
	case WXK_RETURN: Cancel(); break;
	}
}

void ibCallTip::OnProcessFocus(wxFocusEvent& event)
{
	Cancel();
}

void ibCallTip::OnProcessChildFocus(wxChildFocusEvent& event)
{
	Cancel();
}

void ibCallTip::OnProcessSize(wxSizeEvent& event)
{
	Cancel();
}

void ibCallTip::OnProcessMouse(wxMouseEvent& event)
{
	if (event.GetEventType() != wxEVT_LEFT_UP) Cancel();
}
