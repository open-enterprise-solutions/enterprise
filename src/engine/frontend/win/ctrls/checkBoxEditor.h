#ifndef _CHECK_BOX_H__
#define _CHECK_BOX_H__

#include <wx/panel.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>

#include "dynamicBorder.h"

class CCheckBox : public wxPanel,
	public IDynamicBorder {

	wxBoxSizer *m_boxSizer;

	wxStaticText *m_staticText;
	wxCheckBox *m_checkBox;

	void CreateControls() {
		m_boxSizer = new wxBoxSizer(wxHORIZONTAL);

		m_staticText = new wxStaticText(this, wxID_ANY, wxT("<labelText>"), wxDefaultPosition, wxDefaultSize);
		m_staticText->Wrap(-1);
		m_boxSizer->Add(m_staticText, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
		m_checkBox = new wxCheckBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 20));
		m_boxSizer->Add(m_checkBox, 1, wxEXPAND);
	}

public:

	CCheckBox(wxWindow *parent,
		wxWindowID id = wxID_ANY,
		const wxString &val = wxEmptyString,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = wxBORDER_THEME | wxTAB_TRAVERSAL) :
		wxPanel(parent, id, pos, size)
	{
		wxPanel::SetSizeHints(wxDefaultSize, wxDefaultSize);

		CreateControls();

		wxPanel::SetSizer(m_boxSizer);
		wxPanel::Layout();
		wxPanel::Centre(wxBOTH);
	}

	virtual ~CCheckBox() {
	}

	// Bind functors to an event:
	template <typename Functor, typename EventHandler>
	void BindCheckBoxCtrl(const Functor &functor, EventHandler handler) {
		m_checkBox->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, functor, handler);
	}

	// Unbind functors to an event:
	template <typename Functor, typename EventHandler>
	void UnbindCheckBoxCtrl(const Functor &functor, EventHandler handler) { 
		m_checkBox->Unbind(wxEVT_COMMAND_CHECKBOX_CLICKED, functor, handler);
	}

	void SetCheckBoxLabel(const wxString& label) {
		m_staticText->SetLabel(label);
	}

	wxString GetCheckBoxLabel() const {
		return m_staticText->GetLabel();
	}

	void SetCheckBoxValue(bool value) {
		m_checkBox->SetValue(value);
	}

	bool GetCheckBoxValue() const {
		return m_checkBox->GetValue();
	}

	// overridden base class virtuals
	virtual bool SetBackgroundColour(const wxColour& colour) {
		m_staticText->SetBackgroundColour(colour);
		m_checkBox->SetBackgroundColour(colour);
		return wxPanel::SetBackgroundColour(colour);
	}

	virtual bool SetForegroundColour(const wxColour& colour) {
		m_staticText->SetForegroundColour(colour);
		m_checkBox->SetForegroundColour(colour);
		return wxPanel::SetForegroundColour(colour);
	}

	virtual bool SetFont(const wxFont& font) {
		m_staticText->SetFont(font);
		m_checkBox->SetFont(font);
		return wxPanel::SetFont(font);
	}

	virtual bool Enable(bool enable = true) {
		/*m_staticText->Enable(enable);
		m_checkBox->Enable(enable);
		return wxPanel::Enable(enable);*/
		return m_checkBox->Enable(enable);
	}

	virtual void SetWindowStyleFlag(long style) override
	{
		m_boxSizer->Clear();

		if ((style & wxAlignment::wxALIGN_LEFT) != 0 || style == wxAlignment::wxALIGN_LEFT) {
			m_boxSizer->Add(m_checkBox, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
			m_boxSizer->Add(m_staticText, 1, wxEXPAND);
		}
		else if ((style & wxAlignment::wxALIGN_RIGHT || style == wxAlignment::wxALIGN_RIGHT) != 0) {
			m_boxSizer->Add(m_staticText, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
			m_boxSizer->Add(m_checkBox, 1, wxEXPAND);
		}

		wxPanel::SetWindowStyleFlag(style);
	}

#if wxUSE_TOOLTIPS
	virtual void DoSetToolTipText(const wxString &tip) override {
		m_staticText->SetToolTip(tip);
		m_checkBox->SetToolTip(tip);
	}

	virtual void DoSetToolTip(wxToolTip *tip) override {
		m_staticText->SetToolTip(tip);
		m_checkBox->SetToolTip(tip);
	}
#endif // wxUSE_TOOLTIPS

	virtual bool AllowCalc() const { 
		if ((GetWindowStyleFlag() & wxAlignment::wxALIGN_LEFT) != 0 ||
			GetWindowStyleFlag() == wxAlignment::wxALIGN_LEFT) {
			return false;
		}	
		return true; 
	};

	virtual wxSize GetControlSize() const {
		return m_checkBox->GetSize();
	}

	virtual wxStaticText *GetStaticText() const {
		return m_staticText;
	}

	virtual wxWindow* GetControl() const {
		return m_checkBox;
	}
};

#endif 