#include "controlButton.h"

#include <wx/arrstr.h>   // wxSplit, wxArrayString - a tile's caption in lines
#include <wx/dcbuffer.h>
#include <wx/renderer.h>
#include <wx/settings.h>

namespace {

// A label broken into lines no wider than `width` at its spaces - a tile's caption in its tile, not cut at its edge. A
// word wider than the line stands on a line of its own and is cut there, as a label that does not wrap is.
wxArrayString WrappedLines(const wxWindow& window, const wxString& label, int width)
{
	wxArrayString lines;
	wxString line;
	for (const wxString& word : wxSplit(label, wxT(' '), 0)) {
		const wxString longer = line.empty() ? word : line + wxT(' ') + word;
		if (!line.empty() && window.GetTextExtent(longer).x > width) {
			lines.push_back(line);
			line = word;
		}
		else {
			line = longer;
		}
	}
	if (!line.empty() || lines.empty())
		lines.push_back(line);
	return lines;
}

} // namespace

wxBEGIN_EVENT_TABLE(ibControlButton, wxAnyButtonBase)
	EVT_PAINT(ibControlButton::OnPaint)
	EVT_SIZE(ibControlButton::OnSize)
	EVT_ERASE_BACKGROUND(ibControlButton::OnEraseBackground)
	EVT_MOTION(ibControlButton::OnMouseMotion)
	EVT_LEAVE_WINDOW(ibControlButton::OnMouseLeave)
	EVT_LEFT_DOWN(ibControlButton::OnLeftDown)
	EVT_LEFT_UP(ibControlButton::OnLeftUp)
	EVT_MOUSE_CAPTURE_LOST(ibControlButton::OnCaptureLost)
	EVT_KEY_DOWN(ibControlButton::OnKeyDown)
	EVT_KEY_UP(ibControlButton::OnKeyUp)
	EVT_SET_FOCUS(ibControlButton::OnSetFocus)
	EVT_KILL_FOCUS(ibControlButton::OnKillFocus)
wxEND_EVENT_TABLE()

wxIMPLEMENT_DYNAMIC_CLASS(ibControlButton, wxControl);

bool ibControlButton::Create(wxWindow* parent,
	wxWindowID id,
	const wxString& label,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxValidator& validator,
	const wxString& name)
{
	if (!wxControl::Create(parent, id, pos, size, style | wxBORDER_NONE, validator, name))
		return false;

	SetBackgroundStyle(wxBG_STYLE_PAINT);

	wxControl::SetLabel(label);
	InvalidateBestSize();
	return true;
}

void ibControlButton::SetLabel(const wxString& label)
{
	if (GetLabel() == label)
		return;
	wxControl::SetLabel(label);
	InvalidateCache();
	InvalidateBestSize();
	Refresh();
}

void ibControlButton::SetNote(const wxString& note)
{
	if (m_note == note)
		return;
	m_note = note;
	InvalidateCache();
	InvalidateBestSize();
	Refresh();
}

wxSize ibControlButton::DoGetBestClientSize() const
{
	if (m_cachedBestSize.x < 0) {
		const wxString label = HasFlag(wxBU_NOTEXT) ? wxString() : GetLabel();
		wxCoord tw = 0, th = 0;
		if (!label.empty())
			GetTextExtent(label, &tw, &th);
		else
			th = GetCharHeight();

		// Add the bitmap contribution (if any).
		const wxBitmap bmp = BitmapForCurrentState();
		const int bmpW = bmp.IsOk() ? bmp.GetLogicalWidth() : 0;
		const int bmpH = bmp.IsOk() ? bmp.GetLogicalHeight() : 0;
		const int bmpGap = (bmp.IsOk() && !label.empty()) ? FromDIP(6) : 0;

		int contentW = tw + bmpW + bmpGap;
		int contentH = wxMax(th, bmpH);
		// Stacked - a tile: the bitmap over the label, and the note under them. Once the button has a width, the label
		// is counted wrapped into it, as it is drawn - a tile narrower than its caption is as tall as its lines.
		if (m_bitmapPosition == wxTOP || m_bitmapPosition == wxBOTTOM) {
			const wxString note = (m_bitmapPosition == wxTOP && !HasFlag(wxBU_NOTEXT)) ? m_note : wxString();
			wxCoord nw = 0, nh = 0;
			const wxFont noteFont = NoteFont();
			if (!note.empty())
				GetTextExtent(note, &nw, &nh, nullptr, nullptr, &noteFont);
			int labelH = th;
			const int width = GetClientSize().x;
			if (m_bitmapPosition == wxTOP && !label.empty() && width > 0) {
				labelH = th * static_cast<int>(WrappedLines(*this, label, width - 2 * FromDIP(6)).size());
				contentW = wxMax(wxMax(wxMin(tw, width), bmpW), nw);
			}
			else {
				contentW = wxMax(wxMax(tw, bmpW), nw);
			}
			contentH = bmpH + bmpGap + labelH + (note.empty() ? 0 : FromDIP(2) + nh);
		}

		const int padX = FromDIP(14);
		const int padY = FromDIP(4);
		m_cachedBestSize = wxSize(contentW + 2 * padX, contentH + 2 * padY);
	}
	return m_cachedBestSize;
}

// ----------------------------------------------------------------------------
// bitmap
// ----------------------------------------------------------------------------

void ibControlButton::DoSetBitmap(const wxBitmapBundle& bitmap, State which)
{
	m_bitmaps[which] = bitmap;
	InvalidateCache();
	InvalidateBestSize();
	Refresh();
}

wxBitmap ibControlButton::DoGetBitmap(State which) const
{
	const wxBitmapBundle& b = m_bitmaps[which];
	return b.IsOk() ? b.GetBitmapFor(this) : wxBitmap();
}

void ibControlButton::DoSetBitmapMargins(wxCoord x, wxCoord y)
{
	m_bitmapMargins = wxSize(x, y);
	InvalidateCache();
	InvalidateBestSize();
	Refresh();
}

void ibControlButton::DoSetBitmapPosition(wxDirection dir)
{
	m_bitmapPosition = dir;
	InvalidateCache();
	InvalidateBestSize();
	Refresh();
}

wxBitmap ibControlButton::BitmapForCurrentState() const
{
	// Fall back to Normal if the current-state bundle isn't set.
	auto pick = [&](State s) -> wxBitmap {
		if (m_bitmaps[s].IsOk()) return m_bitmaps[s].GetBitmapFor(this);
		if (m_bitmaps[State_Normal].IsOk()) return m_bitmaps[State_Normal].GetBitmapFor(this);
		return wxBitmap();
	};
	if (!m_enabledIntent)                return pick(State_Disabled);
	if (m_pressed || m_keyPressed)       return pick(State_Pressed);
	if (m_hovered)                       return pick(State_Current);
	if (m_hasFocus)                      return pick(State_Focused);
	return pick(State_Normal);
}

void ibControlButton::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxAutoBufferedPaintDC dc(this);

	const wxSize sz = GetClientSize();
	if (sz.x <= 0 || sz.y <= 0)
		return;

	const wxColour parentBg = GetParent() != nullptr
		? GetParent()->GetBackgroundColour()
		: GetBackgroundColour();
	dc.SetBackground(parentBg);
	dc.Clear();

	const bool enabled = m_enabledIntent;

	// Base fill: subtle surface that shifts on hover/pressed. Use the
	// control's own background colour when the form has customised it
	// via property; otherwise the system button face so the button
	// feels native without mimicking the heavy themed bevel.
	const wxColour systemFace = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
	const wxColour ownBg      = GetBackgroundColour();
	wxColour base = (ownBg.IsOk() && ownBg != systemFace) ? ownBg : systemFace;
	wxColour fill = base;
	if (enabled) {
		if (m_pressed || m_keyPressed)     fill = base.ChangeLightness(85);
		else if (m_hovered)                fill = base.ChangeLightness(108);
	}
	else {
		fill = base.ChangeLightness(96);
	}

	// Border: slightly darker for a clean outline; use the accent color for
	// the default / focused state so it reads as the primary action.
	wxColour border = wxSystemSettings::GetColour(wxSYS_COLOUR_ACTIVEBORDER);
	if (enabled && (m_isDefault || m_hasFocus))
		border = wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT);
	if (!enabled)
		border = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);

	const wxRect full(0, 0, sz.x, sz.y);

	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(fill);
	dc.DrawRoundedRectangle(full, FromDIP(3));

	dc.SetPen(wxPen(border, m_isDefault ? FromDIP(2) : FromDIP(1)));
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.DrawRoundedRectangle(full, FromDIP(3));

	// Content: bitmap and/or label, positioned per m_bitmapPosition.
	const wxString label = HasFlag(wxBU_NOTEXT) ? wxString() : GetLabel();
	const wxBitmap bmp = BitmapForCurrentState();

	dc.SetFont(GetFont());
	dc.SetTextForeground(enabled
		? GetForegroundColour()
		: wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

	wxCoord tw = 0, th = 0;
	if (!label.empty()) dc.GetTextExtent(label, &tw, &th);
	const int bmpW = bmp.IsOk() ? bmp.GetLogicalWidth() : 0;
	const int bmpH = bmp.IsOk() ? bmp.GetLogicalHeight() : 0;
	const int gap = (bmp.IsOk() && !label.empty()) ? FromDIP(6) : 0;

	int pressShift = (m_pressed || m_keyPressed) ? FromDIP(1) : 0;

	auto drawBmpAt = [&](int x, int y) {
		if (!bmp.IsOk()) return;
		wxBitmap toDraw = bmp;
		if (!enabled) toDraw = toDraw.ConvertToDisabled();
		dc.DrawBitmap(toDraw, x + pressShift, y + pressShift, true);
	};

	if (m_bitmapPosition == wxLEFT || m_bitmapPosition == wxRIGHT) {
		const int contentW = bmpW + gap + tw;
		int x = (sz.x - contentW) / 2;
		const int cy = sz.y / 2;
		if (m_bitmapPosition == wxLEFT) {
			drawBmpAt(x, cy - bmpH / 2);
			if (!label.empty())
				dc.DrawText(label, x + bmpW + gap + pressShift, cy - th / 2 + pressShift);
		}
		else {
			if (!label.empty())
				dc.DrawText(label, x + pressShift, cy - th / 2 + pressShift);
			drawBmpAt(x + tw + gap, cy - bmpH / 2);
		}
	}
	else {
		// Stacked: the note, when there is one, under the label in a smaller face - a tile's second line. With the
		// bitmap below the label there is no line under the label for it, and with no text drawn, no label to stand under.
		const wxString note = (m_bitmapPosition == wxTOP && !HasFlag(wxBU_NOTEXT)) ? m_note : wxString();
		wxCoord nw = 0, nh = 0;
		const wxFont noteFont = NoteFont();
		if (!note.empty())
			dc.GetTextExtent(note, &nw, &nh, nullptr, nullptr, &noteFont);
		const int noteGap = note.empty() ? 0 : FromDIP(2);
		// The label over the note wraps into the width it has - a tile is as wide as its cell, not as its caption.
		const wxArrayString lines = (m_bitmapPosition == wxTOP && !label.empty())
			? WrappedLines(*this, label, sz.x - 2 * FromDIP(6)) : wxArrayString();
		const int labelH = lines.empty() ? th : th * static_cast<int>(lines.size());
		const int contentH = bmpH + gap + labelH + noteGap + nh;
		int y = (sz.y - contentH) / 2;
		const int cx = sz.x / 2;
		if (m_bitmapPosition == wxTOP) {
			drawBmpAt(cx - bmpW / 2, y);
			int lineY = y + bmpH + gap;
			for (const wxString& line : lines) {
				const int lw = dc.GetTextExtent(line).x;
				dc.DrawText(line, cx - lw / 2 + pressShift, lineY + pressShift);
				lineY += th;
			}
			if (!note.empty()) {
				dc.SetFont(noteFont);
				dc.DrawText(note, cx - nw / 2 + pressShift, y + bmpH + gap + labelH + noteGap + pressShift);
				dc.SetFont(GetFont());
			}
		}
		else {
			if (!label.empty())
				dc.DrawText(label, cx - tw / 2 + pressShift, y + pressShift);
			drawBmpAt(cx - bmpW / 2, y + th + gap);
		}
	}

	// Dotted focus indicator inset from the border
	if (m_hasFocus && enabled) {
		const int inset = FromDIP(3);
		wxRect focus = full;
		focus.Deflate(inset);
		wxRendererNative::Get().DrawFocusRect(this, dc, focus);
	}
}

// A stacked button given another width wraps its label anew: its best height is asked again, and the sizer that holds
// it reads it at its next layout.
void ibControlButton::OnSize(wxSizeEvent& event)
{
	const int width = GetClientSize().x;
	if (width != m_wrappedFor) {
		m_wrappedFor = width;
		if (m_bitmapPosition == wxTOP || m_bitmapPosition == wxBOTTOM) {
			InvalidateCache();
			InvalidateBestSize();
		}
	}
	event.Skip();
}

// ----------------------------------------------------------------------------
// mouse
// ----------------------------------------------------------------------------

void ibControlButton::OnMouseMotion(wxMouseEvent& event)
{
	const wxSize sz = GetClientSize();
	const wxPoint p = event.GetPosition();
	const bool inside = p.x >= 0 && p.y >= 0 && p.x < sz.x && p.y < sz.y;
	if (inside != m_hovered) {
		m_hovered = inside;
		Refresh();
	}
	event.Skip();
}

void ibControlButton::OnMouseLeave(wxMouseEvent& event)
{
	if (m_hovered && !m_pressed) {
		m_hovered = false;
		Refresh();
	}
	event.Skip();
}

void ibControlButton::OnLeftDown(wxMouseEvent& event)
{
	// HWND is always enabled even when m_enabledIntent==false, so the
	// designer gets the mouse event and can select the control. Track the
	// pressed state visually; only fire wxEVT_BUTTON on release if we are
	// actually enabled.
	if (!HasCapture()) CaptureMouse();
	m_pressed = true;
	m_hovered = true;
	if (m_enabledIntent) SetFocus();
	Refresh();
	event.Skip();
}

void ibControlButton::OnLeftUp(wxMouseEvent& event)
{
	if (HasCapture()) ReleaseMouse();

	const bool wasPressed = m_pressed;
	m_pressed = false;

	const wxSize sz = GetClientSize();
	const wxPoint p = event.GetPosition();
	const bool inside = p.x >= 0 && p.y >= 0 && p.x < sz.x && p.y < sz.y;

	Refresh();

	if (wasPressed && inside && m_enabledIntent)
		FireClick();

	event.Skip();
}

void ibControlButton::OnCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event))
{
	if (m_pressed || m_hovered) {
		m_pressed = false;
		m_hovered = false;
		Refresh();
	}
}

// ----------------------------------------------------------------------------
// keyboard
// ----------------------------------------------------------------------------

void ibControlButton::OnKeyDown(wxKeyEvent& event)
{
	if (!m_enabledIntent) { event.Skip(); return; }

	const int key = event.GetKeyCode();
	if (key == WXK_SPACE || key == WXK_RETURN || key == WXK_NUMPAD_ENTER) {
		if (!m_keyPressed) {
			m_keyPressed = true;
			Refresh();
		}
		return;
	}
	event.Skip();
}

void ibControlButton::OnKeyUp(wxKeyEvent& event)
{
	const int key = event.GetKeyCode();
	if ((key == WXK_SPACE || key == WXK_RETURN || key == WXK_NUMPAD_ENTER)
		&& m_keyPressed) {
		m_keyPressed = false;
		Refresh();
		if (m_enabledIntent) FireClick();
		return;
	}
	event.Skip();
}

// ----------------------------------------------------------------------------
// focus
// ----------------------------------------------------------------------------

void ibControlButton::OnSetFocus(wxFocusEvent& event)
{
	m_hasFocus = true;
	Refresh();
	event.Skip();
}

void ibControlButton::OnKillFocus(wxFocusEvent& event)
{
	m_hasFocus = false;
	m_keyPressed = false;
	Refresh();
	event.Skip();
}

// ----------------------------------------------------------------------------
// click
// ----------------------------------------------------------------------------

void ibControlButton::FireClick()
{
	wxCommandEvent ev(wxEVT_BUTTON, GetId());
	ev.SetEventObject(this);
	GetEventHandler()->ProcessEvent(ev);
}
