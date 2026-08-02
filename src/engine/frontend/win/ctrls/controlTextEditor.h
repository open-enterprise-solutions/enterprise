#ifndef __TEXT_CTRL_H__
#define __TEXT_CTRL_H__

#include <wx/compositewin.h>
#include <wx/containr.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tooltip.h>   // DoSetToolTip() calls into wxToolTip, so it needs the full type

#include "dynamicBorder.h"

#define buttonSize 20
#define dvcMode 0x0004096

#include "frontend/frontend.h"

//text event
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_CONTROL_TEXT_ENTER, wxCommandEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_CONTROL_TEXT_INPUT, wxCommandEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_CONTROL_TEXT_CLEAR, wxCommandEvent);

//button event
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_CONTROL_BUTTON_OPEN, wxCommandEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_CONTROL_BUTTON_SELECT, wxCommandEvent);
wxDECLARE_EXPORTED_EVENT(FRONTEND_API, wxEVT_CONTROL_BUTTON_CLEAR, wxCommandEvent);

class FRONTEND_API ibControlTextEditor :

	public wxCompositeWindow<wxWindow>,
	public wxTextCtrlIface,

	public ibControlDynamicBorder {

	// ----------------------------------------------------------------------------
	// ibControlCustomTextEditor: text control used by editor control
	// ----------------------------------------------------------------------------

	class ibControlCustomTextEditor : public wxTextCtrl {
	public:

		ibControlCustomTextEditor(ibControlTextEditor* editor, const wxString& value, long style = 0)
			: m_editor(editor) {

			Create(editor, wxID_ANY, value, wxDefaultPosition, wxDefaultSize,
				(style & ~wxBORDER_MASK) | wxBORDER_NONE | wxTE_PROCESS_ENTER | wxTE_NO_VSCROLL);

			// Ensure that our best size is recomputed using our overridden
			// DoGetBestSize().
			InvalidateBestSize();
		}

		virtual void SetWindowStyleFlag(long style) override {

#if defined(__WXMSW__)

			const long specialMask = wxTE_MULTILINE | wxTE_PASSWORD | wxTE_READONLY;

			if ((style & specialMask) != (GetWindowStyle() & specialMask)) {

				const wxString value = GetValue();
				const wxPoint pos = GetPosition();
				const wxSize size = GetSize();

				// delete the old window
				HWND hwnd = GetHWND();
				DissociateHandle();
				::DestroyWindow(hwnd);

				// create the new one with the updated flags
				m_windowStyle = style;

				MSWCreateText(value, pos, size);

				// and make sure it has the same attributes as before
				if (m_hasFont) {
					// calling SetFont(m_font) would do nothing as the code would
					// notice that the font didn't change, so force it to believe
					// that it did
					wxFont font = m_font;
					m_font = wxNullFont;
					SetFont(font);
				}

				if (m_hasFgCol) {
					wxColour colFg = m_foregroundColour;
					m_foregroundColour = wxNullColour;
					SetForegroundColour(colFg);
				}

				if (m_hasBgCol) {
					wxColour colBg = m_backgroundColour;
					m_backgroundColour = wxNullColour;
					SetBackgroundColour(colBg);
				}

				SetMinSize(wxDefaultSize);
				return;
			}
#endif
			wxTextCtrl::SetWindowStyleFlag(style);
		}

		virtual void SetValue(const wxString& value) override {
			DoSetValue(value, SetValue_NoEvent);
		}

		virtual wxWindow* GetMainWindowOfCompositeControl() override { return m_editor; }

		// provide access to the base class protected methods to ibControlCustomTextEditor which
		// needs to forward to them
		void DoSetValue(const wxString& value, int flags) override
		{
			wxTextCtrl::DoSetValue(value, flags);
		}

		bool DoLoadFile(const wxString& file, int fileType) override
		{
			return wxTextCtrl::DoLoadFile(file, fileType);
		}

		bool DoSaveFile(const wxString& file, int fileType) override
		{
			return wxTextCtrl::DoSaveFile(file, fileType);
		}

	protected:

		void OnText(wxCommandEvent& e) {

			if (!IsEmpty()) {
				wxCommandEvent event(wxEVT_CONTROL_TEXT_INPUT, m_editor->GetId());
				event.SetEventObject(this);
				event.SetString(m_editor->GetValue());
				m_editor->GetEventHandler()->ProcessEvent(event);
			}
			else {
				wxCommandEvent event(wxEVT_CONTROL_TEXT_CLEAR, m_editor->GetId());
				event.SetEventObject(this);
				m_editor->GetEventHandler()->ProcessEvent(event);
			}

			e.Skip();
		}

		void OnTextEnter(wxCommandEvent& e) {

			wxCommandEvent event(wxEVT_CONTROL_TEXT_ENTER, m_editor->GetId());
			event.SetEventObject(this);
			event.SetString(m_editor->GetValue());
			m_editor->ProcessWindowEvent(event);

			e.Skip();
		}

		// Tab out of the inner EDIT: walk the outer control's sibling list
		// and move focus to the next one that accepts keyboard focus.
		// wxWidgets' own Navigate() path doesn't unwind Tab past our
		// wxCompositeWindow hierarchy on wxMSW.
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
				if (node == nullptr) {
					node = forward ? siblings.GetFirst() : siblings.GetLast();
				}
				if (node == self) break;
				wxWindow* cand = node->GetData();
				if (cand != nullptr && cand->CanAcceptFocusFromKeyboard()) {
					cand->SetFocusFromKbd();
					return;
				}
			}
			e.Skip();
		}


		// We increase the text control height to be the same as for the controls
		// with border as this is what we actually need here because even though
		// this control itself is borderless, it's inside ibControlCustomTextEditor which does
		// have the border and so should have the same height as the normal text
		// entries with border.
		virtual wxSize DoGetBestSize() const override {

			wxSize size = DoGetSizeFromTextSize(FromDIP(m_defaultItemWidth), -1);
			// A little extra breathing room above/below the text — the
			// wxBORDER_NONE style sits too tight otherwise.
			size.y += FromDIP(5);
			return size;
		}

	private:

		static const unsigned int m_defaultItemWidth = 140;

		ibControlTextEditor* m_editor;
		wxDECLARE_EVENT_TABLE();
	};

	// ----------------------------------------------------------------------------
	// drawn button slot (replaces standalone button window)
	// ----------------------------------------------------------------------------

	struct ButtonSlot {
		bool        visible = false;
		bool        enabled = true;   // false = greyed & inert (a read-only binding disables Select / Clear)
		wxString    text;
		wxEventType eventType = wxEVT_NULL;
		wxRect      rect;
		bool        hovered = false;
		bool        pressed = false;
	};

private:

	ibControlCustomTextEditor* m_text = nullptr;

	// Drawn label state (replaces ibControlStaticText).
	wxString       m_labelText;
	mutable wxSize m_labelMinSize = wxDefaultSize;
	wxRect         m_labelRect;
	wxRect         m_frameRect;
	wxRect         m_textRect;

	// Perf: cache metrics that depend on font / label text. Invalidated by
	// SetLabel / SetFont / OnDPIChanged via InvalidateMetrics().
	mutable wxSize m_cachedLabelSize = wxSize(-1, -1);
	mutable int    m_cachedSlotW = -1;
	mutable int    m_cachedSlotH = -1;

	// Set in the destructor so wxCompositeWindow's teardown code does not call
	// back into this object through GetCompositeWindowParts() once m_text has
	// started tearing down.
	bool m_destroying = false;

	// "Logical" enabled state as set by Enable(). In designer mode the HWND
	// itself stays enabled so the designer still receives mouse events for
	// selecting and editing a disabled control; the flag here drives the
	// grey-out look in OnPaint.
	bool m_enabledIntent = true;

	// Drawn button state (replaces ibControlCompositeEditor + 3 ibControlEditorButtonCtrl).
	ButtonSlot m_btnSelect;
	ButtonSlot m_btnClear;
	ButtonSlot m_btnOpen;

	bool m_dvcMode;

	bool m_passwordMode;
	bool m_multilineMode;
	bool m_textEditMode;

public:

	ibControlTextEditor() :
		m_dvcMode(false), m_passwordMode(false), m_multilineMode(false), m_textEditMode(true)
	{
		InitButtonSlots();
	}

	ibControlTextEditor(wxWindow* parent,
		wxWindowID id = wxID_ANY,
		const wxString& val = wxEmptyString,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = wxBORDER_NONE) :
		m_dvcMode(false), m_passwordMode(false), m_multilineMode(false), m_textEditMode(true)
	{
		InitButtonSlots();
		Create(parent, id, val, pos, size, style);
	}

	virtual ~ibControlTextEditor() {
		// Same pattern as the original code: explicitly destroy owned widgets
		// in our destructor body, synchronously. That way they are removed
		// from the parent child list before ~wxWindow's own child-destroy
		// loop runs. Flag first + null the pointer so any callback coming
		// through wxCompositeWindow / composite-event machinery during the
		// teardown sees an empty parts list.
		m_destroying = true;
		if (m_text != nullptr) {
			ibControlCustomTextEditor* t = m_text;
			m_text = nullptr;
			delete t;
		}
	}

	bool Create(wxWindow* parent = nullptr,
		wxWindowID id = wxID_ANY,
		const wxString& val = wxEmptyString,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0)
	{
		if (!wxCompositeWindow::Create(parent, id, pos, size, style))
			return false;

		SetBackgroundStyle(wxBG_STYLE_PAINT);

		m_text = new ibControlCustomTextEditor(this, val, style);

		return true;
	}

	void SetDVCMode(bool dvc) { m_dvcMode = dvc; }

	void SetMultilineMode(bool mode) {
		if (m_text != nullptr && m_multilineMode != mode) {
			const long style = m_text->GetWindowStyleFlag();
			if (mode) {
				m_text->SetWindowStyleFlag(style | wxTE_MULTILINE);
			}
			else {
				m_text->SetWindowStyleFlag(style & (~wxTE_MULTILINE));
			}
			m_text->Update();
		}
		m_multilineMode = mode;
	}

	bool GetMultilineMode() const { return m_multilineMode; }

	void SetPasswordMode(bool mode) {
		if (m_text != nullptr && m_passwordMode != mode) {
			const long style = m_text->GetWindowStyleFlag();
			if (mode) {
				m_text->SetWindowStyleFlag(style | wxTE_PASSWORD);
			}
			else {
				m_text->SetWindowStyleFlag(style & (~wxTE_PASSWORD));
			}
			m_text->Update();
		}
		m_passwordMode = mode;
	}

	bool GetPasswordMode() const { return m_passwordMode; }

	// TextEditMode IS the read-only policy: mode == false makes the inner text inert AND locks the value-
	// changing side buttons (Select / Clear); Open (a read-only navigation) stays live. One flag — a read-only
	// binding is just SetTextEditMode(false), no separate read-only setter.
	void SetTextEditMode(bool mode) {
		m_textEditMode = mode;
		if (m_text != nullptr) {
			const long style = m_text->GetWindowStyleFlag();
			m_text->SetWindowStyleFlag(mode ? (style & ~wxTE_READONLY) : (style | wxTE_READONLY));
			m_text->Update();
		}
		EnableSelectButton(mode);
		EnableClearButton(mode);
	}
	bool GetTextEditMode() const { return m_textEditMode; }

	virtual void SetLabel(const wxString& label) {
		if (m_labelText == label)
			return;
		m_labelText = label;
		InvalidateMetrics();
		InvalidateBestSize();
		if (!m_dvcMode) {
			LayoutControls();
			Refresh();
		}
	}

	virtual wxString GetLabel() const { return m_labelText; }

	virtual void SetValue(const wxString& label) { m_text->SetValue(label); }
	virtual wxString GetValue() const { return m_text->GetValue(); }

	void LayoutControls();

	//buttons:
	void ShowSelectButton(bool select) { ShowButton(m_btnSelect, select); }
	bool IsSelectButtonVisible() const { return m_btnSelect.visible; }

	void ShowOpenButton(bool select = true) { ShowButton(m_btnOpen, select); }
	bool IsOpenButtonVisible() const { return m_btnOpen.visible; }

	void ShowClearButton(bool select = true) { ShowButton(m_btnClear, select); }
	bool IsClearButtonVisible() const { return m_btnClear.visible; }

	// Grey Select / Clear without hiding them (they stay visible but inert). SetTextEditMode(false) calls these
	// — a read-only text box keeps its value-changing buttons visible but locked; Open (read) stays live.
	void EnableSelectButton(bool enable) { EnableButton(m_btnSelect, enable); }
	void EnableClearButton(bool enable)  { EnableButton(m_btnClear, enable); }

	// overridden base class virtuals
	virtual bool SetBackgroundColour(const wxColour& colour) {
		if (m_text != nullptr) m_text->SetBackgroundColour(colour);

		if (m_parent != nullptr)
			return wxWindow::SetBackgroundColour(m_parent->GetBackgroundColour());
		return wxWindow::SetBackgroundColour(colour);
	}

	virtual bool SetForegroundColour(const wxColour& colour) {
		if (m_text != nullptr) m_text->SetForegroundColour(colour);
		Refresh();
		return wxWindow::SetForegroundColour(colour);
	}

	virtual bool SetFont(const wxFont& font) {
		if (m_text != nullptr) m_text->SetFont(font);
		InvalidateMetrics();
		InvalidateBestSize();
		Refresh();
		return wxWindow::SetFont(font);
	}

	virtual bool Enable(bool enable = true);

	// accessors
	// ---------

	virtual wxString GetRange(long from, long to) const override;

	virtual int GetLineLength(long lineNo) const override;
	virtual wxString GetLineText(long lineNo) const override;
	virtual int GetNumberOfLines() const override;

	virtual bool IsModified() const override;
	virtual bool IsEditable() const override;

	// more readable flag testing methods
	virtual bool IsSingleLine() const;
	virtual bool IsMultiLine() const;

	// If the return values from and to are the same, there is no selection.
	virtual void GetSelection(long* from, long* to) const override;

	virtual wxString GetStringSelection() const override;

	// operations
	// ----------

	virtual void ChangeValue(const wxString& value) override;

	// editing
	virtual void Clear() override;
	virtual void Replace(long from, long to, const wxString& value) override;
	virtual void Remove(long from, long to) override;

	// load/save the controls contents from/to the file
	virtual bool LoadFile(const wxString& file);
	virtual bool SaveFile(const wxString& file = wxEmptyString);

	// sets/clears the dirty flag
	virtual void MarkDirty() override;
	virtual void DiscardEdits() override;

	// set the max number of characters which may be entered in a single line
	// text control
	virtual void SetMaxLength(unsigned long WXUNUSED(len)) override;

	// writing text inserts it at the current position, appending always
	// inserts it at the end
	virtual void WriteText(const wxString& text) override;
	virtual void AppendText(const wxString& text) override;

	// insert the character which would have resulted from this key event,
	// return true if anything has been inserted
	virtual bool EmulateKeyPress(const wxKeyEvent& event);

	// text control under some platforms supports the text styles: these
	// methods allow to apply the given text style to the given selection or to
	// set/get the style which will be used for all appended text
	virtual bool SetStyle(long start, long end, const wxTextAttr& style) override;
	virtual bool GetStyle(long position, wxTextAttr& style) override;
	virtual bool SetDefaultStyle(const wxTextAttr& style) override;
	virtual const wxTextAttr& GetDefaultStyle() const override;

	// translate between the position (which is just an index in the text ctrl
	// considering all its contents as a single strings) and (x, y) coordinates
	// which represent column and line.
	virtual long XYToPosition(long x, long y) const override;
	virtual bool PositionToXY(long pos, long* x, long* y) const override;

	virtual void ShowPosition(long pos) override;

	// find the character at position given in pixels
	//
	// NB: pt is in device coords (not adjusted for the client area origin nor
	//     scrolling)
	virtual wxTextCtrlHitTestResult HitTest(const wxPoint& pt, long* pos) const override;
	virtual wxTextCtrlHitTestResult HitTest(const wxPoint& pt,
		wxTextCoord* col,
		wxTextCoord* row) const override;

	// Clipboard operations
	virtual void Copy() override;
	virtual void Cut() override;
	virtual void Paste() override;

	virtual bool CanCopy() const override;
	virtual bool CanCut() const override;
	virtual bool CanPaste() const override;

	// Undo/redo
	virtual void Undo() override;
	virtual void Redo() override;

	virtual bool CanUndo() const override;
	virtual bool CanRedo() const override;

	// Insertion point
	virtual void SetInsertionPoint(long pos) override;
	virtual void SetInsertionPointEnd() override;
	virtual long GetInsertionPoint() const override;
	virtual wxTextPos GetLastPosition() const override;

	virtual void SetSelection(long from, long to) override;
	virtual void SelectAll() override;
	virtual void SetEditable(bool editable) override;

	// Autocomplete
	virtual bool DoAutoCompleteStrings(const wxArrayString& choices) override;
	virtual bool DoAutoCompleteFileNames(int flags) override;
	virtual bool DoAutoCompleteCustom(wxTextCompleter* completer) override;

	virtual bool ShouldInheritColours() const override;

	//dynamic border
	virtual ibDynamicStaticText* GetStaticText() const { return nullptr; }
	virtual wxWindow* GetControl() const { return m_text; }
	virtual wxSize GetControlSize() const {
		wxSize textSize = m_text != nullptr ? m_text->GetSize() : wxSize(0, 0);
		wxSize btnSize = ComputeButtonAreaSize();
		return wxSize(textSize.x + btnSize.x, std::max(textSize.y, btnSize.y));
	}

	virtual void CalculateLabelSize(wxCoord* w, wxCoord* h) const;
	virtual void ApplyLabelSize(const wxSize& s);

protected:

	virtual void DoSetValue(const wxString& value, int flags) override;
	virtual wxString DoGetValue() const override;

	virtual bool DoLoadFile(const wxString& file, int fileType) override;
	virtual bool DoSaveFile(const wxString& file, int fileType) override;

	// override the base class virtuals involved into geometry calculations
	virtual wxSize DoGetBestClientSize() const override;

#if wxUSE_TOOLTIPS
	virtual void DoSetToolTipText(const wxString& tip) override {
		// SetToolTip(const wxString&) creates a fresh wxToolTip instance per
		// call, so each window ends up owning its own tooltip — safe.
		if (m_text != nullptr) m_text->SetToolTip(tip);
		wxWindow::DoSetToolTipText(tip);
	}

	virtual void DoSetToolTip(wxToolTip* tip) override {
		// DoSetToolTip(wxToolTip*) transfers ownership of `tip` to the window
		// that receives it. Passing the same pointer to both m_text and
		// wxWindow::DoSetToolTip() would cause a double-free in the
		// destructor (wx base tripped on `delete m_tooltip` on the second
		// owner). Give m_text its own clone; let the base class keep the
		// original.
		if (m_text != nullptr) {
			if (tip != nullptr)
				m_text->SetToolTip(tip->GetTip());
			else
				m_text->UnsetToolTip();
		}
		wxWindow::DoSetToolTip(tip);
	}
#endif // wxUSE_TOOLTIPS

	void OnPaint(wxPaintEvent& event);
	void OnEraseBackground(wxEraseEvent&) { /* handled in OnPaint via wxBG_STYLE_PAINT */ }
	void OnMouseMotion(wxMouseEvent& event);
	void OnMouseLeave(wxMouseEvent& event);
	void OnLeftDown(wxMouseEvent& event);
	void OnLeftUp(wxMouseEvent& event);
	void OnCaptureLost(wxMouseCaptureLostEvent& event);

	void OnSize(wxSizeEvent& event) { LayoutControls(); event.Skip(); }
	void OnDPIChanged(wxDPIChangedEvent& event) { InvalidateMetrics(); LayoutControls(); event.Skip(); }

private:

	void InitButtonSlots() {
		m_btnSelect.text = wxT("...");
		m_btnSelect.eventType = wxEVT_CONTROL_BUTTON_SELECT;
		m_btnClear.text = wxT("\u00D7"); // ×
		m_btnClear.eventType = wxEVT_CONTROL_BUTTON_CLEAR;
		m_btnOpen.text = wxT("\U0001F50D"); // magnifying glass
		m_btnOpen.eventType = wxEVT_CONTROL_BUTTON_OPEN;
	}

	void ShowButton(ButtonSlot& slot, bool show) {
		if (slot.visible == show) return;
		slot.visible = show;
		if (!show) { slot.hovered = false; slot.pressed = false; }
		InvalidateBestSize();
		LayoutControls();
		Refresh();
	}

	// Enable/disable a slot in place — same size (stays visible), just greyed & inert. No relayout.
	void EnableButton(ButtonSlot& slot, bool enable) {
		if (slot.enabled == enable) return;
		slot.enabled = enable;
		if (!enable) { slot.hovered = false; slot.pressed = false; }
		Refresh();
	}

	wxSize ComputeLabelBestSize() const;
	wxSize ComputeButtonAreaSize() const;
	void   EnsureSlotMetrics() const;
	int    BtnSlotWidth() const { EnsureSlotMetrics(); return m_cachedSlotW; }
	int    BtnSlotHeight() const { EnsureSlotMetrics(); return m_cachedSlotH; }
	void   InvalidateMetrics() const {
		m_cachedLabelSize = wxSize(-1, -1);
		m_cachedSlotW = -1;
		m_cachedSlotH = -1;
	}
	int    VisibleButtonCount() const {
		return (m_btnSelect.visible ? 1 : 0)
		     + (m_btnClear.visible  ? 1 : 0)
		     + (m_btnOpen.visible   ? 1 : 0);
	}

	ButtonSlot* HitTestButton(const wxPoint& p);
	void DrawButton(wxDC& dc, const ButtonSlot& b);

	// implement wxTextEntry pure virtual method
	virtual wxWindow* GetEditableWindow() override { return this; }

	// Implement pure virtual function inherited from wxCompositeWindow.
	virtual wxWindowList GetCompositeWindowParts() const override;

	wxDECLARE_DYNAMIC_CLASS(ibControlTextEditor);
	wxDECLARE_NO_COPY_CLASS(ibControlTextEditor);
	wxDECLARE_EVENT_TABLE();
};

class FRONTEND_API ibControlNavigationTextEditor :
	public wxNavigationEnabled<ibControlTextEditor> {
public:

	ibControlNavigationTextEditor() {}
	ibControlNavigationTextEditor(wxWindow* parent,
		wxWindowID id = wxID_ANY,
		const wxString& val = wxEmptyString,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = wxBORDER_NONE)
	{
		Create(parent, id, val, pos, size, style);
	}
};

#endif
