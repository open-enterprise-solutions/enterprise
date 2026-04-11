#include "connectionDB.h"

#include <wx/xml/xml.h>

void ibDialogConnection::InitConnection(const wxString& strName, const wxString& strServer, const wxString& strPort, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	m_textCtrlName->SetValue(strName);
	m_textCtrlServer->SetValue(strServer);
	m_textCtrlPort->SetValue(strPort);
	m_textCtrlDataBase->SetValue(strDatabase);
	m_textCtrlUser->SetValue(strUser);
	m_textCtrlPassword->SetValue(strPassword);
}

ibDialogConnection::ibDialogConnection(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) :
	wxDialog(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);
	this->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INACTIVECAPTION));

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* sizerName = new wxBoxSizer(wxHORIZONTAL);

	m_staticTextName = new wxStaticText(this, wxID_ANY, _("Name IB:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextName->Wrap(-1);
	sizerName->Add(m_staticTextName, 1, wxALIGN_CENTER_VERTICAL, 0);

	m_textCtrlName = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sizerName->Add(m_textCtrlName, 3, wxALL | wxEXPAND, 5);
	mainSizer->Add(sizerName, 0, wxEXPAND, 5);

	wxBoxSizer* sizerServer = new wxBoxSizer(wxHORIZONTAL);

	m_staticTextServer = new wxStaticText(this, wxID_ANY, _("Server:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextServer->Wrap(-1);
	sizerServer->Add(m_staticTextServer, 1, wxALIGN_CENTER_VERTICAL, 0);

	m_textCtrlServer = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sizerServer->Add(m_textCtrlServer, 3, wxALL | wxEXPAND, 5);
	mainSizer->Add(sizerServer, 0, wxEXPAND, 5);

	wxBoxSizer* sizerPort = new wxBoxSizer(wxHORIZONTAL);

	m_staticTextPort = new wxStaticText(this, wxID_ANY, _("Port:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextPort->Wrap(-1);
	sizerPort->Add(m_staticTextPort, 1, wxALIGN_CENTER_VERTICAL, 0);

	m_textCtrlPort = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sizerPort->Add(m_textCtrlPort, 3, wxALL | wxEXPAND, 5);
	mainSizer->Add(sizerPort, 0, wxEXPAND, 5);

	wxBoxSizer* sizerUser = new wxBoxSizer(wxHORIZONTAL);

	m_staticTextUser = new wxStaticText(this, wxID_ANY, _("User:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextUser->Wrap(-1);
	sizerUser->Add(m_staticTextUser, 1, wxALIGN_CENTER_VERTICAL, 0);

	m_textCtrlUser = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sizerUser->Add(m_textCtrlUser, 3, wxALL | wxEXPAND, 5);
	mainSizer->Add(sizerUser, 0, wxEXPAND, 5);

	wxBoxSizer* sizerPassword = new wxBoxSizer(wxHORIZONTAL);

	m_staticTextPassword = new wxStaticText(this, wxID_ANY, _("Password:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextPassword->Wrap(-1);
	sizerPassword->Add(m_staticTextPassword, 1, wxALIGN_CENTER_VERTICAL, 0);

	m_textCtrlPassword = new wxTextCtrl(this, wxID_ANY, wxT("_________________"), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	sizerPassword->Add(m_textCtrlPassword, 3, wxALL | wxEXPAND, 5);
	mainSizer->Add(sizerPassword, 0, wxEXPAND, 5);

	wxBoxSizer* sizerDataBase = new wxBoxSizer(wxHORIZONTAL);

	m_staticTextDataBase = new wxStaticText(this, wxID_ANY, _("Database:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextDataBase->Wrap(-1);
	sizerDataBase->Add(m_staticTextDataBase, 1, wxALIGN_CENTER_VERTICAL, 0);

	m_textCtrlDataBase = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sizerDataBase->Add(m_textCtrlDataBase, 3, wxALL | wxEXPAND, 5);
	mainSizer->Add(sizerDataBase, 0, wxEXPAND, 5);

	wxBoxSizer* bSizerButton = new wxBoxSizer(wxHORIZONTAL);

	m_buttonTestConnection = new wxButton(this, wxID_ANY, _("Test connection"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonTestConnection->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOBK));

	bSizerButton->Add(m_buttonTestConnection, 0, wxALL, 5);

	m_buttonSaveConnection = new wxButton(this, wxID_ANY, _("Save connection"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonSaveConnection->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_ACTIVECAPTION));

	bSizerButton->Add(m_buttonSaveConnection, 1, wxALL, 5);
	mainSizer->Add(bSizerButton, 1, wxEXPAND, 5);

	this->SetSizer(mainSizer);
	this->Layout();
	this->Centre(wxBOTH);

	// Connect Events
	m_buttonTestConnection->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ibDialogConnection::TestConnectionOnButtonClick), NULL, this);
	m_buttonSaveConnection->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ibDialogConnection::SaveConnectionOnButtonClick), NULL, this);
}

#ifdef OES_USE_POSTGRESQL
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#endif

void ibDialogConnection::TestConnectionOnButtonClick(wxCommandEvent& event)
{
#ifdef OES_USE_POSTGRESQL
	std::shared_ptr<ibDatabaseLayerPostgres>postgresDatabaseLayer(new ibDatabaseLayerPostgres);
	bool sucess = postgresDatabaseLayer->Open(
		m_textCtrlServer->GetValue(),
		m_textCtrlPort->GetValue(),
		wxT(""),
		m_textCtrlUser->GetValue(),
		m_textCtrlPassword->GetValue()
	);

	postgresDatabaseLayer->Close();

	if (sucess) wxMessageBox("Connected to DB!");
	else wxMessageBox("Failed connect to DB!");
#else
	wxMessageBox("PostgreSQL driver not available");
#endif
	event.Skip();
}

void ibDialogConnection::SaveConnectionOnButtonClick(wxCommandEvent& event)
{
	if (!m_textCtrlName->IsEmpty()) { EndModal(wxID_OK); }
	event.Skip();
}
