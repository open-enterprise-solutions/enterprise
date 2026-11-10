#include "authorization.h"

#include "frmclient/backend/backend_picture.h"
#include "core/picturePredefined.h"

///////////////////////////////////////////////////////////////////////////

ibDialogAuthentication::ibDialogAuthentication(const std::vector<ibString>& users, wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : ibDialog(parent, id, title, pos, size, style)
{
	ibDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);

	// Consistent DIP-aware padding — previous layout used 5/9/10 px which looked
	// asymmetric and didn't scale on HiDPI.
	const int kPad = FromDIP(6);

	wxBoxSizer* bSizer = new wxBoxSizer(wxVERTICAL);

	// Label + field grid: 2 columns, second column stretches so the combo /
	// password fill the available width.
	wxFlexGridSizer* grid = new wxFlexGridSizer(/*rows*/ 0, /*cols*/ 2, kPad, kPad);
	grid->AddGrowableCol(1, 1);

	m_staticTextLogin = new ibStaticText(this, wxID_ANY, _("Login:"));
	m_comboBoxLogin   = new wxBitmapComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize);
	for (const ibString& user : users)
		m_comboBoxLogin->Append(user, ibBackendPicture::GetPicture(g_picUserCLSID));

	m_staticTextPassword = new ibStaticText(this, wxID_ANY, _("Password:"));
	m_textCtrlPassword   = new ibTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);

	grid->Add(m_staticTextLogin,    0, wxALIGN_CENTER_VERTICAL);
	grid->Add(m_comboBoxLogin,      1, wxEXPAND);
	grid->Add(m_staticTextPassword, 0, wxALIGN_CENTER_VERTICAL);
	grid->Add(m_textCtrlPassword,   1, wxEXPAND);

	bSizer->Add(grid, 1, wxALL | wxEXPAND, kPad * 2);

	ibStdDialogButtonSizer* bSizerButtons = new ibStdDialogButtonSizer();
	m_buttonOK = new ibButton(this, wxID_OK);
	bSizerButtons->AddButton(m_buttonOK);
	m_buttonCancel = new ibButton(this, wxID_CANCEL);
	bSizerButtons->AddButton(m_buttonCancel);
	bSizerButtons->Realize();

	bSizer->Add(bSizerButtons, 0, wxALL | wxALIGN_RIGHT, kPad);

	ibDialog::SetSizer(bSizer);
	bSizer->SetSizeHints(this);

	// Ensure the dialog is at least wide enough for the title bar text
	// (localised titles may exceed the content width produced by SetSizeHints).
	const wxSize minSize = FromDIP(wxSize(420, -1));
	if (GetSize().x < minSize.x)
		SetSize(minSize.x, GetSize().y);

	ibDialog::Layout();

	ibDialog::Centre(wxBOTH);

	// The server sends no picture before the login: until it has, the window goes without an icon.
	const wxBitmap picture = ibBackendPicture::GetPicture(g_picAuthenticationCLSID);
	if (picture.IsOk()) {
		wxIcon dlg_icon;
		dlg_icon.CopyFromBitmap(picture);
		ibDialog::SetIcon(dlg_icon);
	}
	ibDialog::SetFocus();
}
