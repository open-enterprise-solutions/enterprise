#ifndef _STATIC_TEXT_VALUE_H__
#define _STATIC_TEXT_VALUE_H__

#include <wx/compositewin.h>
#include <wx/containr.h>
#include <wx/dcbuffer.h>
#include <wx/settings.h>

#include "controlStaticText.h"
#include "dynamicBorder.h"

#include "frontend/frontend.h"

// A VALUE SHOWN AS TEXT — the caption / body pair, built exactly the way the checkbox composite is
// built: a drawn caption on one side, a real child control on the other. Where the checkbox puts a
// wxCheckBox, this puts an ibControlStaticText — the plain label widget, used as-is.
//
// The body is a LINK when it leads somewhere: underlined, hotlight-coloured, hand cursor, and a
// left click (or Space / Enter on focus) sends the ordinary wxEVT_BUTTON. Whoever owns this
// control decides what opening means; this side only says "it was clicked".
//
// Why a control of its own rather than a mode on ibControlStaticText: that one is a LABEL and is
// used as one all over the form layer — captions of other controls included. Teaching it about
// values, links and clicks would have made every caption on every form carry machinery it never
// uses. Same reason the checkbox is a composite and wxCheckBox stayed a checkbox.
class FRONTEND_API ibControlStaticTextValue :

	public wxCompositeWindow<wxWindow>,
	public ibControlDynamicBorder {

	// The value half — the plain label widget, with the link behaviour layered on top. Clicks and
	// keys are handled HERE rather than in ibControlStaticText, which stays free of both.
	class ibInnerValueText : public ibControlStaticText {
	public:
		ibInnerValueText(ibControlStaticTextValue* owner)
			: m_owner(owner)
		{
			Create(owner, wxID_ANY, wxEmptyString);
			Bind(wxEVT_LEFT_DOWN, &ibInnerValueText::OnLeftDown, this);
			Bind(wxEVT_KEY_DOWN, &ibInnerValueText::OnKeyDown, this);
		}

		void SetHyperlink(bool hyperlink) {
			if (m_hyperlink == hyperlink)
				return;
			m_hyperlink = hyperlink;
			// The pointer is the part people actually read: underlined text still looks like text
			// until the cursor changes over it.
			SetCursor(m_hyperlink ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
			InvalidateBestSize();
			Refresh();
		}

		bool IsHyperlink() const { return m_hyperlink; }

		// A link IS focusable — an action only the mouse can reach is one a keyboard user does not
		// have. Plain text is not, exactly like the label it derives from.
		virtual bool AcceptsFocus() const override { return m_hyperlink; }
		virtual bool AcceptsFocusFromKeyboard() const override { return m_hyperlink; }

		// PAINT LOOK of a link — colour and underline. Public because the owner applies it after
		// every change that can affect it (font, enable, link mode).
		void ApplyLinkLook() {
			wxFont font = GetFont();
			font.SetUnderlined(m_hyperlink && IsThisEnabled());
			ibControlStaticText::SetFont(font);
			SetForegroundColour(m_hyperlink && IsThisEnabled()
				? wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT)
				: wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
		}

	protected:

		void OnLeftDown(wxMouseEvent& event) {
			if (!m_hyperlink || !IsThisEnabled()) {
				event.Skip();
				return;
			}
			SetFocus();
			SendClicked();
		}

		void OnKeyDown(wxKeyEvent& event) {
			// Space / Enter on a focused link does what the click does — the keyboard reaches the
			// same action, not a shorter one.
			if (m_hyperlink && IsThisEnabled()
				&& (event.GetKeyCode() == WXK_SPACE || event.GetKeyCode() == WXK_RETURN
					|| event.GetKeyCode() == WXK_NUMPAD_ENTER)) {
				SendClicked();
				return;
			}
			event.Skip();
		}

	private:

		void SendClicked() {
			// wxEVT_BUTTON on the OWNER, not on this child: whoever binds the composite already
			// knows that shape, and an inner window is an implementation detail.
			wxCommandEvent clicked(wxEVT_BUTTON, m_owner != nullptr ? m_owner->GetId() : GetId());
			clicked.SetEventObject(m_owner != nullptr ? static_cast<wxObject*>(m_owner) : this);
			if (m_owner != nullptr)
				m_owner->ProcessWindowEvent(clicked);
		}

		ibControlStaticTextValue* m_owner = nullptr;
		bool m_hyperlink = false;
	};

private:

	ibInnerValueText* m_valueText = nullptr;

	// Drawn caption state — the same three fields the checkbox keeps for its own caption.
	wxString       m_labelText;
	wxRect         m_labelRect;
	mutable wxSize m_cachedLabelSize = wxSize(-1, -1);

	// Guards wxCompositeWindow teardown from following a dangling child.
	bool           m_destroying = false;

	wxAlignment    m_align = wxAlignment::wxALIGN_LEFT;

public:

	ibControlStaticTextValue() {}

	ibControlStaticTextValue(wxWindow* parent,
		wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = wxBORDER_NONE)
	{
		Create(parent, id, pos, size, style);
	}

	virtual ~ibControlStaticTextValue() {
		// Same teardown pattern as the checkbox composite: raise the flag so anything routed
		// through GetCompositeWindowParts() during the base destructors sees an empty list, then
		// delete the child synchronously.
		m_destroying = true;
		if (m_valueText != nullptr) {
			ibInnerValueText* v = m_valueText;
			m_valueText = nullptr;
			v->Destroy();
		}
	}

	bool Create(wxWindow* parent,
		wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = wxBORDER_NONE)
	{
		if (!wxWindow::Create(parent, id, pos, size, style | wxTAB_TRAVERSAL))
			return false;

		SetBackgroundStyle(wxBG_STYLE_PAINT);

		m_valueText = new ibInnerValueText(this);

		Bind(wxEVT_PAINT, &ibControlStaticTextValue::OnPaint, this);
		Bind(wxEVT_SIZE, &ibControlStaticTextValue::OnSize, this);

		LayoutControls();
		return true;
	}

	// THE CAPTION — what the value is called ("Counterparty"). Empty = no caption, and then the
	// value fills the control on its own.
	void SetLabel(const wxString& label) override {
		if (m_labelText == label) return;
		m_labelText = label;
		m_cachedLabelSize = wxSize(-1, -1);
		InvalidateBestSize();
		LayoutControls();
		Refresh();
	}

	wxString GetLabel() const override { return m_labelText; }

	// THE VALUE — the text on the other side of the caption.
	void SetValueText(const wxString& text) {
		if (m_valueText == nullptr) return;
		m_valueText->SetLabel(text);
		InvalidateBestSize();
		LayoutControls();
	}

	wxString GetValueText() const { return m_valueText != nullptr ? m_valueText->GetLabel() : wxString(); }

	// Does the value lead anywhere? Draws it as a link and makes it answer clicks.
	void SetHyperlink(bool hyperlink) {
		if (m_valueText == nullptr) return;
		m_valueText->SetHyperlink(hyperlink);
		m_valueText->ApplyLinkLook();
		LayoutControls();
		Refresh();
	}

	virtual void SetWindowStyleFlag(long style) override {
		if ((style & wxALIGN_RIGHT) != 0)
			m_align = wxALIGN_RIGHT;
		else if ((style & wxALIGN_LEFT) != 0)
			m_align = wxALIGN_LEFT;
		LayoutControls();
		Refresh();
		wxWindow::SetWindowStyleFlag(style);
	}

	virtual bool SetFont(const wxFont& font) override {
		const bool ok = wxWindow::SetFont(font);
		if (m_valueText != nullptr) {
			m_valueText->SetFont(font);
			m_valueText->ApplyLinkLook();
		}
		m_cachedLabelSize = wxSize(-1, -1);
		InvalidateBestSize();
		LayoutControls();
		return ok;
	}

	virtual bool Enable(bool enable = true) override {
		const bool ok = wxWindow::Enable(enable);
		if (m_valueText != nullptr) {
			m_valueText->Enable(enable);
			m_valueText->ApplyLinkLook();
		}
		Refresh();
		return ok;
	}

	// ibControlDynamicBorder — the border machinery asks the composite what its inner control is
	// and how its caption measures, exactly as it asks the checkbox.
	virtual wxWindow* GetControl() const override { return m_valueText; }
	virtual wxSize GetControlSize() const override {
		return m_valueText != nullptr ? m_valueText->GetSize() : wxSize(0, 0);
	}

	virtual void CalculateLabelSize(wxCoord* w, wxCoord* h) const override {
		const wxSize caption = CaptionExtent();
		if (w != nullptr) *w = caption.x;
		if (h != nullptr) *h = caption.y;
	}

	virtual void ApplyLabelSize(const wxSize& s) override {
		// The caption column was widened / narrowed by the row's alignment pass — remember it and
		// lay the two halves out again.
		m_cachedLabelSize = s;
		LayoutControls();
		Refresh();
	}

protected:

	// wxCompositeWindow contract — the parts that inherit colour / font / tooltip from the outer
	// window. Empty while destroying, so teardown never walks a dying child.
	virtual wxWindowList GetCompositeWindowParts() const override {
		wxWindowList parts;
		if (!m_destroying && m_valueText != nullptr)
			parts.push_back(m_valueText);
		return parts;
	}

	virtual wxSize DoGetBestClientSize() const override {
		const wxSize caption = CaptionExtent();
		const wxSize value = m_valueText != nullptr ? m_valueText->GetBestSize() : wxSize(0, 0);

		wxSize size;
		size.x = caption.x + (caption.x > 0 ? Gap() : 0) + value.x;
		size.y = wxMax(caption.y, value.y);

		const int rowH = GetCharHeight() + FromDIP(4);
		if (size.y < rowH) size.y = rowH;
		return size;
	}

	void OnSize(wxSizeEvent& event) {
		LayoutControls();
		event.Skip();
	}

	// The caption is DRAWN (like the checkbox's); the value is a child window, so it is placed.
	void OnPaint(wxPaintEvent& WXUNUSED(event)) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(GetBackgroundColour());
		dc.Clear();

		if (m_labelText.empty())
			return;

		dc.SetFont(GetFont());
		dc.SetTextForeground(IsThisEnabled()
			? GetForegroundColour()
			: wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

		wxCoord tw = 0, th = 0;
		dc.GetMultiLineTextExtent(m_labelText, &tw, &th);
		dc.DrawText(m_labelText, m_labelRect.x, m_labelRect.y + m_labelRect.height / 2 - th / 2);
	}

private:

	int Gap() const { return FromDIP(6); }

	wxSize CaptionExtent() const {
		if (m_cachedLabelSize.x < 0) {
			if (m_labelText.empty()) {
				m_cachedLabelSize = wxSize(0, 0);
			}
			else {
				wxCoord w = 0, h = 0;
				GetTextExtent(m_labelText, &w, &h);
				m_cachedLabelSize = wxSize(w, h);
			}
		}
		return m_cachedLabelSize;
	}

	// Caption on one side, value on the other — which side is the window style's answer, the same
	// one the checkbox reads.
	void LayoutControls() {
		if (m_valueText == nullptr)
			return;

		const wxSize client = GetClientSize();
		const wxSize caption = CaptionExtent();
		const int captionW = caption.x > 0 ? caption.x + Gap() : 0;
		const int valueW = wxMax(0, client.x - captionW);

		if (m_align == wxALIGN_LEFT) {
			m_labelRect = wxRect(0, 0, caption.x, client.y);
			m_valueText->SetSize(captionW, 0, valueW, client.y);
		}
		else {
			m_valueText->SetSize(0, 0, valueW, client.y);
			m_labelRect = wxRect(valueW + (caption.x > 0 ? Gap() : 0), 0, caption.x, client.y);
		}
	}

	wxDECLARE_NO_COPY_CLASS(ibControlStaticTextValue);
};

#endif // !_STATIC_TEXT_VALUE_H__
