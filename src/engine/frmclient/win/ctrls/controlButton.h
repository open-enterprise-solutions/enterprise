#ifndef __CONTROL_BUTTON_H__
#define __CONTROL_BUTTON_H__

#include <wx/anybutton.h>

#include "frmclient/frmclient.h"

// Flat, owner-drawn button. Inherits wxAnyButtonBase to reuse the common
// button API (label, bitmaps, alignment flags) without pulling in any
// native button HWND — everything is drawn in OnPaint so the look is
// consistent across platforms and we don't pay the native widget cost
// for every button on a dense form.
class FRMCLIENT_API ibControlButton : public wxAnyButtonBase {
public:

	ibControlButton() = default;

	ibControlButton(wxWindow* parent,
		wxWindowID id,
		const wxString& label = wxEmptyString,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = 0,
		const wxValidator& validator = wxDefaultValidator,
		const wxString& name = wxASCII_STR("ibControlButton"))
	{
		Create(parent, id, label, pos, size, style, validator, name);
	}

	bool Create(wxWindow* parent,
		wxWindowID id,
		const wxString& label = wxEmptyString,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = 0,
		const wxValidator& validator = wxDefaultValidator,
		const wxString& name = wxASCII_STR("ibControlButton"));

	virtual void SetLabel(const wxString& label) override;

	// Mark the button as the default action on its dialog / form — drawn
	// with a thicker accent border so it reads as the primary choice.
	void SetDefault(bool isDefault = true) {
		if (m_isDefault == isDefault) return;
		m_isDefault = isDefault;
		Refresh();
	}
	bool IsDefault() const { return m_isDefault; }

	virtual bool AcceptsFocus() const override { return m_enabledIntent && IsShown(); }
	virtual bool AcceptsFocusFromKeyboard() const override { return AcceptsFocus(); }

	// Enabled/disabled decoupling — the HWND always stays enabled so mouse
	// events reach our handlers (and through them the designer / parent)
	// even when the logical state is "disabled". m_enabledIntent drives the
	// greyed-out look in OnPaint and gates the wxEVT_BUTTON emission; we
	// deliberately do *not* call wxWindow::Enable() so Windows keeps
	// delivering mouse messages.
	virtual bool Enable(bool enable = true) override {
		m_enabledIntent = enable;
		Refresh();
		return true;
	}

protected:

	virtual wxSize DoGetBestClientSize() const override;

	// Bitmap support (inherited API from wxAnyButtonBase): remember the
	// bundle for each state and refresh — we draw everything manually.
	virtual void DoSetBitmap(const wxBitmapBundle& bitmap, State which) override;
	virtual wxBitmap DoGetBitmap(State which) const override;
	virtual void DoSetBitmapMargins(wxCoord x, wxCoord y) override;
	virtual wxSize DoGetBitmapMargins() const override { return m_bitmapMargins; }
	virtual void DoSetBitmapPosition(wxDirection dir) override;

	void OnPaint(wxPaintEvent& event);
	void OnEraseBackground(wxEraseEvent&) {}

	void OnMouseMotion(wxMouseEvent& event);
	void OnMouseLeave(wxMouseEvent& event);
	void OnLeftDown(wxMouseEvent& event);
	void OnLeftUp(wxMouseEvent& event);
	void OnCaptureLost(wxMouseCaptureLostEvent& event);

	void OnKeyDown(wxKeyEvent& event);
	void OnKeyUp(wxKeyEvent& event);

	void OnSetFocus(wxFocusEvent& event);
	void OnKillFocus(wxFocusEvent& event);

private:

	mutable wxSize m_cachedBestSize = wxSize(-1, -1);

	bool m_hovered = false;
	bool m_pressed = false;
	bool m_hasFocus = false;
	bool m_keyPressed = false;    // Space/Enter currently held
	bool m_isDefault = false;
	bool m_enabledIntent = true;

	// Bitmap state (one bundle per wxAnyButton::State).
	wxBitmapBundle m_bitmaps[State_Max];
	wxSize         m_bitmapMargins{ 0, 0 };
	wxDirection    m_bitmapPosition = wxLEFT;

	wxBitmap BitmapForCurrentState() const;

	void InvalidateCache() const { m_cachedBestSize = wxSize(-1, -1); }
	void FireClick();

	wxDECLARE_DYNAMIC_CLASS(ibControlButton);
	wxDECLARE_NO_COPY_CLASS(ibControlButton);
	wxDECLARE_EVENT_TABLE();
};

#endif
