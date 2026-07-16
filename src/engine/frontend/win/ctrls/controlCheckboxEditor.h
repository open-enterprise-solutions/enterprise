#ifndef _CHECK_BOX_H__
#define _CHECK_BOX_H__

#include <wx/checkbox.h>
#include <wx/compositewin.h>
#include <wx/containr.h>

#include "dynamicBorder.h"

#include "frontend/frontend.h"

class FRONTEND_API ibControlCheckbox :

	public wxCompositeWindow<wxWindow>,
	public ibControlDynamicBorder {

	// Inner checkbox that forwards Tab to the outer composite's siblings.
	// The same workaround as in ibControlTextEditor is needed because
	// wxWidgets' default Tab handling doesn't unwind past our
	// wxCompositeWindow on wxMSW.
	class ibInnerCheckBox : public wxCheckBox {
	public:
		ibInnerCheckBox(ibControlCheckbox* editor, long style)
			: m_editor(editor)
		{
			Create(editor, wxID_ANY, wxEmptyString,
				wxDefaultPosition, wxDefaultSize, style);
		}

	protected:
		void OnKeyDown(wxKeyEvent& e) {
			if (e.GetKeyCode() != WXK_TAB || e.ControlDown() || e.AltDown()) {
				e.Skip();
				return;
			}
			wxWindow* outer = m_editor != nullptr ? static_cast<wxWindow*>(m_editor) : this;
			wxWindow* parent = outer->GetParent();
			if (parent == nullptr) { e.Skip(); return; }

			const wxWindowList& siblings = parent->GetChildren();
			auto self = siblings.Find(outer);
			if (self == nullptr) { e.Skip(); return; }

			const bool forward = !e.ShiftDown();
			auto node = self;
			for (;;) {
				node = forward ? node->GetNext() : node->GetPrevious();
				if (node == nullptr)
					node = forward ? siblings.GetFirst() : siblings.GetLast();
				if (node == self) break;
				wxWindow* cand = node->GetData();
				if (cand != nullptr && cand->CanAcceptFocusFromKeyboard()) {
					cand->SetFocusFromKbd();
					return;
				}
			}
			e.Skip();
		}

	private:
		ibControlCheckbox* m_editor;
		wxDECLARE_EVENT_TABLE();
	};

private:

	ibInnerCheckBox* m_checkBox = nullptr;

	// Drawn label state (replaces the old ibControlStaticText child window).
	wxString       m_labelText;
	mutable wxSize m_labelMinSize = wxDefaultSize;
	wxRect         m_labelRect;

	// Perf cache — invalidated on SetLabel/SetFont/DPI change.
	mutable wxSize m_cachedLabelSize = wxSize(-1, -1);

	// Guards wxCompositeWindow teardown from following a dangling m_checkBox.
	bool           m_destroying = false;

	// Logical enabled state; OnPaint uses it to grey out the label without
	// relying on the HWND state (HWND may stay enabled in designer mode).
	bool           m_enabledIntent = true;

	// Read-only: the box RENDERS its value and takes focus, but a click / space cannot CHANGE it (a view-only
	// form / read-only binding). Distinct from Enable(false) — the box is not greyed, just inert to edits.
	bool           m_readOnly = false;

	// Tracks whether the native wxCheckBox currently has keyboard focus so
	// OnPaint can draw a dotted focus rectangle spanning label + box.
	bool           m_hasFocus = false;

	wxAlignment    m_align = wxAlignment::wxALIGN_LEFT;

public:

	ibControlCheckbox() {}

	ibControlCheckbox(wxWindow* parent,
		wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = wxBORDER_NONE)
	{
		Create(parent, id, pos, size, style);
	}

	virtual ~ibControlCheckbox() {
		// Same teardown pattern as ibControlTextEditor: set the flag so any
		// callback routed through GetCompositeWindowParts() during the base
		// destructors sees an empty parts list, then delete the child
		// synchronously so wxWindow's own loop has nothing to do.
		m_destroying = true;
		if (m_checkBox != nullptr) {
			ibInnerCheckBox* c = m_checkBox;
			m_checkBox = nullptr;
			delete c;
		}
	}

	bool Create(wxWindow* parent = nullptr,
		wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0)
	{
		if (!wxCompositeWindow::Create(parent, id, pos, size, style))
			return false;

		SetBackgroundStyle(wxBG_STYLE_PAINT);

		m_checkBox = new ibInnerCheckBox(this, style);

		// Follow the native checkbox focus so we can draw a dotted rect
		// around label + box.
		m_checkBox->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) {
			m_hasFocus = true;
			Refresh();
			e.Skip();
		});
		m_checkBox->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
			m_hasFocus = false;
			Refresh();
			e.Skip();
		});

		// Read-only guard: a click still toggles the native box, so UNDO it and swallow the event when
		// read-only — the value shows but can't change (no source write, no OnChange script downstream). The
		// editable path skips through so ibValueCheckbox::OnClickedCheckbox runs as before.
		m_checkBox->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& e) {
			if (m_readOnly) {
				if (m_checkBox != nullptr) m_checkBox->SetValue(!e.IsChecked());   // revert to the model value
				return;   // consumed — never reaches the outer ibValueCheckbox handler
			}
			e.Skip();
		});

		return true;
	}

	virtual void SetWindowStyleFlag(long style) override {

		if ((style & wxAlignment::wxALIGN_LEFT) != 0 || style == wxAlignment::wxALIGN_LEFT) {
			m_align = wxAlignment::wxALIGN_LEFT;
		}
		else if ((style & wxAlignment::wxALIGN_RIGHT || style == wxAlignment::wxALIGN_RIGHT) != 0) {
			m_align = wxAlignment::wxALIGN_RIGHT;
		}

		wxWindow::SetWindowStyleFlag(style);
	}

	virtual void SetLabel(const wxString& label) {
		if (m_labelText == label) return;
		m_labelText = label;
		InvalidateMetrics();
		InvalidateBestSize();
		LayoutControls();
		Refresh();
	}

	virtual wxString GetLabel() const { return m_labelText; }

	virtual void SetValue(bool value) { if (m_checkBox) m_checkBox->SetValue(value); }
	virtual bool GetValue() const { return m_checkBox != nullptr && m_checkBox->GetValue(); }

	void LayoutControls();

	// overridden base class virtuals
	virtual bool SetBackgroundColour(const wxColour& colour) {
		if (m_checkBox != nullptr) {
			m_checkBox->SetBackgroundColour(m_parent != nullptr
				? m_parent->GetBackgroundColour()
				: colour);
		}
		if (m_parent != nullptr)
			return wxCompositeWindow::SetBackgroundColour(m_parent->GetBackgroundColour());
		return wxCompositeWindow::SetBackgroundColour(colour);
	}

	virtual bool SetForegroundColour(const wxColour& colour) {
		if (m_checkBox != nullptr) m_checkBox->SetForegroundColour(colour);
		Refresh();
		return wxCompositeWindow::SetForegroundColour(colour);
	}

	virtual bool SetFont(const wxFont& font) {
		if (m_checkBox != nullptr) m_checkBox->SetFont(font);
		InvalidateMetrics();
		InvalidateBestSize();
		Refresh();
		return wxCompositeWindow::SetFont(font);
	}

	virtual bool Enable(bool enable = true) {
		m_enabledIntent = enable;
		if (m_checkBox != nullptr) m_checkBox->Enable(enable);
		Refresh();
		return true;
	}

	// Read-only: value shown / focusable, but not changeable (view-only form / read-only binding). NOT Enable —
	// the box stays crisp, only edits are blocked (the toggle is reverted in the wxEVT_CHECKBOX guard above).
	void SetReadOnly(bool readOnly) { m_readOnly = readOnly; }
	bool IsReadOnly() const { return m_readOnly; }

	virtual bool AllowCalc() const {
		return m_align == wxAlignment::wxALIGN_LEFT;
	}

	//dynamic border
	virtual ibDynamicStaticText* GetStaticText() const { return nullptr; }
	virtual wxWindow* GetControl() const { return m_checkBox; }
	virtual wxSize GetControlSize() const {
		return m_checkBox != nullptr ? m_checkBox->GetSize() : wxSize(0, 0);
	}

	virtual void CalculateLabelSize(wxCoord* w, wxCoord* h) const;
	virtual void ApplyLabelSize(const wxSize& s);

protected:

#if wxUSE_TOOLTIPS
	virtual void DoSetToolTipText(const wxString& tip) override {
		// SetToolTip(const wxString&) creates a fresh wxToolTip per call;
		// giving each window its own avoids the double-free that happens
		// when the same wxToolTip pointer is handed to both.
		if (m_checkBox != nullptr) m_checkBox->SetToolTip(tip);
		wxWindow::DoSetToolTipText(tip);
	}

	virtual void DoSetToolTip(wxToolTip* tip) override {
		if (m_checkBox != nullptr) {
			if (tip != nullptr)
				m_checkBox->SetToolTip(tip->GetTip());
			else
				m_checkBox->UnsetToolTip();
		}
		wxWindow::DoSetToolTip(tip);
	}
#endif // wxUSE_TOOLTIPS

	virtual wxSize DoGetBestClientSize() const override;

	void OnPaint(wxPaintEvent& event);
	void OnEraseBackground(wxEraseEvent&) {}
	void OnSize(wxSizeEvent& event) { LayoutControls(); event.Skip(); }
	void OnDPIChanged(wxDPIChangedEvent& event) { InvalidateMetrics(); LayoutControls(); event.Skip(); }

private:

	wxSize ComputeLabelBestSize() const;
	void   InvalidateMetrics() const { m_cachedLabelSize = wxSize(-1, -1); }

	// Implement pure virtual function inherited from wxCompositeWindow.
	virtual wxWindowList GetCompositeWindowParts() const override;

	wxDECLARE_DYNAMIC_CLASS(ibControlCheckbox);
	wxDECLARE_NO_COPY_CLASS(ibControlCheckbox);
	wxDECLARE_EVENT_TABLE();
};

class FRONTEND_API wxControlNavigationCheckbox :
	public wxNavigationEnabled<ibControlCheckbox> {
public:

	wxControlNavigationCheckbox() {}
	wxControlNavigationCheckbox(wxWindow* parent,
		wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = wxBORDER_NONE)
	{
		Create(parent, id, pos, size, style);
	}
};

#endif
