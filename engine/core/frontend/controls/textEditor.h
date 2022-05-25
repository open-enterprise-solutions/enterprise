#ifndef _TEXTCTRL_H__
#define _TEXTCTRL_H__

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
	textCtrl_buttonList,
	textCtrl_buttonClear
};

#include "formatValidator/fvalnum.h"
#include "formatValidator/forstrnu.h"

#define buttonSize 20

#define dvcMode 0x0004096

class CTextCtrl : public wxWindow,
	public IDynamicBorder {

	wxDECLARE_DYNAMIC_CLASS(CTextCtrl);
	wxDECLARE_NO_COPY_CLASS(CTextCtrl);

	class CPanelButtons : public wxTextCtrl {

		wxDECLARE_DYNAMIC_CLASS(CPanelButtons);
		wxDECLARE_NO_COPY_CLASS(CPanelButtons);

		wxButton *m_buttonSelect = NULL;
		wxButton *m_buttonClear = NULL;
		wxButton *m_buttonList = NULL;

		bool m_dvcMode = false;

		bool m_selbutton = false;
		bool m_listbutton = false;
		bool m_clearbutton = false;

		friend class CTextCtrl;

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
			CalculateButtons();
			event.Skip();
		}

		int CalculateButtons()
		{
			// Use one two units smaller to match size of the combo's dropbutton.
			// (normally a bigger button is used because it looks better)
			wxSize bt_sz(buttonSize - 7, m_dvcMode ? buttonSize : buttonSize - 2);

			// Position of button.
			wxPoint bt_pos(0, 1);

			int delta = 0;

			if (m_buttonSelect->IsShown()) {
				m_buttonSelect->SetSize(bt_sz);
				m_buttonSelect->SetPosition(wxPoint(bt_pos.x + delta, bt_pos.y));
				delta += bt_sz.x;
			}

			if (m_buttonList->IsShown()) {
				m_buttonList->SetSize(bt_sz);
				m_buttonList->SetPosition(wxPoint(bt_pos.x + delta, bt_pos.y));
				delta += bt_sz.x;
			}

			if (m_buttonClear->IsShown()) {
				m_buttonClear->SetSize(bt_sz);
				m_buttonClear->SetPosition(wxPoint(bt_pos.x + delta, bt_pos.y));
				delta += bt_sz.x;
			}

			wxSize controlSize = wxTextCtrl::GetSize();
			wxTextCtrl::SetMaxSize(wxSize(delta > 0 ? delta + 1 : 0, controlSize.y));

			return delta;
		}

	public:

		CPanelButtons() : wxTextCtrl() {}

		virtual ~CPanelButtons() {

			m_buttonSelect = NULL;
			m_buttonList = NULL;
			m_buttonClear = NULL;

			wxTextCtrl::Unbind(wxEVT_SIZE, &CPanelButtons::OnSizeTextCtrl, this);

			if (m_parent->IsKindOf(CLASSINFO(CTextCtrl))) {
				((CTextCtrl *)m_parent)->m_textCtrl = NULL;
			}
		}

		bool Create(wxWindow *parent, const wxPoint& pos = wxDefaultPosition,
			const wxSize& size = wxDefaultSize, int style = wxBORDER_SIMPLE)
		{
			if (!wxTextCtrl::Create(parent, wxID_ANY, wxEmptyString, pos, size, style))
				return false;

			m_buttonSelect = new wxButton(this, textCtrl_buttonSelect, wxT("..."), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTRANSPARENT_WINDOW);
			m_buttonList = new wxButton(this, textCtrl_buttonList, wxT("↓"), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTRANSPARENT_WINDOW);
			m_buttonClear = new wxButton(this, textCtrl_buttonClear, wxT("×"), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTRANSPARENT_WINDOW);

			wxColour bgCol = GetBackgroundColour();

			m_buttonSelect->SetBackgroundColour(bgCol.ChangeLightness(80));
			m_buttonList->SetBackgroundColour(bgCol.ChangeLightness(85));
			m_buttonClear->SetBackgroundColour(bgCol.ChangeLightness(80));

			m_buttonSelect->SetLabelMarkup("<b>" + m_buttonSelect->GetLabelText() + "</b>");
			m_buttonList->SetLabelMarkup("<b>" + m_buttonList->GetLabelText() + "</b>");
			m_buttonClear->SetLabelMarkup("<b>" + m_buttonClear->GetLabelText() + "</b>");

			m_buttonSelect->Show(m_selbutton);
			m_buttonList->Show(m_listbutton);
			m_buttonClear->Show(m_clearbutton);

			CalculateButtons();

			wxTextCtrl::Bind(wxEVT_SIZE, &CPanelButtons::OnSizeTextCtrl, this);
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
		void BindButtonList(const Functor &functor, EventHandler handler) { m_buttonList->Bind(wxEVT_BUTTON, functor, handler, textCtrl_buttonList); }
		template <typename Functor, typename EventHandler>
		void BindButtonClear(const Functor &functor, EventHandler handler) { m_buttonClear->Bind(wxEVT_BUTTON, functor, handler, textCtrl_buttonClear); }

		// Unbind functors to an event:
		template <typename Functor, typename EventHandler>
		void UnbindButtonSelect(const Functor &functor, EventHandler handler) { m_buttonSelect->Unbind(wxEVT_BUTTON, functor, handler, textCtrl_buttonSelect); }
		template <typename Functor, typename EventHandler>
		void UnbindButtonList(const Functor &functor, EventHandler handler) { m_buttonList->Unbind(wxEVT_BUTTON, functor, handler, textCtrl_buttonList); }
		template <typename Functor, typename EventHandler>
		void UnbindButtonClear(const Functor &functor, EventHandler handler) { m_buttonClear->Unbind(wxEVT_BUTTON, functor, handler, textCtrl_buttonClear); }

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

		void SetButtonList(bool select = true) {
			m_listbutton = select;
			if (m_buttonList) {
				m_buttonList->Show(select);
			}
		}

		bool HasButtonList() const {
			return m_listbutton;
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
			if (m_buttonList->IsShown()) {
				m_buttonList->SetBackgroundColour(colour.ChangeLightness(colLight));
				if (colLight == 80) {
					colLight = 85;
				}
				else {
					colLight = 80;
				}
			}
			if (m_buttonList->IsShown()) {
				m_buttonClear->SetBackgroundColour(colour.ChangeLightness(colLight));
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
			m_buttonList->SetForegroundColour(colour);
			m_buttonClear->SetForegroundColour(colour);
			return wxTextCtrl::SetForegroundColour(colour);
		}

		virtual bool SetFont(const wxFont& font) {
			if (m_buttonSelect) {
				m_buttonSelect->SetFont(font);
			}
			if (m_buttonList) {
				m_buttonList->SetFont(font);
			}
			if (m_buttonClear) {
				m_buttonClear->SetFont(font);
			}
			return wxTextCtrl::SetFont(font);
		}

		virtual bool Enable(bool enable = true) {
			m_buttonSelect->Enable(enable);
			m_buttonList->Enable(enable);
			m_buttonClear->Enable(enable);
			return wxTextCtrl::Enable(enable);
		}

#if wxUSE_TOOLTIPS
		virtual void DoSetToolTipText(const wxString &tip) override {
			m_buttonSelect->SetToolTip(tip);
			m_buttonList->SetToolTip(tip);
			m_buttonClear->SetToolTip(tip);
		}

		virtual void DoSetToolTip(wxToolTip *tip) override {
			m_buttonSelect->SetToolTip(tip);
			m_buttonList->SetToolTip(tip);
			m_buttonClear->SetToolTip(tip);
		}
#endif // wxUSE_TOOLTIPS
	};

	class CTextCtrlPopupWindow : public wxPopupTransientWindow {

		wxDECLARE_DYNAMIC_CLASS(CTextCtrlPopupWindow);
		wxDECLARE_NO_COPY_CLASS(CTextCtrlPopupWindow);

		wxListBox *m_listBox;
	public:

		CTextCtrlPopupWindow(wxWindow *textCtrl = NULL) : wxPopupTransientWindow(textCtrl, wxBORDER_SIMPLE)
		{
			wxPopupTransientWindow::SetSizeHints(wxDefaultSize, wxDefaultSize);

			wxBoxSizer* sizerListBox = new wxBoxSizer(wxVERTICAL);

			m_listBox = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, NULL, wxBORDER_SIMPLE | wxLB_SINGLE);
			m_listBox->SetForegroundColour(textCtrl->GetForegroundColour());
			m_listBox->SetBackgroundColour(textCtrl->GetBackgroundColour());

			sizerListBox->Add(m_listBox, 1, wxALL | wxEXPAND, 5);

			m_listBox->SetFont(textCtrl->GetFont());

			// Bind Events
			m_listBox->Bind(wxEVT_COMMAND_LISTBOX_SELECTED, &CTextCtrlPopupWindow::OnListBox, this);

			wxPopupTransientWindow::SetSizer(sizerListBox);
			wxPopupTransientWindow::Layout();
			wxPopupTransientWindow::Centre(wxBOTH);
		}

		virtual ~CTextCtrlPopupWindow() {}

		void AddString(const wxString &item) {
			m_listBox->AppendString(item);
			m_listBox->Select(0);
		}
		void ClearAll() { m_listBox->Clear(); }

	protected:

		// Virtual event handlers, overide them in your derived class
		virtual void OnListBox(wxCommandEvent& event)
		{
			/*wxCommandEvent redirectedEvent(event);
			redirectedEvent.SetEventObject(m_textOwner);

			if (!m_textOwner->GetEventHandler()->ProcessEvent(redirectedEvent)) {
				event.Skip();
			}*/

			event.Skip();
		}
	};

private:

	wxBoxSizer *m_boxSizer = NULL;
	wxStaticText *m_staticText = NULL;
	wxTextCtrl *m_textCtrl = NULL;

	CPanelButtons *m_buttons = NULL;
	CTextCtrlPopupWindow *m_winPopup = NULL;

	bool m_dvcMode;

	bool m_selbutton;
	bool m_listbutton;
	bool m_clearbutton;

	bool m_passwordMode;
	bool m_multilineMode;
	bool m_textEditMode;

private:

	void UpdateStyleControls() {

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

	void CreateControls(const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
		const wxString &val = wxEmptyString)
	{
		m_boxSizer = new wxBoxSizer(wxHORIZONTAL);

		if (!m_dvcMode) {
			m_staticText = new wxStaticText(this, wxID_ANY, wxEmptyString, pos, wxDefaultSize);
			m_staticText->Wrap(-1);
			m_boxSizer->Add(m_staticText, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
		}

		m_textCtrl = new wxTextCtrl(this, wxID_ANY, val, pos, wxSize(-1, m_dvcMode ? buttonSize + 3 : buttonSize), wxTE_PROCESS_ENTER | wxTE_PROCESS_TAB | wxBORDER_SIMPLE);

		m_boxSizer->Add(m_textCtrl, 1, wxEXPAND);
		m_buttons = new CPanelButtons();
		m_buttons->SetButtonSelect(m_selbutton);
		m_buttons->SetButtonList(m_listbutton);
		m_buttons->SetButtonClear(m_clearbutton);
		m_buttons->SetDVCMode(m_dvcMode);
		m_buttons->Create(this, pos, wxSize(-1, m_dvcMode ? buttonSize + 3 : buttonSize));
		m_boxSizer->Add(m_buttons, 0, wxALIGN_NOT);
		UpdateStyleControls();
	}

	bool IsPopupShown() const {
		return m_winPopup->IsShown();
	}

	void HidePopup() {
		if (m_winPopup->Hide()) {
		}
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
		if (IsPopupShown()) {
			// pass it to the popped up control
			m_winPopup->GetEventHandler()->ProcessEvent(event);
		}
		else {
			event.Skip();
		}
		event.Skip();
	}
	void OnCharEvent(wxKeyEvent& event)
	{
		if (IsPopupShown()) {
			// pass it to the popped up control
			m_winPopup->GetEventHandler()->ProcessEvent(event);
		}
		else {
			event.Skip();
		}
		event.Skip();
	}
	void OnFocusEvent(wxFocusEvent& event)
	{
		if (event.GetEventType() == wxEVT_KILL_FOCUS) {
			HidePopup();
		}

		Refresh();
	}

public:

	CTextCtrl() :
		m_selbutton(true), m_listbutton(false), m_clearbutton(false),
		m_passwordMode(false), m_multilineMode(false), m_textEditMode(true),
		m_dvcMode(false)
	{
	}

	CTextCtrl(wxWindow *parent,
		wxWindowID id = wxID_ANY,
		const wxString &val = wxEmptyString,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = wxBORDER_SIMPLE | wxTAB_TRAVERSAL) :
		m_selbutton(true), m_listbutton(false), m_clearbutton(false),
		m_passwordMode(false), m_multilineMode(false), m_textEditMode(true),
		m_dvcMode(false)
	{
		Create(parent, id, val, pos, size, style);
	}

	virtual ~CTextCtrl() {

		wxWindow::Unbind(wxEVT_KEY_DOWN, &CTextCtrl::OnKeyEvent, this);
		wxWindow::Unbind(wxEVT_CHAR, &CTextCtrl::OnCharEvent, this);
		wxWindow::Unbind(wxEVT_SET_FOCUS, &CTextCtrl::OnFocusEvent, this);
		wxWindow::Unbind(wxEVT_KILL_FOCUS, &CTextCtrl::OnFocusEvent, this);

		if (m_textCtrl) {
			m_textCtrl->Unbind(wxEVT_KILL_FOCUS, &CTextCtrl::OnKillFocus, this);
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

		m_winPopup = new CTextCtrlPopupWindow(this);

		wxWindow::Bind(wxEVT_KEY_DOWN, &CTextCtrl::OnKeyEvent, this);
		wxWindow::Bind(wxEVT_CHAR, &CTextCtrl::OnCharEvent, this);
		wxWindow::Bind(wxEVT_SET_FOCUS, &CTextCtrl::OnFocusEvent, this);
		wxWindow::Bind(wxEVT_KILL_FOCUS, &CTextCtrl::OnFocusEvent, this);

		wxWindow::SetSizeHints(wxDefaultSize, wxDefaultSize);

		CreateControls(pos, size, val);

		m_textCtrl->Bind(wxEVT_KILL_FOCUS, &CTextCtrl::OnKillFocus, this);

		wxWindow::SetSizer(m_boxSizer);
		wxWindow::Layout();
		wxWindow::Centre(wxBOTH);

		if (pos != wxDefaultPosition) {
			wxWindow::Move(pos);
		}

		return true;
	}

	void SetDVCMode(bool dvc)
	{
		if (m_buttons) {
			m_buttons->SetDVCMode(m_dvcMode);
		}
		m_dvcMode = dvc;
	}

	void ShowPopup()
	{
		if (IsPopupShown()) {
			HidePopup();
		}

		m_winPopup->ClearAll();
		m_winPopup->AddString("test1");
		m_winPopup->AddString("test2");
		m_winPopup->AddString("test3");
		m_winPopup->AddString("test4");
		m_winPopup->AddString("test5");
		m_winPopup->AddString("test6");

		wxSize ctrlSz = GetSize();

		int screenHeight = wxSystemSettings::GetMetric(wxSYS_SCREEN_Y);
		wxPoint scrPos = GetScreenPosition();

		// Space above and below
		int spaceAbove = scrPos.y;
		int spaceBelow = screenHeight - spaceAbove - ctrlSz.y;

		int maxHeightPopup = spaceBelow;

		if (spaceAbove > spaceBelow) maxHeightPopup = spaceAbove;

		// Width
		int widthPopup = ctrlSz.x;

		if (widthPopup < -1) widthPopup = -1;

		wxSize adjustedSize = wxSize(widthPopup, 250);

		m_winPopup->SetSize(adjustedSize);
		m_winPopup->Move(0, 0);

		//
		// Reposition and resize popup window
		//

		wxSize szp = m_winPopup->GetSize();

		int popupX;
		int popupY = scrPos.y + ctrlSz.y;

		// Default anchor is wxLEFT
		int anchorSide = wxLEFT;

		int rightX = scrPos.x + ctrlSz.x - szp.x;
		int leftX = scrPos.x;

		if (wxTheApp->GetLayoutDirection() == wxLayout_RightToLeft)
			leftX -= ctrlSz.x;

		int screenWidth = wxSystemSettings::GetMetric(wxSYS_SCREEN_X);

		// If there is not enough horizontal space, anchor on the other side.
		// If there is no space even then, place the popup at x 0.
		if (anchorSide == wxRIGHT)
		{
			if (rightX < 0)
			{
				if ((leftX + szp.x) < screenWidth)
					anchorSide = wxLEFT;
				else
					anchorSide = 0;
			}
		}
		else
		{
			if ((leftX + szp.x) >= screenWidth)
			{
				if (rightX >= 0)
					anchorSide = wxRIGHT;
				else
					anchorSide = 0;
			}
		}

		// Select x coordinate according to the anchor side
		if (anchorSide == wxRIGHT)
			popupX = rightX;
		else if (anchorSide == wxLEFT)
			popupX = leftX;
		else
			popupX = 0;

		if (spaceBelow < szp.y)
		{
			popupY = scrPos.y - szp.y;
		}

		m_winPopup->SetBackgroundColour(GetBackgroundColour());

		wxRect popupWinRect(popupX - 2, popupY, szp.x, szp.y);
		m_winPopup->SetSize(popupWinRect);
		m_winPopup->Popup();
		m_winPopup->Refresh();
	}

	// Bind functors to an event:
	template <typename Functor, typename EventHandler>
	void BindButtonSelect(const Functor &functor, EventHandler handler) { m_buttons->BindButtonSelect(functor, handler); }
	template <typename Functor, typename EventHandler>
	void BindButtonList(const Functor &functor, EventHandler handler) { m_buttons->BindButtonList(functor, handler); }
	template <typename Functor, typename EventHandler>
	void BindButtonClear(const Functor &functor, EventHandler handler) { m_buttons->BindButtonClear(functor, handler); }
	template <typename Functor, typename EventHandler>
	void BindTextCtrl(const Functor &functor, EventHandler handler) { m_textCtrl->Bind(wxEVT_COMMAND_TEXT_ENTER, functor, handler); }
	template <typename Functor, typename EventHandler>
	void BindKillFocus(const Functor &functor, EventHandler handler) { m_textCtrl->Bind(wxEVT_KILL_FOCUS, functor, handler); }

	// Unbind functors to an event:
	template <typename Functor, typename EventHandler>
	void UnbindButtonSelect(const Functor &functor, EventHandler handler) { m_buttons->UnbindButtonSelect(functor, handler); }
	template <typename Functor, typename EventHandler>
	void UnbindButtonList(const Functor &functor, EventHandler handler) { m_buttons->UnbindButtonList(functor, handler); }
	template <typename Functor, typename EventHandler>
	void UnbindButtonClear(const Functor &functor, EventHandler handler) { m_buttons->UnbindButtonClear(functor, handler); }
	template <typename Functor, typename EventHandler>
	void UnbindTextCtrl(const Functor &functor, EventHandler handler) { m_textCtrl->Unbind(wxEVT_COMMAND_TEXT_ENTER, functor, handler); }
	template <typename Functor, typename EventHandler>
	void UnbindKillFocus(const Functor &functor, EventHandler handler) { m_textCtrl->Unbind(wxEVT_KILL_FOCUS, functor, handler); }

	void SetMultilineMode(bool mode) {
		m_multilineMode = mode;
		UpdateStyleControls();
	}

	bool GetMultilineMode() const {
		return m_multilineMode;
	}

	void SetPasswordMode(bool mode) {
		m_passwordMode = mode;
		UpdateStyleControls();
	}

	bool GetPasswordMode() const {
		return m_passwordMode;
	}

	void SetTextEditMode(bool mode) {
		m_textEditMode = mode;
		UpdateStyleControls();
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

	void CalculateButtons() {
		m_textCtrl->SetMinSize(wxSize(-1, m_textCtrl->GetMinSize().y));
		m_buttons->Hide();
		int x = wxWindow::GetBestSize().x - 5;
		m_buttons->Show();
		int delta = m_buttons->CalculateButtons();
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
		if (m_buttons) {
			m_buttons->SetButtonSelect(select);
		}
		m_selbutton = select;
	}

	bool HasButtonSelect() const {
		return m_selbutton;
	}

	void SetButtonList(bool select = true) {
		if (m_buttons) {
			m_buttons->SetButtonList(select);
		}
		m_listbutton = select;
	}

	bool HasButtonList() const {
		return m_listbutton;
	}

	void SetButtonClear(bool select = true) {
		if (m_buttons) {
			m_buttons->SetButtonClear(select);
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
		m_buttons->SetBackgroundColour(colour);
		return wxWindow::SetBackgroundColour(colour);
	}

	virtual bool SetForegroundColour(const wxColour& colour) {
		if (!m_dvcMode) {
			if (m_textCtrl) {
				m_staticText->SetForegroundColour(colour);
			}
		}
		m_textCtrl->SetForegroundColour(colour);
		m_buttons->SetForegroundColour(colour);
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
		if (m_buttons) {
			m_buttons->SetFont(font);
		}
		return wxWindow::SetFont(font);
	}

	virtual bool Enable(bool enable = true) {
		return m_textCtrl->Enable(enable) &&
			m_buttons->Enable(enable);
	}

#if wxUSE_TOOLTIPS
	virtual void DoSetToolTipText(const wxString &tip) override {
		if (!m_dvcMode) {
			m_staticText->SetToolTip(tip);
		}
		m_textCtrl->SetToolTip(tip);
		m_buttons->SetToolTip(tip);
	}

	virtual void DoSetToolTip(wxToolTip *tip) override {
		if (!m_dvcMode) {
			m_staticText->SetToolTip(tip);
		}
		m_textCtrl->SetToolTip(tip);
		m_buttons->SetToolTip(tip);
	}
#endif // wxUSE_TOOLTIPS

	virtual void SetInsertionPointEnd() { m_textCtrl->SetInsertionPointEnd(); }

	virtual wxStaticText *GetStaticText() const { return m_staticText; }
	virtual void AfterCalc() { CalculateButtons(); }

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