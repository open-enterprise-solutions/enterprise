#ifndef _authorization_h__
#define _authorization_h__

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/bmpcbox.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>

#include "frontend/frontend.h"

class FRONTEND_API ibDialogAuthentication : public wxDialog {
public:

	ibDialogAuthentication(wxWindow* parent = nullptr, wxWindowID id = wxID_ANY, const wxString& title = _("Authentication to information base"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(420, 130), long style = wxDEFAULT_DIALOG_STYLE);

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

// Standalone interactive-auth entry point. Shows ibDialogAuthentication
// modal with the prefilled user/password; dialog's OK handler calls
// AuthenticateUser + InstallUser itself under the main thread's
// ibSessionScope, so m_userInfo / m_sessionRawPassword on the scoped
// session are populated on confirm. Returns true if user confirmed
// (ShowModal != wxID_CANCEL), false on cancel. Registered as
// ibApplicationInstance's InteractiveAuthHook by GUI apps (enterprise /
// designer) at startup so the Authenticate() fallback can prompt
// without depending on the main frame's lifecycle.
FRONTEND_API bool ibPromptAuthenticationDialog(const wxString& userName, const wxString& userPassword);

#endif