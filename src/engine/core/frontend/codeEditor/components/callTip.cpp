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

CCallTip::CCallTip(wxStyledTextCtrl *owner)
{
	m_callTip = NULL;
	m_evtHandler = NULL;
	inCallTipMode = false;
	posStartCallTip = 0;
	rectUp = wxRect(0, 0, 0, 0);
	rectDown = wxRect(0, 0, 0, 0);
	m_owner = owner;
	lineHeight = 1;
	offsetMain = 0;
	startHighlight = 0;
	endHighlight = 0;
	tabSize = 0;
	useStyleCallTip = false;    // for backwards compatibility

	insetX = 5;
	widthArrow = 14;
	borderHeight = 2; // Extra line for border and an empty line at top and bottom.
	verticalOffset = 1;

#ifdef __APPLE__
	// proper apple colours for the default
	colourBG = wxColour(0xff, 0xff, 0xc6);
	colourUnSel = wxColour(0, 0, 0);
#else
	colourBG = wxColour(0xff, 0xff, 0xff);
	colourUnSel = wxColour(0x80, 0x80, 0x80);
#endif

	colourSel = wxColour(0, 0, 0x80);
	colourShade = wxColour(0, 0, 0);
	colourLight = wxColour(0xc0, 0xc0, 0xc0);

	clickPlace = 0;
}

CCallTip::~CCallTip()
{
	if (m_callTip) m_callTip->Destroy();
}

// Although this test includes 0, we should never see a \0 character.
static bool IsArrowCharacter(char ch)
{
	return (ch == 0) || (ch == '\001') || (ch == '\002');
}

// We ignore tabs unless a tab width has been set.
bool CCallTip::IsTabCharacter(char ch) const
{
	return (tabSize > 0) && (ch == '\t');
}

int CCallTip::NextTabPos(int x) const
{
	if (tabSize > 0) {              // paranoia... not called unless this is true
		x -= insetX;                // position relative to text
		x = (x + tabSize) / tabSize;  // tab "number"
		return tabSize * x + insetX;  // position of next tab
	}
	else {
		return x + 1;                 // arbitrary
	}
}

// Draw a section of the call tip that does not include \n in one colour.
// The text may include up to numEnds tabs or arrow characters.
void CCallTip::DrawChunk(wxDC &dc, int &x, const char *s,
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
				xEnd = x + widthArrow;
				bool upArrow = s[startSeg] == '\001';
				rcClient.SetLeft(x);
				rcClient.SetRight(xEnd);
				if (draw) {
					const int halfWidth = widthArrow / 2 - 3;
					const int quarterWidth = halfWidth / 2;
					const int centreX = x + widthArrow / 2 - 1;
					const int centreY = static_cast<int>(rcClient.GetTop() + rcClient.GetBottom()) / 2;

					//dc.FillRectangle(rcClient, colourBG);
					dc.SetBrush(wxBrush(colourBG));
					dc.SetPen(*wxTRANSPARENT_PEN);
					dc.DrawRectangle(rcClient);

					wxRect rcClientInner(rcClient.GetLeft() + 1, rcClient.GetTop() + 1,
						rcClient.GetRight() - 2, rcClient.GetBottom() - 1);

					//dc.FillRectangle(rcClientInner, colourUnSel);

					dc.SetBrush(wxBrush(colourUnSel));
					dc.SetPen(*wxTRANSPARENT_PEN);
					dc.DrawRectangle(rcClientInner);

					if (upArrow) {      // Up arrow
						wxPoint pts[] = {
							wxPoint(centreX - halfWidth, centreY + quarterWidth),
							wxPoint(centreX + halfWidth, centreY + quarterWidth),
							wxPoint(centreX, centreY - halfWidth + quarterWidth),
						};

						//dc.Polygon(pts, ELEMENTS(pts), colourBG, colourBG);
						dc.SetPen(wxPen(colourBG));
						dc.SetBrush(wxBrush(colourBG));
						dc.DrawPolygon(3, pts);
					}
					else {            // Down arrow
						wxPoint pts[] = {
							wxPoint(centreX - halfWidth, centreY - quarterWidth),
							wxPoint(centreX + halfWidth, centreY - quarterWidth),
							wxPoint(centreX, centreY + halfWidth - quarterWidth),
						};

						//dc.Polygon(pts, ELEMENTS(pts), colourBG, colourBG);	
						dc.SetPen(wxPen(colourBG));
						dc.SetBrush(wxBrush(colourBG));
						dc.DrawPolygon(3, pts);

					}
				}
				offsetMain = xEnd;
				if (upArrow)
				{
					rectUp = rcClient;
				}
				else
				{
					rectDown = rcClient;
				}
			}
			else if (IsTabCharacter(s[startSeg]))
			{
				xEnd = NextTabPos(x);
			}
			else
			{
				//xEnd = x + RoundXYPosition(dc.WidthText(font, s + startSeg, endSeg - startSeg));

				dc.SetFont(font);
				int w, h;
				dc.GetTextExtent(wxString::FromUTF8(s + startSeg, endSeg - startSeg), &w, &h);

				xEnd = x + RoundXYPosition(w);

				if (draw)
				{
					rcClient.SetLeft(x);
					rcClient.SetRight(xEnd);
					//dc.DrawTextTransparent(rcClient, font, static_cast<float>(ytext),
					//	s + startSeg, endSeg - startSeg,
					//	highlight ? colourSel : colourUnSel);

					dc.SetFont(font);
					dc.SetTextForeground(highlight ? colourSel : colourUnSel);
					dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);

					// ybase is where the baseline should be, but wxWin uses the upper left
					// corner, so I need to calculate the real position for the text...
					dc.DrawText(wxString::FromUTF8(s, len), rcClient.GetLeft(), static_cast<float>(ytext) - font.GetAscent());
					dc.SetBackgroundMode(wxBRUSHSTYLE_SOLID);
				}
			}
			x = xEnd;
			startSeg = endSeg;
		}
	}
}

int CCallTip::PaintContents(wxDC &dc, bool draw)
{
	wxSize sz = m_callTip->GetClientSize();
	wxRect rcClientPos(0, 0, sz.x, sz.y);

	wxRect rcClientSize(0.0f, 0.0f, rcClientPos.GetRight() - rcClientPos.GetLeft(),
		rcClientPos.GetBottom() - rcClientPos.GetTop());
	wxRect rcClient(0.0f, 0.0f, rcClientSize.GetRight(), rcClientSize.GetBottom());

	// To make a nice small call tip wxWindow, it is only sized to fit most normal characters without accents
	int ascent = RoundXYPosition(font.GetAscent() - 0);

	// For each line...
	// Draw the definition in three parts: before highlight, highlighted, after highlight
	int ytext = static_cast<int>(rcClient.GetTop()) + ascent + 1;

	//rcClient.SetBottom(ytext + dc.Descent(font) + 1;
	dc.SetFont(font);

	int w, h, d, e;
	dc.GetTextExtent(EXTENT_TEST, &w, &h, &d, &e);

	rcClient.SetBottom(ytext + d + 1);

	const char *chunkVal = val.c_str();
	bool moreChunks = true;
	int maxWidth = 0;

	while (moreChunks)
	{
		const char *chunkEnd = strchr(chunkVal, '\n');
		if (chunkEnd == NULL)
		{
			chunkEnd = chunkVal + strlen(chunkVal);
			moreChunks = false;
		}
		int chunkOffset = static_cast<int>(chunkVal - val.c_str());
		int chunkLength = static_cast<int>(chunkEnd - chunkVal);
		int chunkEndOffset = chunkOffset + chunkLength;
		int thisStartHighlight = std::max(startHighlight, chunkOffset);
		thisStartHighlight = std::min(thisStartHighlight, chunkEndOffset);
		thisStartHighlight -= chunkOffset;
		int thisEndHighlight = std::max(endHighlight, chunkOffset);
		thisEndHighlight = std::min(thisEndHighlight, chunkEndOffset);
		thisEndHighlight -= chunkOffset;
		rcClient.SetTop(ytext - ascent - 1);

		int x = insetX;     // start each line at this inset

		DrawChunk(dc, x, wxString(chunkVal), 0, thisStartHighlight,
			ytext, rcClient, false, draw);
		DrawChunk(dc, x, wxString(chunkVal), thisStartHighlight, thisEndHighlight,
			ytext, rcClient, true, draw);
		DrawChunk(dc, x, wxString(chunkVal), thisEndHighlight, chunkLength,
			ytext, rcClient, false, draw);

		chunkVal = chunkEnd + 1;
		ytext += lineHeight;
		rcClient.SetBottom(rcClient.GetBottom() + lineHeight);
		maxWidth = std::max(maxWidth, x);
	}

	return maxWidth;
}

void CCallTip::PaintCT(wxDC &dc)
{
	if (val.empty())
		return;

	wxSize sz = m_callTip->GetClientSize();
	wxRect rcClientPos(0, 0, sz.x, sz.y);

	wxRect rcClientSize(0.0f, 0.0f, rcClientPos.GetRight() - rcClientPos.GetLeft(),
		rcClientPos.GetBottom() - rcClientPos.GetTop());

	wxRect rcClient(0.0f, 0.0f, rcClientSize.GetRight(), rcClientSize.GetBottom());

	dc.SetBrush(wxBrush(colourBG));
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRectangle(rcClient);

	offsetMain = insetX;    // initial alignment assuming no arrows
	PaintContents(dc, true);

#ifndef __APPLE__

	// OSX doesn't put borders on "help tags"
	// Draw a raised border around the edges of the wxWindow
	dc.SetPen(wxPen(colourLight));

	dc.DrawLine(0, rcClientSize.GetBottom(), rcClientSize.GetRight(), rcClientSize.GetBottom());
	dc.DrawLine(rcClientSize.GetRight(), rcClientSize.GetBottom(), rcClientSize.GetRight(), 0);

	dc.SetPen(wxPen(colourLight));
	dc.DrawLine(rcClientSize.GetRight(), 0, 0, 0);
	dc.DrawLine(0, 0, 0, rcClientSize.GetBottom());

#endif
}

void CCallTip::MouseClick(wxPoint pt)
{
	clickPlace = 0;

	if (rectUp.Contains(pt))
		clickPlace = 1;

	if (rectDown.Contains(pt))
		clickPlace = 2;
}

bool CCallTip::Active() const
{
	return inCallTipMode;
}

void CCallTip::Show(int currentPos, const wxString &sTitleText)
{
	if (m_callTip) {
		wxDELETE(m_callTip);
		wxDELETE(m_evtHandler);
	}

	clickPlace = 0;
	val = sTitleText;

	m_callTip = new wxSTCCallTip(m_owner, this);

	m_evtHandler = new wxEvtHandler();
	m_evtHandler->Bind(wxEVT_KEY_DOWN, &CCallTip::OnKeyDown, this);

	//focus kill/set
	m_evtHandler->Bind(wxEVT_SET_FOCUS, &CCallTip::OnProcessFocus, this);
	m_evtHandler->Bind(wxEVT_KILL_FOCUS, &CCallTip::OnProcessFocus, this);
	m_evtHandler->Bind(wxEVT_CHILD_FOCUS, &CCallTip::OnProcessChildFocus, this);

	//on sizing 
	m_evtHandler->Bind(wxEVT_SIZE, &CCallTip::OnProcessSize, this);
	m_evtHandler->Bind(wxEVT_SIZING, &CCallTip::OnProcessSize, this);

	//on mouse event
	m_evtHandler->Bind(wxEVT_LEFT_DCLICK, &CCallTip::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_RIGHT_DCLICK, &CCallTip::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_MIDDLE_DCLICK, &CCallTip::OnProcessMouse, this);
	
	m_evtHandler->Bind(wxEVT_LEFT_UP, &CCallTip::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_RIGHT_UP, &CCallTip::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_MIDDLE_UP, &CCallTip::OnProcessMouse, this);

	m_evtHandler->Bind(wxEVT_LEFT_DOWN, &CCallTip::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_RIGHT_DOWN, &CCallTip::OnProcessMouse, this);
	m_evtHandler->Bind(wxEVT_MIDDLE_DOWN, &CCallTip::OnProcessMouse, this);

	wxPoint pt = m_owner->PointFromPosition(currentPos);
	int textHeight = m_owner->TextHeight(m_owner->GetCurrentLine());

	wxBitmap m_bitmap(1, 1);
	wxMemoryDC dc(m_bitmap);

	startHighlight = 0;
	endHighlight = 0;
	inCallTipMode = true;
	posStartCallTip = currentPos;

	font = wxFontWithAscent(m_owner->StyleGetFont(wxSTC_STYLE_DEFAULT));
	dc.SetFont(font);

	// Look for multiple lines in the text
	// Only support \n here - simply means container must avoid \r!
	int numLines = 1;
	const char *newline;
	const char *look = val.c_str();
	rectUp = wxRect(0, 0, 0, 0);
	rectDown = wxRect(0, 0, 0, 0);
	offsetMain = insetX;            // changed to right edge of any arrows
	int width_ = PaintContents(dc, false) + insetX;
	while ((newline = strchr(look, '\n')) != NULL)
	{
		look = newline + 1;
		numLines++;
	}

	//lineHeight = RoundXYPosition(dc.Height(font));
	lineHeight = RoundXYPosition(dc.GetCharHeight() + 1);// rectangle is aligned to the right edge of the last arrow encountered in

	// The returned
	// rectangle is aligned to the right edge of the last arrow encountered in
	// the tip text, else to the tip text left edge.
	int height_ = lineHeight * numLines + borderHeight * 2;

	//wxRect rc = { pt.x - offsetMain, pt.y + verticalOffset + textHeight, (pt.x + width_ - offsetMain) - (pt.x - offsetMain), (pt.y + verticalOffset + textHeight + height_) - (pt.y + verticalOffset + textHeight) };
	wxRect rc = { pt.x + offsetMain, pt.y - verticalOffset - height_, (pt.x + width_ + offsetMain) - (pt.x + offsetMain), (pt.y - verticalOffset) - (pt.y + verticalOffset - height_) };

	// If the call-tip window would be out of the client
	// space
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

	dc.SetFont(font);

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

void CCallTip::Cancel()
{
	inCallTipMode = false;

	if (m_callTip)
	{
		m_callTip->Destroy();
		m_callTip = NULL;
	}

	if (m_evtHandler) wxDELETE(m_evtHandler);
}

void CCallTip::SetHighlight(int start, int end)
{
	// Avoid flashing by checking something has really changed
	if ((start != startHighlight) || (end != endHighlight))
	{
		startHighlight = start;
		endHighlight = (end > start) ? end : start;
		if (m_callTip)
		{
			m_callTip->Refresh(false);
		}
	}
}

// Set the tab size (sizes > 0 enable the use of tabs). This also enables the
// use of the STYLE_CALLTIP.
void CCallTip::SetTabSize(int tabSz)
{
	tabSize = tabSz;
	useStyleCallTip = true;
}

// It might be better to have two access functions for this and to use
// them for all settings of colours.
void CCallTip::SetForeBack(const wxColour &fore, const wxColour &back)
{
	colourBG = back;
	colourUnSel = fore;
}

bool CCallTip::CallEvent(wxEvent &event)
{
	if (!inCallTipMode) return false;
	bool result = m_evtHandler->ProcessEvent(event);
	if (m_evtHandler) return result;
	return false;
}

void CCallTip::OnKeyDown(wxKeyEvent &event)
{
	switch (event.GetKeyCode())
	{
	case WXK_ESCAPE: Cancel(); break;
	case WXK_BACK: Cancel(); break;
	case WXK_NUMPAD_ENTER:
	case WXK_RETURN: Cancel(); break;
	}
}

void CCallTip::OnProcessFocus(wxFocusEvent &event)
{
	Cancel();
}

void CCallTip::OnProcessChildFocus(wxChildFocusEvent &event)
{
	Cancel();
}

void CCallTip::OnProcessSize(wxSizeEvent &event)
{
	Cancel();
}

void CCallTip::OnProcessMouse(wxMouseEvent &event)
{
	if (event.GetEventType() != wxEVT_LEFT_UP) Cancel();
}

