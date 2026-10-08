#include "controlButton.h"

#include <wx/dcbuffer.h>
#include <wx/renderer.h>
#include <wx/settings.h>

wxBEGIN_EVENT_TABLE(ibControlButton, wxAnyButtonBase)
	EVT_PAINT(ibControlButton::OnPaint)
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
		const int bmpW = bmp.IsOk() ? bmp.GetWidth() : 0;
		const int bmpH = bmp.IsOk() ? bmp.GetHeight() : 0;
		const int bmpGap = (bmp.IsOk() && !label.empty()) ? FromDIP(6) : 0;

		int contentW = tw + bmpW + bmpGap;
		int contentH = wxMax(th, bmpH);

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
	const int bmpW = bmp.IsOk() ? bmp.GetWidth() : 0;
	const int bmpH = bmp.IsOk() ? bmp.GetHeight() : 0;
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
		const int contentH = bmpH + gap + th;
		int y = (sz.y - contentH) / 2;
		const int cx = sz.x / 2;
		if (m_bitmapPosition == wxTOP) {
			drawBmpAt(cx - bmpW / 2, y);
			if (!label.empty())
				dc.DrawText(label, cx - tw / 2 + pressShift, y + bmpH + gap + pressShift);
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
