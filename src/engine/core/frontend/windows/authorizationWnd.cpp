#include "authorizationWnd.h"
#include "databaseLayer/databaseLayer.h"
#include "appData.h"

///////////////////////////////////////////////////////////////////////////

CAuthorizationWnd::CAuthorizationWnd(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : wxDialog(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* bSizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* bSizerCtrl = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* bSizerHeader = new wxBoxSizer(wxVERTICAL);

	m_staticTextLogin = new wxStaticText(this, wxID_ANY, _("Login:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextLogin->Wrap(-1);
	bSizerHeader->Add(m_staticTextLogin, 0, wxALL, 10);

	m_staticTextPassword = new wxStaticText(this, wxID_ANY, _("Password:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextPassword->Wrap(-1);
	bSizerHeader->Add(m_staticTextPassword, 0, wxALL, 9);

	bSizerCtrl->Add(bSizerHeader, 0, 0, 5);

	wxBoxSizer* bSizerBottom = new wxBoxSizer(wxVERTICAL);

	m_comboBoxLogin = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, appData->GetAllowedUsers());
	bSizerBottom->Add(m_comboBoxLogin, 0, wxALL | wxEXPAND, 5);
	m_textCtrlPassword = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	bSizerBottom->Add(m_textCtrlPassword, 0, wxALL | wxEXPAND, 5);
	bSizerCtrl->Add(bSizerBottom, 1, wxEXPAND, 5);
	bSizer->Add(bSizerCtrl, 0, wxEXPAND, 5);

	wxStdDialogButtonSizer* bSizerButtons = new wxStdDialogButtonSizer();;
	m_buttonOK = new wxButton(this, wxID_OK);
	bSizerButtons->AddButton(m_buttonOK);
	m_buttonCancel = new wxButton(this, wxID_CANCEL);
	bSizerButtons->AddButton(m_buttonCancel);
	bSizerButtons->Realize();

	bSizer->Add(bSizerButtons, 1, wxALIGN_RIGHT, 5);

	this->SetSizer(bSizer);
	this->Layout();

	this->Centre(wxBOTH);

	// Connect Events
	m_buttonOK->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CAuthorizationWnd::OnOKButtonClick), NULL, this);
	m_buttonCancel->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CAuthorizationWnd::OnCancelButtonClick), NULL, this);
}

CAuthorizationWnd::~CAuthorizationWnd()
{
	m_buttonOK->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CAuthorizationWnd::OnOKButtonClick), NULL, this);
	m_buttonCancel->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CAuthorizationWnd::OnCancelButtonClick), NULL, this);
}

void CAuthorizationWnd::OnOKButtonClick(wxCommandEvent &event)
{
	if (!appData->CheckLoginAndPassword(m_comboBoxLogin->GetValue(), m_textCtrlPassword->GetValue())) {
		wxMessageBox(_("Wrong user or password entered!"));
	}
	else {
		event.Skip();
	}
}

void CAuthorizationWnd::OnCancelButtonClick(wxCommandEvent &event)
{
	event.Skip();
}
