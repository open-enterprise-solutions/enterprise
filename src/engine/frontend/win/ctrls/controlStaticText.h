#ifndef __CONTROL_STATIC_TEXT_H__
#define __CONTROL_STATIC_TEXT_H__

#include <wx/control.h>

#include "frontend/frontend.h"

// Cross-platform owner-drawn static text whose best size matches the label
// drawn inside ibControlTextEditor. Drop it into the same sizer row as a
// text editor and the captions line up on the same baseline for identical
// font / text.
//
// A LABEL, and only that: it draws one string and answers no input. The control that shows a
// VALUE as text — caption on one side, clickable value on the other — is ibControlStaticTextValue
// (controlStaticTextValue.h), which uses one of these as its value half, exactly as the checkbox
// composite uses a real wxCheckBox as its box half.
class FRONTEND_API ibControlStaticText : public wxControl {
public:

	ibControlStaticText() = default;

	ibControlStaticText(wxWindow* parent,
		wxWindowID id,
		const wxString& label = wxEmptyString,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = 0,
		const wxString& name = wxASCII_STR("ibControlStaticText"))
	{
		Create(parent, id, label, pos, size, style, name);
	}

	bool Create(wxWindow* parent,
		wxWindowID id,
		const wxString& label = wxEmptyString,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = 0,
		const wxString& name = wxASCII_STR("ibControlStaticText"));

	virtual void SetLabel(const wxString& label) override;

	virtual bool SetFont(const wxFont& font) override {
		InvalidateCache();
		InvalidateBestSize();
		const bool ok = wxControl::SetFont(font);
		Refresh();
		return ok;
	}

	virtual bool AcceptsFocus() const override { return false; }
	virtual bool AcceptsFocusFromKeyboard() const override { return false; }

protected:

	virtual wxSize DoGetBestClientSize() const override;

	void OnPaint(wxPaintEvent& event);
	void OnEraseBackground(wxEraseEvent&) {}

private:

	mutable wxSize m_cachedLabelSize = wxSize(-1, -1);

	void InvalidateCache() const { m_cachedLabelSize = wxSize(-1, -1); }

	wxSize ComputeLabelExtent() const;

	wxDECLARE_DYNAMIC_CLASS(ibControlStaticText);
	wxDECLARE_NO_COPY_CLASS(ibControlStaticText);
	wxDECLARE_EVENT_TABLE();
};

#endif
