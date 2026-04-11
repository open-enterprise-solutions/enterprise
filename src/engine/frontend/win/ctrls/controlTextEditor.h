#ifndef __TEXT_CTRL_H__
#define __TEXT_CTRL_H__

#include <wx/button.h>
#include <wx/compositewin.h>
#include <wx/containr.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

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

	class ibControlStaticText : public ibDynamicStaticText {
	public:

		ibControlStaticText(wxWindow* parent,
			wxWindowID id, const wxString& label, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxST_ELLIPSIZE_MASK, const wxString& name = wxASCII_STR(wxStaticTextNameStr)) :
			ibDynamicStaticText(parent, id, label, pos, size, style, name)
		{
			// Ensure that our best size is recomputed using our overridden
			// DoGetBestSize().
			InvalidateBestSize();
		}
	};

	// ----------------------------------------------------------------------------
	// ibControlCustomTextEditor: text control used by editor control
	// ----------------------------------------------------------------------------

	class ibControlCustomTextEditor : public wxTextCtrl {
	public:

		ibControlCustomTextEditor(ibControlTextEditor* editor, const wxString& value, long style = 0)
			: m_editor(editor) {

			Create(editor, wxID_ANY, value, wxDefaultPosition, wxDefaultSize,
				(style & ~wxBORDER_MASK) | wxBORDER_SIMPLE | wxTE_PROCESS_ENTER | wxTE_NO_VSCROLL);

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

		// We increase the text control height to be the same as for the controls
		// with border as this is what we actually need here because even though
		// this control itself is borderless, it's inside ibControlCustomTextEditor which does
		// have the border and so should have the same height as the normal text
		// entries with border.
		//
		// This is a bit ugly and it would arguably be better to use whatever size
		// the base class version returns and just centre the text vertically in
		// the search control but I failed to modify the code in LayoutControls()
		// to do this easily and as there is much in that code I don't understand
		// (notably what is the logic for buttons sizing?) I prefer to not touch it
		// at all.
		virtual wxSize DoGetBestSize() const override {

			wxSize size = DoGetSizeFromTextSize(FromDIP(m_defaultItemWidth), -1);

			// The calculation for no external borders in wxTextCtrl::DoGetSizeFromTextSize also
			// removes any padding around the value, which is wrong for this situation. So we
			// can't use wxBORDER_NONE to calculate a good height, in which case we just have to
			// assume a border in the code above and then subtract the space that would be taken up
			// by a themed border (the thin blue border and the white internal border).
			// Don't use FromDIP(1), this seems not needed.
			size.y -= 3;

			return size;
		}

	private:

		static const unsigned int m_defaultItemWidth = 125;

		ibControlTextEditor* m_editor;
		wxDECLARE_EVENT_TABLE();
	};

	class ibControlCompositeEditor : public wxWindow {

		// ----------------------------------------------------------------------------
		// ibControlEditorButtonCtrl: editor button used by editor control
		// ----------------------------------------------------------------------------

		class ibControlEditorButtonCtrl : public wxButton
		{
		public:
			ibControlEditorButtonCtrl(ibControlCompositeEditor* editor, int eventType, const wxString& val) :
				m_editor(editor), m_eventType(eventType)
			{
				Create(editor, wxID_ANY, val, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxBU_EXACTFIT);
				SetBackgroundStyle(wxBG_STYLE_PAINT);
			}

			// The buttons in ibControlEditorButtonCtrl shouldn't accept focus from keyboard because
			// this would interfere with the usual TAB processing: the user expects
			// that pressing TAB in the search control should switch focus to the next
			// control and not give it to the button inside the same control. Besides,
			// the search button can be already activated by pressing "Enter" so there
			// is really no reason for it to be able to get focus from keyboard.
			virtual bool AcceptsFocusFromKeyboard() const override { return false; }

			virtual wxWindow* GetMainWindowOfCompositeControl() override { return m_editor->GetMainWindowOfCompositeControl(); }

		protected:

			void OnLeftUp(wxMouseEvent& e)
			{
				wxCommandEvent event(m_eventType, m_editor->GetId());
				event.SetEventObject(m_editor->GetTextEditor());

				if (m_eventType != wxEVT_CONTROL_BUTTON_CLEAR) {

					// it's convenient to have the string to editor for directly in the
					// event instead of having to retrieve it from the control in the
					// event handler code later, so provide it here
					event.SetString(m_editor->GetValue());
				}

				m_editor->GetTextEditor()->SetFocus();

				GetEventHandler()->ProcessEvent(event);

				e.Skip();
			}

		private:

			ibControlCompositeEditor* m_editor;
			wxEventType m_eventType;

			wxDECLARE_EVENT_TABLE();
		};

		ibControlTextEditor* m_editor = nullptr;

		ibControlEditorButtonCtrl* m_selectButton = nullptr;
		ibControlEditorButtonCtrl* m_clearButton = nullptr;
		ibControlEditorButtonCtrl* m_openButton = nullptr;

		bool m_dvcMode = false;

		friend class ibControlTextEditor;

	public:

		virtual ~ibControlCompositeEditor() {

			if (m_editor != nullptr)
				m_editor->m_text = nullptr;

			delete m_selectButton;
			delete m_openButton;
			delete m_clearButton;
		}

		bool Create(ibControlTextEditor* editor,
			const wxPoint& pos = wxDefaultPosition,
			const wxSize& size = wxDefaultSize)
		{
			if (!wxWindow::Create(editor, wxID_ANY, pos, size, wxBORDER_SIMPLE))
				return false;

			m_editor = editor;

			if (pos != wxDefaultPosition)
				wxWindow::Move(pos);

			return true;
		}

		void SetDVCMode(bool dvc) { m_dvcMode = dvc; }

		ibControlCustomTextEditor* GetTextEditor() const { return m_editor->m_text; }

		virtual wxWindow* GetMainWindowOfCompositeControl() override { return m_editor->GetMainWindowOfCompositeControl(); }

		// set focus to this window as the result of a keyboard action
		virtual bool AcceptsFocusFromKeyboard() const override { return false; }

		// text event:
		virtual void SetLabel(const wxString& label) { m_editor->SetLabel(label); }
		virtual wxString GetLabel() const { return m_editor->GetLabel(); }

		virtual void SetValue(const wxString& label) { m_editor->SetValue(label); }
		virtual wxString GetValue() const { return m_editor->GetValue(); }

		//buttons:
		void ShowSelectButton(bool select = true) {

			if (select && m_selectButton == nullptr) {
				m_selectButton = new ibControlEditorButtonCtrl(this,
					wxEVT_CONTROL_BUTTON_SELECT, wxT("...")
				);
				m_selectButton->SetLabelMarkup("<b>" + m_selectButton->GetLabelText() + "</b>");
				m_selectButton->SetFont(GetFont());
				m_selectButton->SetForegroundColour(GetForegroundColour());
				m_selectButton->SetBackgroundColour(GetBackgroundColour());
				DoRefreshBackgroundColour();
				m_selectButton->Show(select);
			}
			else if (!select && m_selectButton != nullptr) {
				m_selectButton->DeletePendingEvents();
				wxDELETE(m_selectButton);
			}

			InvalidateBestSize();
		}

		bool IsSelectButtonVisible() const { return m_selectButton != nullptr; }

		void ShowOpenButton(bool select = true) {

			if (select && m_openButton == nullptr) {
				m_openButton = new ibControlEditorButtonCtrl(this,
					wxEVT_CONTROL_BUTTON_OPEN, wxT("🔍")
				);
				m_openButton->SetLabelMarkup("<b>" + m_openButton->GetLabelText() + "</b>");
				m_openButton->SetFont(GetFont());
				m_openButton->SetForegroundColour(GetForegroundColour());
				m_openButton->SetBackgroundColour(GetBackgroundColour());
				DoRefreshBackgroundColour();
				m_openButton->Show(select);
			}
			else if (!select && m_openButton != nullptr) {
				m_openButton->DeletePendingEvents();
				wxDELETE(m_openButton);
			}

			InvalidateBestSize();
		}

		bool IsOpenButtonVisible() const { return m_openButton != nullptr; }

		void ShowClearButton(bool select = true) {

			if (select && m_clearButton == nullptr) {
				m_clearButton = new ibControlEditorButtonCtrl(this,
					wxEVT_CONTROL_BUTTON_CLEAR, wxT("×")
				);
				m_clearButton->SetLabelMarkup("<b>" + m_clearButton->GetLabelText() + "</b>");
				m_clearButton->SetFont(GetFont());
				m_clearButton->SetForegroundColour(GetForegroundColour());
				m_clearButton->SetBackgroundColour(GetBackgroundColour());
				DoRefreshBackgroundColour();
				m_clearButton->Show(select);
			}
			else if (!select && m_clearButton != nullptr) {
				m_clearButton->DeletePendingEvents();
				wxDELETE(m_clearButton);
			}

			InvalidateBestSize();
		}

		bool IsClearButtonVisible() const { return m_clearButton != nullptr; }

		// overridden base class virtuals
		virtual bool SetBackgroundColour(const wxColour& colour) override {
			DoRefreshBackgroundColour(colour);
			return wxWindow::SetBackgroundColour(colour);
		}

		virtual bool SetForegroundColour(const wxColour& colour) override {
			if (m_selectButton != nullptr) m_selectButton->SetForegroundColour(colour);
			if (m_openButton != nullptr) m_openButton->SetForegroundColour(colour);
			if (m_clearButton != nullptr) m_clearButton->SetForegroundColour(colour);
			return wxWindow::SetForegroundColour(colour);
		}

		virtual bool SetFont(const wxFont& font) {
			if (m_selectButton != nullptr) m_selectButton->SetFont(font);
			if (m_openButton != nullptr) m_openButton->SetFont(font);
			if (m_clearButton != nullptr) m_clearButton->SetFont(font);
			return wxWindow::SetFont(font);
		}

		virtual bool Enable(bool enable = true) {
			if (m_selectButton != nullptr) m_selectButton->Enable(enable);
			if (m_openButton != nullptr) m_openButton->Enable(enable);
			if (m_clearButton != nullptr) m_clearButton->Enable(enable);
			return wxWindow::Enable(enable);
		}

#if wxUSE_TOOLTIPS
		virtual void DoSetToolTipText(const wxString& tip) override {
			if (m_selectButton != nullptr) m_selectButton->SetToolTip(tip);
			if (m_openButton != nullptr) m_openButton->SetToolTip(tip);
			if (m_clearButton != nullptr) m_clearButton->SetToolTip(tip);
		}

		virtual void DoSetToolTip(wxToolTip* tip) override {
			if (m_selectButton != nullptr) m_selectButton->SetToolTip(tip);
			if (m_openButton != nullptr) m_openButton->SetToolTip(tip);
			if (m_clearButton != nullptr) m_clearButton->SetToolTip(tip);
		}
#endif // wxUSE_TOOLTIPS

		virtual wxSize DoGetBestSize() const override {

			// Use one two units smaller to match size of the combo's dropbutton.
			// (normally a bigger button is used because it looks better)
			wxSize bt_sz(buttonSize - 7, m_dvcMode ? buttonSize : buttonSize - 2);

			// Position of button.
			wxPoint bt_pos(0, 0);

			int deltaX = 0, deltaY = 0;

			if (m_selectButton != nullptr && m_selectButton->IsShown()) {
				m_selectButton->SetSize(bt_sz);
				m_selectButton->SetPosition(wxPoint(bt_pos.x + deltaX, bt_pos.y));
				deltaX += bt_sz.x; deltaY += bt_sz.y;
			}

			if (m_clearButton != nullptr && m_clearButton->IsShown()) {
				m_clearButton->SetSize(bt_sz);
				m_clearButton->SetPosition(wxPoint(bt_pos.x + deltaX, bt_pos.y));
				deltaX += bt_sz.x; deltaY += bt_sz.y;
			}

			if (m_openButton != nullptr && m_openButton->IsShown()) {
				m_openButton->SetSize(bt_sz);
				m_openButton->SetPosition(wxPoint(bt_pos.x + deltaX, bt_pos.y));
				deltaX += bt_sz.x; deltaY += bt_sz.y;
			}

			wxSize controlSize = wxWindow::GetSize();
			return wxSize(deltaX > 0 ? deltaX + 1 : 0, controlSize.y);
		}

		void DoRefreshBackgroundColour() {
			DoRefreshBackgroundColour(GetBackgroundColour());
		}

		void DoRefreshBackgroundColour(const wxColour& colour) {

			int colLight = 80;
			if (m_selectButton != nullptr && m_selectButton->IsShown()) {
				m_selectButton->SetBackgroundColour(colour.ChangeLightness(colLight));
				if (colLight == 80) {
					colLight = 85;
				}
				else {
					colLight = 80;
				}
			}
			if (m_clearButton != nullptr && m_clearButton->IsShown()) {
				m_clearButton->SetBackgroundColour(colour.ChangeLightness(colLight));
				if (colLight == 80) {
					colLight = 85;
				}
				else {
					colLight = 80;
				}
			}
			if (m_openButton != nullptr && m_openButton->IsShown()) {
				m_openButton->SetBackgroundColour(colour.ChangeLightness(colLight));
				if (colLight == 80) {
					colLight = 85;
				}
				else {
					colLight = 80;
				}
			}
		}
	};

private:

	ibControlStaticText* m_label = nullptr;
	ibControlCustomTextEditor* m_text = nullptr;

	ibControlCompositeEditor* m_winButton = nullptr;

	bool m_dvcMode;

	bool m_passwordMode;
	bool m_multilineMode;
	bool m_textEditMode;

public:

	ibControlTextEditor() :
		m_passwordMode(false), m_multilineMode(false), m_textEditMode(true),
		m_dvcMode(false)
	{
	}

	ibControlTextEditor(wxWindow* parent,
		wxWindowID id = wxID_ANY,
		const wxString& val = wxEmptyString,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = wxBORDER_NONE) :
		m_passwordMode(false), m_multilineMode(false), m_textEditMode(true),
		m_dvcMode(false)
	{
		Create(parent, id, val, pos, size, style);
	}

	virtual ~ibControlTextEditor() {

		delete m_label;
		delete m_text;
		delete m_winButton;
	}

	bool Create(wxWindow* parent = nullptr,
		wxWindowID id = wxID_ANY,
		const wxString& val = wxEmptyString,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0)
	{
		if (!wxCompositeWindow::Create(parent, id, pos, size, style))
			return false;

		if (!m_dvcMode) {	
			m_label = new ibControlStaticText(this, wxID_ANY, wxEmptyString);
		}

		m_text = new ibControlCustomTextEditor(this, val, style);

		m_winButton = new ibControlCompositeEditor;
		m_winButton->SetDVCMode(m_dvcMode);
		m_winButton->Create(this, wxDefaultPosition, wxDefaultSize);

		return true;
	}

	void SetDVCMode(bool dvc)
	{
		if (m_winButton != nullptr)
			m_winButton->SetDVCMode(m_dvcMode);
		m_dvcMode = dvc;
	}

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

	void SetTextEditMode(bool mode) {
		if (m_text != nullptr && m_textEditMode != mode) {
			const long style = m_text->GetWindowStyleFlag();
			if (mode) {
				m_text->SetWindowStyle(style & (~wxTE_READONLY));
			}
			else {
				m_text->SetWindowStyleFlag(style | wxTE_READONLY);
			}
			m_text->Update();
		}
		m_textEditMode = mode;
	}

	bool GetTextEditMode() const { return m_textEditMode; }

	virtual void SetLabel(const wxString& label) {
		if (m_label != nullptr) m_label->SetLabel(label);
	}

	virtual wxString GetLabel() const { if (m_label != nullptr) return m_label->GetLabel(); return wxString(); }

	virtual void SetValue(const wxString& label) { m_text->SetValue(label); }
	virtual wxString GetValue() const { return m_text->GetValue(); }

	void LayoutControls() {

		if (!m_text)
			return;

		const wxSize sizeTotal = GetClientSize();
		int width = sizeTotal.x,
			height = sizeTotal.y;

		wxSize sizeButton = m_winButton->GetBestSize();

		if (m_dvcMode) {
			m_text->SetSize(0, 0, width - sizeButton.x, height);
			m_winButton->SetSize(width - sizeButton.x - 1, 0, sizeButton.x + 1, height);
		}
		else {
			wxSize sizeLabel = m_label->GetBestSize();
			m_label->SetSize(0, (height - sizeLabel.y) / 2, sizeLabel.x, height);
			m_text->SetSize(sizeLabel.x + 1, 0, width - sizeButton.x - sizeLabel.x - 1, height);
			m_winButton->SetSize(width - sizeButton.x - 1, 0, sizeButton.x, sizeButton.y);
		}
	}

	//buttons:
	void ShowSelectButton(bool select) {

		if (m_winButton != nullptr) {
			m_winButton->ShowSelectButton(select);
		}
	}

	bool IsSelectButtonVisible() const { return m_winButton->IsSelectButtonVisible(); }

	void ShowOpenButton(bool select = true) {

		if (m_winButton != nullptr) {
			m_winButton->ShowOpenButton(select);
		}
	}

	bool IsOpenButtonVisible() const { return m_winButton->IsOpenButtonVisible(); }

	void ShowClearButton(bool select = true) {

		if (m_winButton != nullptr) {
			m_winButton->ShowClearButton(select);
		}
	}

	bool IsClearButtonVisible() const { return m_winButton->IsClearButtonVisible(); }

	// overridden base class virtuals
	virtual bool SetBackgroundColour(const wxColour& colour) {
		if (!m_dvcMode) {
			if (m_label != nullptr && m_parent != nullptr) {
				m_label->SetBackgroundColour(m_parent->GetBackgroundColour());
			}
		}
		
		if (m_text != nullptr) m_text->SetBackgroundColour(colour);
		if (m_winButton != nullptr) m_winButton->SetBackgroundColour(colour);

		if (m_parent != nullptr)
			return wxWindow::SetBackgroundColour(m_parent->GetBackgroundColour());
		return wxWindow::SetBackgroundColour(colour);
	}

	virtual bool SetForegroundColour(const wxColour& colour) {
		if (!m_dvcMode) {
			if (m_text != nullptr) {
				m_label->SetForegroundColour(colour);
			}
		}
		if (m_text != nullptr) m_text->SetForegroundColour(colour);
		if (m_winButton != nullptr) m_winButton->SetForegroundColour(colour);
		return wxWindow::SetForegroundColour(colour);
	}

	virtual bool SetFont(const wxFont& font) {
		if (!m_dvcMode) {
			if (m_label != nullptr) {
				m_label->SetFont(font);
			}
		}
		if (m_text != nullptr) m_text->SetFont(font);
		if (m_winButton != nullptr) m_winButton->SetFont(font);
		return wxWindow::SetFont(font);
	}

	virtual bool Enable(bool enable = true) {
		bool result = m_text->Enable(enable);
		m_winButton->Enable(enable);
		return result;
	}

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
	virtual ibDynamicStaticText* GetStaticText() const { return m_label; }
	virtual wxWindow* GetControl() const { return m_text; }
	virtual wxSize GetControlSize() const {
		return m_text->GetSize() +
			m_winButton->GetSize();
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
		if (!m_dvcMode) {
			m_label->SetToolTip(tip);
		}
		m_text->SetToolTip(tip);
		m_winButton->SetToolTip(tip);
	}

	virtual void DoSetToolTip(wxToolTip* tip) override {
		if (!m_dvcMode) {
			m_label->SetToolTip(tip);
		}
		m_text->SetToolTip(tip);
		m_winButton->SetToolTip(tip);
	}
#endif // wxUSE_TOOLTIPS

	void OnSize(wxSizeEvent& event) { LayoutControls(); event.Skip(); }
	void OnDPIChanged(wxDPIChangedEvent& event) { LayoutControls(); event.Skip(); }

private:

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
