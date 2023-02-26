#ifndef __TEXT_CTRL_H__
#define __TEXT_CTRL_H__

#include <wx/app.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/listbox.h>
#include <wx/popupwin.h>
#include <wx/panel.h>
#include <wx/textctrl.h>

#if defined(__WXMSW__)
#include <windows.h>
#endif

#include "dynamicBorder.h"

enum {
	textCtrl_buttonSelect = wxID_LOWEST + 1,
	textCtrl_buttonOpen,
	textCtrl_buttonClear
};

#define buttonSize 20
#define dvcMode 0x0004096

class wxTextContainerCtrl : public wxWindow,
	public IDynamicBorder {

	wxDECLARE_DYNAMIC_CLASS(wxTextContainerCtrl);
	wxDECLARE_NO_COPY_CLASS(wxTextContainerCtrl);

	class wxTextRawCtrl : public wxTextCtrl {
	public:

		wxTextRawCtrl() {}
		wxTextRawCtrl(wxWindow* parent, wxWindowID id,
			const wxString& value = wxEmptyString,
			const wxPoint& pos = wxDefaultPosition,
			const wxSize& size = wxDefaultSize,
			long style = 0,
			const wxValidator& validator = wxDefaultValidator, 
			const wxString& name = wxASCII_STR(wxTextCtrlNameStr)) 
			: 
			wxTextCtrl(parent, id, value, pos, size, style, validator, name)
		{
		}

		virtual void SetValue(const wxString& value) override {
			DoSetValue(value, SetValue_NoEvent);
		}
	};

	class wxTextButtonCtrl : public wxTextCtrl {

		wxDECLARE_DYNAMIC_CLASS(wxTextButtonCtrl);
		wxDECLARE_NO_COPY_CLASS(wxTextButtonCtrl);

		wxButton *m_buttonSelect = NULL;
		wxButton *m_buttonClear = NULL;
		wxButton *m_buttonOpen = NULL;

		bool m_dvcMode = false;

		bool m_selbutton = false;
		bool m_clearbutton = false;
		bool m_openbutton = false;

		friend class wxTextContainerCtrl;

	private:

		void OnButtonClicked(wxCommandEvent& event)
		{
			wxCommandEvent redirectedEvent(event);
			redirectedEvent.SetEventObject(this);

			if (!GetEventHandler()->ProcessEvent(redirectedEvent)) {
				event.Skip();
			}
		}

		void OnSizeTextCtrl(wxSizeEvent& event)
		{
			CalculateButton();
			event.Skip();
		}

		int CalculateButton()
		{
			// Use one two units smaller to match size of the combo's dropbutton.
			// (normally a bigger button is used because it looks better)
			wxSize bt_sz(buttonSize - 7, m_dvcMode ? buttonSize : buttonSize - 2);

			// Position of button.
			wxPoint bt_pos(0, 1);

			int deltaX = 0, deltaY = 0;

			if (m_buttonSelect->IsShown()) {
				m_buttonSelect->SetSize(bt_sz);
				m_buttonSelect->SetPosition(wxPoint(bt_pos.x + deltaX, bt_pos.y));
				deltaX += bt_sz.x; deltaY += bt_sz.y;
			}

			if (m_buttonClear->IsShown()) {
				m_buttonClear->SetSize(bt_sz);
				m_buttonClear->SetPosition(wxPoint(bt_pos.x + deltaX, bt_pos.y));
				deltaX += bt_sz.x; deltaY += bt_sz.y;
			}

			if (m_buttonOpen->IsShown()) {
				m_buttonOpen->SetSize(bt_sz);
				m_buttonOpen->SetPosition(wxPoint(bt_pos.x + deltaX, bt_pos.y));
				deltaX += bt_sz.x; deltaY += bt_sz.y;
			}

			wxSize controlSize = wxTextCtrl::GetSize();
			wxTextCtrl::SetMaxSize(wxSize(deltaX > 0 ? deltaX + 1 : 0, controlSize.y));
			return deltaX;
		}

	public:

		wxTextButtonCtrl() : wxTextCtrl() {}

		virtual ~wxTextButtonCtrl() {

			m_buttonSelect = NULL;
			m_buttonOpen = NULL;
			m_buttonClear = NULL;

			wxTextCtrl::Unbind(wxEVT_SIZE, &wxTextButtonCtrl::OnSizeTextCtrl, this);

			if (m_parent->IsKindOf(CLASSINFO(wxTextContainerCtrl))) {
				((wxTextContainerCtrl *)m_parent)->m_textCtrl = NULL;
			}
		}

		bool Create(wxWindow *parent, const wxPoint& pos = wxDefaultPosition,
			const wxSize& size = wxDefaultSize, int style = wxBORDER_SIMPLE)
		{
			if (!wxTextCtrl::Create(parent, wxID_ANY, wxEmptyString, pos, size, style))
				return false;

			m_buttonSelect = new wxButton(this, textCtrl_buttonSelect, wxT("..."), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTRANSPARENT_WINDOW);
			m_buttonClear = new wxButton(this, textCtrl_buttonClear, wxT("×"), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTRANSPARENT_WINDOW);
			m_buttonOpen = new wxButton(this, textCtrl_buttonOpen, wxT("🔍"), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTRANSPARENT_WINDOW);

			wxColour bgCol = GetBackgroundColour();

			m_buttonSelect->SetBackgroundColour(bgCol.ChangeLightness(80));
			m_buttonClear->SetBackgroundColour(bgCol.ChangeLightness(85));
			m_buttonOpen->SetBackgroundColour(bgCol.ChangeLightness(80));

			m_buttonSelect->SetLabelMarkup("<b>" + m_buttonSelect->GetLabelText() + "</b>");
			m_buttonClear->SetLabelMarkup("<b>" + m_buttonClear->GetLabelText() + "</b>");
			m_buttonOpen->SetLabelMarkup("<b>" + m_buttonOpen->GetLabelText() + "</b>");

			m_buttonSelect->Show(m_selbutton);
			m_buttonClear->Show(m_clearbutton);
			m_buttonOpen->Show(m_openbutton);

			CalculateButton();

			wxTextCtrl::Bind(wxEVT_SIZE, &wxTextButtonCtrl::OnSizeTextCtrl, this);
			return true;
		}

		void SetDVCMode(bool dvc)
		{
			m_dvcMode = dvc;
		}

		// Bind functors to an event:
		template <typename Functor, typename EventHandler>
		void BindButtonSelect(const Functor &functor, EventHandler handler) { m_buttonSelect->Bind(wxEVT_BUTTON, functor, handler, textCtrl_buttonSelect); }
		template <typename Functor, typename EventHandler>
		void BindButtonClear(const Functor &functor, EventHandler handler) { m_buttonClear->Bind(wxEVT_BUTTON, functor, handler, textCtrl_buttonClear); }
		template <typename Functor, typename EventHandler>
		void BindButtonOpen(const Functor& functor, EventHandler handler) { m_buttonOpen->Bind(wxEVT_BUTTON, functor, handler, textCtrl_buttonOpen); }

		// Unbind functors to an event:
		template <typename Functor, typename EventHandler>
		void UnbindButtonSelect(const Functor &functor, EventHandler handler) { m_buttonSelect->Unbind(wxEVT_BUTTON, functor, handler, textCtrl_buttonSelect); }
		template <typename Functor, typename EventHandler>
		void UnbindButtonClear(const Functor& functor, EventHandler handler) { m_buttonClear->Unbind(wxEVT_BUTTON, functor, handler, textCtrl_buttonClear); }
		template <typename Functor, typename EventHandler>
		void UnbindButtonOpen(const Functor &functor, EventHandler handler) { m_buttonOpen->Unbind(wxEVT_BUTTON, functor, handler, textCtrl_buttonOpen); }

		//buttons:
		void SetButtonSelect(bool select = true) {
			m_selbutton = select;
			if (m_buttonSelect) {
				m_buttonSelect->Show(select);
			}
		}

		bool HasButtonSelect() const {
			return m_selbutton;
		}

		void SetButtonOpen(bool select = true) {
			m_openbutton = select;
			if (m_buttonOpen) {
				m_buttonOpen->Show(select);
			}
		}

		bool HasButtonOpen() const {
			return m_openbutton;
		}

		void SetButtonClear(bool select = true) {
			m_clearbutton = select;
			if (m_buttonClear) {
				m_buttonClear->Show(select);
			}
		}

		bool HasButtonClear() const {
			return m_clearbutton;
		}

		// overridden base class virtuals
		virtual bool SetBackgroundColour(const wxColour& colour) override {
			int colLight = 80;
			if (m_buttonSelect->IsShown()) {
				m_buttonSelect->SetBackgroundColour(colour.ChangeLightness(colLight));
				if (colLight == 80) {
					colLight = 85;
				}
				else {
					colLight = 80;
				}
			}
			if (m_buttonClear->IsShown()) {
				m_buttonClear->SetBackgroundColour(colour.ChangeLightness(colLight));
				if (colLight == 80) {
					colLight = 85;
				}
				else {
					colLight = 80;
				}
			}
			if (m_buttonOpen->IsShown()) {
				m_buttonOpen->SetBackgroundColour(colour.ChangeLightness(colLight));
				if (colLight == 80) {
					colLight = 85;
				}
				else {
					colLight = 80;
				}
			}
			return wxTextCtrl::SetBackgroundColour(colour);
		}

		virtual bool SetForegroundColour(const wxColour& colour) override {
			m_buttonSelect->SetForegroundColour(colour);
			m_buttonOpen->SetForegroundColour(colour);
			m_buttonClear->SetForegroundColour(colour);
			return wxTextCtrl::SetForegroundColour(colour);
		}

		virtual bool SetFont(const wxFont& font) {
			if (m_buttonSelect) {
				m_buttonSelect->SetFont(font);
			}
			if (m_buttonOpen) {
				m_buttonOpen->SetFont(font);
			}
			if (m_buttonClear) {
				m_buttonClear->SetFont(font);
			}
			return wxTextCtrl::SetFont(font);
		}

		virtual bool Enable(bool enable = true) {
			m_buttonSelect->Enable(enable);
			m_buttonOpen->Enable(enable);
			m_buttonClear->Enable(enable);
			return wxTextCtrl::Enable(enable);
		}

#if wxUSE_TOOLTIPS
		virtual void DoSetToolTipText(const wxString &tip) override {
			m_buttonSelect->SetToolTip(tip);
			m_buttonOpen->SetToolTip(tip);
			m_buttonClear->SetToolTip(tip);
		}

		virtual void DoSetToolTip(wxToolTip *tip) override {
			m_buttonSelect->SetToolTip(tip);
			m_buttonOpen->SetToolTip(tip);
			m_buttonClear->SetToolTip(tip);
		}
#endif // wxUSE_TOOLTIPS
	};

private:

	wxBoxSizer *m_boxSizer = NULL;
	wxStaticText *m_staticText = NULL;
	wxTextCtrl *m_textCtrl = NULL;

	wxTextButtonCtrl *m_winButton = NULL;

	bool m_dvcMode;

	bool m_selbutton;
	bool m_openbutton;
	bool m_clearbutton;

	bool m_passwordMode;
	bool m_multilineMode;
	bool m_textEditMode;

private:

	void UpdateControlStyle() {

		if (m_textCtrl) {

			long style = m_textCtrl->GetWindowStyle();
			if (m_multilineMode) {
				m_textCtrl->SetWindowStyle(style | wxTE_MULTILINE);
			}
			else {
				m_textCtrl->SetWindowStyle(style & (~wxTE_MULTILINE));
			}

			if (m_passwordMode) {
				m_textCtrl->SetWindowStyle(style | wxTE_PASSWORD);
#if defined(__WXMSW__)
				WXHWND hWnd = (WXHWND)m_textCtrl->GetHandle();
				SendMessage(hWnd, EM_SETPASSWORDCHAR, 0x25cf, 0); // 0x25cf is ● character
#endif
			}
			else {
				m_textCtrl->SetWindowStyle(style & (~wxTE_PASSWORD));
#if defined(__WXMSW__)
				WXHWND hWnd = (WXHWND)m_textCtrl->GetHandle();
				SendMessage(hWnd, EM_SETPASSWORDCHAR, 0, 0); // 0x25cf is ● character
#endif
			}

			if (m_textEditMode) {
#if defined(__WXMSW__)
				WXHWND hWnd = (WXHWND)m_textCtrl->GetHandle();
				SendMessage(hWnd, EM_SETREADONLY, 0, 0);
#endif
				m_textCtrl->SetWindowStyle(style & (~wxTE_READONLY));
			}
			else {
#if defined(__WXMSW__)
				WXHWND hWnd = (WXHWND)m_textCtrl->GetHandle();
				SendMessage(hWnd, EM_SETREADONLY, 1, 0);
#endif
				m_textCtrl->SetWindowStyle(style | wxTE_READONLY);
			}

			m_textCtrl->Update();
		}
	}

	void CreateControl(const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
		const wxString &val = wxEmptyString)
	{
		m_boxSizer = new wxBoxSizer(wxHORIZONTAL);

		if (!m_dvcMode) {
			m_staticText = new wxStaticText(this, wxID_ANY, wxEmptyString, pos, wxDefaultSize);
			m_staticText->Wrap(-1);
			m_boxSizer->Add(m_staticText, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
		}

		m_textCtrl = new wxTextRawCtrl(this, wxID_ANY, val, pos, wxSize(-1, m_dvcMode ? buttonSize + 3 : buttonSize), wxTE_PROCESS_ENTER | wxTE_PROCESS_TAB | wxBORDER_SIMPLE);

		m_boxSizer->Add(m_textCtrl, 1, wxEXPAND);
		m_winButton = new wxTextButtonCtrl;
		m_winButton->SetButtonSelect(m_selbutton);
		m_winButton->SetButtonOpen(m_openbutton);
		m_winButton->SetButtonClear(m_clearbutton);
		m_winButton->SetDVCMode(m_dvcMode);
		m_winButton->Create(this, pos, wxSize(-1, m_dvcMode ? buttonSize + 3 : buttonSize));
		m_boxSizer->Add(m_winButton, 0, wxALIGN_NOT);
		UpdateControlStyle();
	}

	//events:
	void OnKillFocus(wxFocusEvent &event)
	{
		/*wxWindow *focusedWnd = wxWindow::FindFocus();

		if (focusedWnd != m_textCtrl) {

			wxCommandEvent evt(wxEVT_TEXT_ENTER, m_textCtrl->GetId());
			evt.SetEventObject(m_textCtrl);

			switch (m_clientDataType)
			{
			case wxClientData_Void:
				evt.SetClientData(m_textCtrl->GetClientData());
				break;

			case wxClientData_Object:
				evt.SetClientObject(m_textCtrl->GetClientObject());
				break;

			case wxClientData_None:
				// nothing to do
				;
			}

			evt.SetString(m_textCtrl->GetValue());
			wxPostEvent(m_textCtrl, evt);
		}*/

		event.Skip();
	}

	void OnKeyEvent(wxKeyEvent& event)
	{
		event.Skip();
	}
	void OnCharEvent(wxKeyEvent& event)
	{
		event.Skip();
	}
	void OnFocusEvent(wxFocusEvent& event)
	{
		Refresh();
	}

public:

	wxTextContainerCtrl() :
		m_selbutton(true), m_openbutton(false), m_clearbutton(false),
		m_passwordMode(false), m_multilineMode(false), m_textEditMode(true),
		m_dvcMode(false)
	{
	}

	wxTextContainerCtrl(wxWindow *parent,
		wxWindowID id = wxID_ANY,
		const wxString &val = wxEmptyString,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = wxBORDER_NONE) :
		m_selbutton(true), m_openbutton(false), m_clearbutton(false),
		m_passwordMode(false), m_multilineMode(false), m_textEditMode(true),
		m_dvcMode(false)
	{
		Create(parent, id, val, pos, size, style);
	}

	virtual ~wxTextContainerCtrl() {

		wxWindow::Unbind(wxEVT_KEY_DOWN, &wxTextContainerCtrl::OnKeyEvent, this);
		wxWindow::Unbind(wxEVT_CHAR, &wxTextContainerCtrl::OnCharEvent, this);
		wxWindow::Unbind(wxEVT_SET_FOCUS, &wxTextContainerCtrl::OnFocusEvent, this);
		wxWindow::Unbind(wxEVT_KILL_FOCUS, &wxTextContainerCtrl::OnFocusEvent, this);

		if (m_textCtrl) {
			m_textCtrl->Unbind(wxEVT_KILL_FOCUS, &wxTextContainerCtrl::OnKillFocus, this);
		}
	}

	bool Create(wxWindow *parent = NULL,
		wxWindowID id = wxID_ANY,
		const wxString &val = wxEmptyString,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0)
	{
		if (!wxWindow::Create(parent, id, pos, size, style))
			return false;

		wxWindow::Bind(wxEVT_KEY_DOWN, &wxTextContainerCtrl::OnKeyEvent, this);
		wxWindow::Bind(wxEVT_CHAR, &wxTextContainerCtrl::OnCharEvent, this);
		wxWindow::Bind(wxEVT_SET_FOCUS, &wxTextContainerCtrl::OnFocusEvent, this);
		wxWindow::Bind(wxEVT_KILL_FOCUS, &wxTextContainerCtrl::OnFocusEvent, this);

		wxWindow::SetSizeHints(wxDefaultSize, wxDefaultSize);

		CreateControl(pos, size, val);

		m_textCtrl->Bind(wxEVT_KILL_FOCUS, &wxTextContainerCtrl::OnKillFocus, this);

		wxWindow::SetSizer(m_boxSizer);
		wxWindow::Layout();
		wxWindow::Centre(wxBOTH);

		if (pos != wxDefaultPosition)
			wxWindow::Move(pos);

		return true;
	}

	void SetDVCMode(bool dvc)
	{
		if (m_winButton) {
			m_winButton->SetDVCMode(m_dvcMode);
		}
		m_dvcMode = dvc;
	}

	// Bind functors to an event:
	template <typename Functor, typename EventHandler>
	void BindButtonSelect(const Functor &functor, EventHandler handler) { m_winButton->BindButtonSelect(functor, handler); }
	template <typename Functor, typename EventHandler>
	void BindButtonOpen(const Functor &functor, EventHandler handler) { m_winButton->BindButtonOpen(functor, handler); }
	template <typename Functor, typename EventHandler>
	void BindButtonClear(const Functor &functor, EventHandler handler) { m_winButton->BindButtonClear(functor, handler); }
	template <typename Functor, typename EventHandler>
	void BindTextEnter(const Functor &functor, EventHandler handler) { m_textCtrl->Bind(wxEVT_COMMAND_TEXT_ENTER, functor, handler); }
	template <typename Functor, typename EventHandler>
	void BindTextUpdated(const Functor& functor, EventHandler handler) { m_textCtrl->Bind(wxEVT_COMMAND_TEXT_UPDATED, functor, handler); }
	template <typename Functor, typename EventHandler>
	void BindKillFocus(const Functor &functor, EventHandler handler) { m_textCtrl->Bind(wxEVT_KILL_FOCUS, functor, handler); }

	// Unbind functors to an event:
	template <typename Functor, typename EventHandler>
	void UnbindButtonSelect(const Functor &functor, EventHandler handler) { m_winButton->UnbindButtonSelect(functor, handler); }
	template <typename Functor, typename EventHandler>
	void UnbindButtonOpen(const Functor &functor, EventHandler handler) { m_winButton->UnbindButtonOpen(functor, handler); }
	template <typename Functor, typename EventHandler>
	void UnbindButtonClear(const Functor &functor, EventHandler handler) { m_winButton->UnbindButtonClear(functor, handler); }
	template <typename Functor, typename EventHandler>
	void UnbindTextEnter(const Functor &functor, EventHandler handler) { m_textCtrl->Unbind(wxEVT_COMMAND_TEXT_ENTER, functor, handler); }
	template <typename Functor, typename EventHandler>
	void UnbindTextUpdated(const Functor& functor, EventHandler handler) { m_textCtrl->Unbind(wxEVT_COMMAND_TEXT_UPDATED, functor, handler); }
	template <typename Functor, typename EventHandler>
	void UnbindKillFocus(const Functor &functor, EventHandler handler) { m_textCtrl->Unbind(wxEVT_KILL_FOCUS, functor, handler); }

	void SetMultilineMode(bool mode) {
		m_multilineMode = mode;
		UpdateControlStyle();
	}

	bool GetMultilineMode() const {
		return m_multilineMode;
	}

	void SetPasswordMode(bool mode) {
		m_passwordMode = mode;
		UpdateControlStyle();
	}

	bool GetPasswordMode() const {
		return m_passwordMode;
	}

	void SetTextEditMode(bool mode) {
		m_textEditMode = mode;
		UpdateControlStyle();
	}

	bool GetTextEditMode() const {
		return m_textEditMode;
	}

	void SetTextLabel(const wxString& label) {
		m_staticText->SetLabel(label);
	}

	wxString GetTextLabel() const {
		return m_staticText->GetLabel();
	}

	void SetTextValue(const wxString& label) {
		m_textCtrl->SetValue(label);
	}

	wxString GetTextValue() const {
		return m_textCtrl->GetValue();
	}

	void CalculateButton() {
		m_textCtrl->SetMinSize(wxSize(-1, m_textCtrl->GetMinSize().y));
		m_winButton->Hide();
		int x = wxWindow::GetBestSize().x + 20;// -5;
		m_winButton->Show();
		int delta = m_winButton->CalculateButton();
		x -= delta;
		if (!m_dvcMode) {
			wxSize minSize = m_staticText->GetMinSize(); 
			if (minSize != wxDefaultSize) {
				x -= minSize.x;
			}
		}
		m_textCtrl->SetMinSize(wxSize(x, m_textCtrl->GetMinSize().y));
	}

	//buttons:
	void SetButtonSelect(bool select) {
		if (m_winButton) {
			m_winButton->SetButtonSelect(select);
		}
		m_selbutton = select;
	}

	bool HasButtonSelect() const {
		return m_selbutton;
	}

	void SetButtonOpen(bool select = true) {
		if (m_winButton) {
			m_winButton->SetButtonOpen(select);
		}
		m_openbutton = select;
	}

	bool HasButtonOpen() const {
		return m_openbutton;
	}

	void SetButtonClear(bool select = true) {
		if (m_winButton) {
			m_winButton->SetButtonClear(select);
		}
		m_clearbutton = select;
	}

	bool HasButtonClear() const {
		return m_clearbutton;
	}

	// overridden base class virtuals
	virtual bool SetBackgroundColour(const wxColour& colour) {
		if (!m_dvcMode) {
			if (m_textCtrl) {
				m_staticText->SetBackgroundColour(colour);
			}
		}
		m_textCtrl->SetBackgroundColour(colour);
		m_winButton->SetBackgroundColour(colour);
		return wxWindow::SetBackgroundColour(colour);
	}

	virtual bool SetForegroundColour(const wxColour& colour) {
		if (!m_dvcMode) {
			if (m_textCtrl) {
				m_staticText->SetForegroundColour(colour);
			}
		}
		m_textCtrl->SetForegroundColour(colour);
		m_winButton->SetForegroundColour(colour);
		return wxWindow::SetForegroundColour(colour);
	}

	virtual bool SetFont(const wxFont& font) {
		if (!m_dvcMode) {
			if (m_textCtrl) {
				m_staticText->SetFont(font);
			}
		}
		if (m_textCtrl) {
			m_textCtrl->SetFont(font);
		}
		if (m_winButton) {
			m_winButton->SetFont(font);
		}
		return wxWindow::SetFont(font);
	}

	virtual bool Enable(bool enable = true) {	
		bool result = m_textCtrl->Enable(enable);
		m_winButton->Enable(enable);
		return result;
	}

#if wxUSE_TOOLTIPS
	virtual void DoSetToolTipText(const wxString &tip) override {
		if (!m_dvcMode) {
			m_staticText->SetToolTip(tip);
		}
		m_textCtrl->SetToolTip(tip);
		m_winButton->SetToolTip(tip);
	}

	virtual void DoSetToolTip(wxToolTip *tip) override {
		if (!m_dvcMode) {
			m_staticText->SetToolTip(tip);
		}
		m_textCtrl->SetToolTip(tip);
		m_winButton->SetToolTip(tip);
	}
#endif // wxUSE_TOOLTIPS

	virtual void SetInsertionPointEnd() { 
		m_textCtrl->SetInsertionPointEnd();
	}

	virtual void SetFocus() override {
		m_textCtrl->SetFocus();
	}

	virtual wxSize GetControlSize() const {
		 return m_textCtrl->GetSize() + 
			 m_winButton->GetSize();
	}

	virtual wxStaticText *GetStaticText() const { 
		return m_staticText;
	}

	virtual wxWindow* GetControl() const {
		return m_textCtrl;
	}

	virtual void AfterCalc() {
		CalculateButton(); 
	}

	/*#if wxUSE_VALIDATORS
		virtual void SetValidator(const wxValidator& validator) override {
			if (m_textCtrl) {
				m_textCtrl->SetValidator(validator);
			}
		}

		virtual wxValidator *GetValidator() override {
			return m_textCtrl ? m_textCtrl->GetValidator() : NULL;
		}
	#endif // wxUSE_VALIDATORS*/
};

#endif