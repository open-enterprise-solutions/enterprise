#ifndef _authorization_h__
#define _authorization_h__

#include <vector>

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/bmpcbox.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>

#include "frmclient/frmclient.h"

// THE LOGIN WINDOW — the desktop's (frontend/win/dlgs/authorization), handed the names the server offers (users) instead
// of reading the base itself. It asks; the main window logs in with what it was told, and asks again when refused.
class FRMCLIENT_API ibDialogAuthentication : public wxDialog {
public:

	ibDialogAuthentication(const std::vector<wxString>& users, wxWindow* parent = nullptr, wxWindowID id = wxID_ANY, const wxString& title = _("Authentication to information base"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(420, 130), long style = wxDEFAULT_DIALOG_STYLE);

	wxString GetLogin() const { return m_comboBoxLogin->GetValue(); }
	void SetLogin(const wxString& login) { m_comboBoxLogin->SetValue(login); }
	wxString GetPassword() const { return m_textCtrlPassword->GetValue(); }
	void SetPassword(const wxString& password) { m_textCtrlPassword->SetValue(password); }

private:

	wxStaticText* m_staticTextLogin;
	wxStaticText* m_staticTextPassword;
	wxBitmapComboBox* m_comboBoxLogin;
	wxTextCtrl* m_textCtrlPassword;
	wxButton* m_buttonOK;
	wxButton* m_buttonCancel;
};

#endif
