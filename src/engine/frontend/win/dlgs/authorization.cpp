#include "authorization.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/appData.h"

///////////////////////////////////////////////////////////////////////////

#include "frontend/visualView/ctrl/frame.h"

ibDialogAuthentication::ibDialogAuthentication(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : wxDialog(parent, id, title, pos, size, style)
{
	wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);
	this->SetBackgroundColour(wxColour(184, 201, 212));   // #B8C9D4 powder-blue dialog

	// Consistent DIP-aware padding — previous layout used 5/9/10 px which looked
	// asymmetric and didn't scale on HiDPI.
	const int kPad = FromDIP(6);

	wxBoxSizer* bSizer = new wxBoxSizer(wxVERTICAL);

	// Label + field grid: 2 columns, second column stretches so the combo /
	// password fill the available width.
	wxFlexGridSizer* grid = new wxFlexGridSizer(/*rows*/ 0, /*cols*/ 2, kPad, kPad);
	grid->AddGrowableCol(1, 1);

	m_staticTextLogin = new wxStaticText(this, wxID_ANY, _("Login:"));
	m_comboBoxLogin   = new wxBitmapComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize);
	for (const auto& userInfo : ibUserInfo::ListAll())
		m_comboBoxLogin->Append(userInfo.m_strUserName, ibBackendPicture::GetPicture(g_picUserCLSID));

	m_staticTextPassword = new wxStaticText(this, wxID_ANY, _("Password:"));
	m_textCtrlPassword   = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);

	grid->Add(m_staticTextLogin,    0, wxALIGN_CENTER_VERTICAL);
	grid->Add(m_comboBoxLogin,      1, wxEXPAND);
	grid->Add(m_staticTextPassword, 0, wxALIGN_CENTER_VERTICAL);
	grid->Add(m_textCtrlPassword,   1, wxEXPAND);

	bSizer->Add(grid, 1, wxALL | wxEXPAND, kPad * 2);

	wxStdDialogButtonSizer* bSizerButtons = new wxStdDialogButtonSizer();
	m_buttonOK = new wxButton(this, wxID_OK);
	bSizerButtons->AddButton(m_buttonOK);
	m_buttonCancel = new wxButton(this, wxID_CANCEL);
	bSizerButtons->AddButton(m_buttonCancel);
	bSizerButtons->Realize();

	bSizer->Add(bSizerButtons, 0, wxALL | wxALIGN_RIGHT, kPad);

	wxDialog::SetSizer(bSizer);
	bSizer->SetSizeHints(this);

	// Ensure the dialog is at least wide enough for the title bar text
	// (localised titles may exceed the content width produced by SetSizeHints).
	const wxSize minSize = FromDIP(wxSize(420, -1));
	if (GetSize().x < minSize.x)
		SetSize(minSize.x, GetSize().y);

	wxDialog::Layout();

	wxDialog::Centre(wxBOTH);

	wxIcon dlg_icon;
	dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_picAuthenticationCLSID));

	wxDialog::SetIcon(dlg_icon);
	wxDialog::SetFocus();

	// Connect Events
	m_buttonOK->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [&](wxCommandEvent& event) {
		ibUserInfo info;
		if (!appData->Login(m_comboBoxLogin->GetValue(), m_textCtrlPassword->GetValue(), info)) {
			wxMessageBox(_("Wrong user or password entered!"));
			return;
		}
		event.Skip();
		}
	);
}

bool ibPromptAuthenticationDialog(const wxString& userName, const wxString& userPassword)
{
	if (appData == nullptr)
		return false;

	ibDialogAuthentication dlg;
	dlg.SetLogin(userName);
	dlg.SetPassword(userPassword);

	return dlg.ShowModal() != wxID_CANCEL;
}
