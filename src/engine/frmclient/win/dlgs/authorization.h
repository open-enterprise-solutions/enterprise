#ifndef _authorization_h__
#define _authorization_h__

#include <vector>

#include "deskgui/univ/dialog.h"
#include <wx/sizer.h>
#include "deskgui/univ/ctrl/button.h"
#include "deskgui/univ/ctrl/comboBox.h"
#include "deskgui/univ/ctrl/textCtrl.h"
#include "deskgui/univ/ctrl/staticText.h"

#include "frmclient/frmclient.h"

// THE LOGIN WINDOW — the old desktop's, handed the names the server offers (users) instead
// of reading the base itself. It asks; the main window logs in with what it was told, and asks again when refused.
class FRMCLIENT_API ibDialogAuthentication : public ibDialog {
public:

	ibDialogAuthentication(const std::vector<ibString>& users, wxWindow* parent = nullptr, wxWindowID id = wxID_ANY, const wxString& title = _("Authentication to information base"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(420, 130), long style = wxDEFAULT_DIALOG_STYLE);

	wxString GetLogin() const { return m_comboBoxLogin->GetValue(); }
	void SetLogin(const wxString& login) { m_comboBoxLogin->SetValue(login); }
	wxString GetPassword() const { return m_textCtrlPassword->GetValue(); }
	void SetPassword(const wxString& password) { m_textCtrlPassword->SetValue(password); }

private:

	ibStaticText* m_staticTextLogin;
	ibStaticText* m_staticTextPassword;
	ibComboBox* m_comboBoxLogin;
	ibTextCtrl* m_textCtrlPassword;
	ibButton* m_buttonOK;
	ibButton* m_buttonCancel;
};

#endif
