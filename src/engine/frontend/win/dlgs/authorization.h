#ifndef _authorization_h__
#define _authorization_h__

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/combobox.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>

#include "frontend/frontend.h"

class FRONTEND_API CDialogAuthentication : public wxDialog {

	wxStaticText* m_staticTextLogin;
	wxStaticText* m_staticTextPassword;
	wxComboBox* m_comboBoxLogin;
	wxTextCtrl* m_textCtrlPassword;
	wxButton* m_buttonOK;
	wxButton* m_buttonCancel;

public:

	CDialogAuthentication(wxWindow* parent = nullptr, wxWindowID id = wxID_ANY, const wxString& title = _("Authentication to information base"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(420, 130), long style = wxDEFAULT_DIALOG_STYLE);
	virtual ~CDialogAuthentication();

	wxString GetLogin() const { return m_comboBoxLogin->GetValue(); }
	void SetLogin(const wxString& login) { m_comboBoxLogin->SetValue(login); }
	wxString GetPassword() const { return m_textCtrlPassword->GetValue(); }
	void SetPassword(const wxString& password) { m_textCtrlPassword->SetValue(password); }

protected:

	// Virtual event handlers, overide them in your derived class
	virtual void OnOKButtonClick(wxCommandEvent& event);
	virtual void OnCancelButtonClick(wxCommandEvent& event);
};

#endif 