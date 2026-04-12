#include "connectionDB.h"

#include <wx/xml/xml.h>

void ibDialogConnection::InitConnection(
	const wxString& strName,
	bool bFileMode,
	const wxString& strFilePath,
	const wxString& strServer, const wxString& strPort,
	const wxString& strDatabase,
	const wxString& strUser, const wxString& strPassword)
{
	m_textCtrlName->SetValue(strName);

	if (bFileMode) {
		m_radioFile->SetValue(true);
		m_radioServer->SetValue(false);
		m_dirPickerFile->SetPath(strFilePath);
	}
	else {
		m_radioFile->SetValue(false);
		m_radioServer->SetValue(true);
		m_textCtrlServer->SetValue(strServer);
		m_textCtrlPort->SetValue(strPort);
		m_textCtrlDataBase->SetValue(strDatabase);
		m_textCtrlUser->SetValue(strUser);
		m_textCtrlPassword->SetValue(strPassword);
	}

	UpdateModeVisibility();
}

void ibDialogConnection::UpdateModeVisibility()
{
	const bool bFile = m_radioFile->GetValue();

	m_sizerFileMode->Show(bFile);
	m_sizerServerMode->Show(!bFile);

	m_buttonTestConnection->Show(!bFile);

	Layout();
	Fit();
}

ibDialogConnection::ibDialogConnection(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) :
	wxDialog(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);
	this->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INACTIVECAPTION));

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// Name row
	wxBoxSizer* sizerName = new wxBoxSizer(wxHORIZONTAL);
	m_staticTextName = new wxStaticText(this, wxID_ANY, _("Name IB:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextName->Wrap(-1);
	sizerName->Add(m_staticTextName, 1, wxALIGN_CENTER_VERTICAL, 0);
	m_textCtrlName = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sizerName->Add(m_textCtrlName, 3, wxALL | wxEXPAND, 5);
	mainSizer->Add(sizerName, 0, wxEXPAND, 5);

	// Mode radio buttons
	wxBoxSizer* sizerMode = new wxBoxSizer(wxHORIZONTAL);
	m_radioFile = new wxRadioButton(this, wxID_ANY, _("File (local database)"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	m_radioServer = new wxRadioButton(this, wxID_ANY, _("Server"), wxDefaultPosition, wxDefaultSize, 0);
	m_radioFile->SetValue(true);
	sizerMode->Add(m_radioFile, 1, wxALL, 5);
	sizerMode->Add(m_radioServer, 1, wxALL, 5);
	mainSizer->Add(sizerMode, 0, wxEXPAND, 5);

	m_radioFile->Bind(wxEVT_RADIOBUTTON, &ibDialogConnection::OnModeChanged, this);
	m_radioServer->Bind(wxEVT_RADIOBUTTON, &ibDialogConnection::OnModeChanged, this);

	// File mode panel
	m_sizerFileMode = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* sizerFilePath = new wxBoxSizer(wxHORIZONTAL);
	m_staticTextFilePath = new wxStaticText(this, wxID_ANY, _("Database folder:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextFilePath->Wrap(-1);
	sizerFilePath->Add(m_staticTextFilePath, 1, wxALIGN_CENTER_VERTICAL, 0);
	m_dirPickerFile = new wxDirPickerCtrl(this, wxID_ANY, wxEmptyString, _("Select database folder"), wxDefaultPosition, wxDefaultSize, wxDIRP_USE_TEXTCTRL | wxDIRP_DIR_MUST_EXIST);
	sizerFilePath->Add(m_dirPickerFile, 3, wxALL | wxEXPAND, 5);
	m_sizerFileMode->Add(sizerFilePath, 0, wxEXPAND, 5);
	mainSizer->Add(m_sizerFileMode, 0, wxEXPAND, 5);

	// Server mode panel
	m_sizerServerMode = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* sizerServer = new wxBoxSizer(wxHORIZONTAL);
	m_staticTextServer = new wxStaticText(this, wxID_ANY, _("Server:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextServer->Wrap(-1);
	sizerServer->Add(m_staticTextServer, 1, wxALIGN_CENTER_VERTICAL, 0);
	m_textCtrlServer = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sizerServer->Add(m_textCtrlServer, 3, wxALL | wxEXPAND, 5);
	m_sizerServerMode->Add(sizerServer, 0, wxEXPAND, 5);

	wxBoxSizer* sizerPort = new wxBoxSizer(wxHORIZONTAL);
	m_staticTextPort = new wxStaticText(this, wxID_ANY, _("Port:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextPort->Wrap(-1);
	sizerPort->Add(m_staticTextPort, 1, wxALIGN_CENTER_VERTICAL, 0);
	m_textCtrlPort = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sizerPort->Add(m_textCtrlPort, 3, wxALL | wxEXPAND, 5);
	m_sizerServerMode->Add(sizerPort, 0, wxEXPAND, 5);

	wxBoxSizer* sizerUser = new wxBoxSizer(wxHORIZONTAL);
	m_staticTextUser = new wxStaticText(this, wxID_ANY, _("User:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextUser->Wrap(-1);
	sizerUser->Add(m_staticTextUser, 1, wxALIGN_CENTER_VERTICAL, 0);
	m_textCtrlUser = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sizerUser->Add(m_textCtrlUser, 3, wxALL | wxEXPAND, 5);
	m_sizerServerMode->Add(sizerUser, 0, wxEXPAND, 5);

	wxBoxSizer* sizerPassword = new wxBoxSizer(wxHORIZONTAL);
	m_staticTextPassword = new wxStaticText(this, wxID_ANY, _("Password:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextPassword->Wrap(-1);
	sizerPassword->Add(m_staticTextPassword, 1, wxALIGN_CENTER_VERTICAL, 0);
	m_textCtrlPassword = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	sizerPassword->Add(m_textCtrlPassword, 3, wxALL | wxEXPAND, 5);
	m_sizerServerMode->Add(sizerPassword, 0, wxEXPAND, 5);

	wxBoxSizer* sizerDataBase = new wxBoxSizer(wxHORIZONTAL);
	m_staticTextDataBase = new wxStaticText(this, wxID_ANY, _("Database:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextDataBase->Wrap(-1);
	sizerDataBase->Add(m_staticTextDataBase, 1, wxALIGN_CENTER_VERTICAL, 0);
	m_textCtrlDataBase = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sizerDataBase->Add(m_textCtrlDataBase, 3, wxALL | wxEXPAND, 5);
	m_sizerServerMode->Add(sizerDataBase, 0, wxEXPAND, 5);

	mainSizer->Add(m_sizerServerMode, 0, wxEXPAND, 5);

	// Buttons
	wxBoxSizer* bSizerButton = new wxBoxSizer(wxHORIZONTAL);

	m_buttonTestConnection = new wxButton(this, wxID_ANY, _("Test connection"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonTestConnection->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOBK));
	bSizerButton->Add(m_buttonTestConnection, 0, wxALL, 5);

	m_buttonSaveConnection = new wxButton(this, wxID_ANY, _("Save connection"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonSaveConnection->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_ACTIVECAPTION));
	bSizerButton->Add(m_buttonSaveConnection, 1, wxALL, 5);

	mainSizer->Add(bSizerButton, 1, wxEXPAND, 5);

	this->SetSizer(mainSizer);

	UpdateModeVisibility();

	this->Centre(wxBOTH);

	// Connect Events
	m_buttonTestConnection->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ibDialogConnection::TestConnectionOnButtonClick), NULL, this);
	m_buttonSaveConnection->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ibDialogConnection::SaveConnectionOnButtonClick), NULL, this);
}

void ibDialogConnection::OnModeChanged(wxCommandEvent& event)
{
	UpdateModeVisibility();
	event.Skip();
}

#ifdef OES_USE_POSTGRESQL
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#endif
#ifdef OES_USE_FIREBIRD
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#endif

void ibDialogConnection::TestConnectionOnButtonClick(wxCommandEvent& event)
{
	bool success = false;

#ifdef OES_USE_FIREBIRD
	{
		std::shared_ptr<ibDatabaseLayerFirebird> db(new ibDatabaseLayerFirebird(
			m_textCtrlServer->GetValue(),
			m_textCtrlDataBase->GetValue(),
			m_textCtrlUser->GetValue(),
			m_textCtrlPassword->GetValue()
		));
		success = db->Open();
		db->Close();
	}
#elif defined(OES_USE_POSTGRESQL)
	{
		std::shared_ptr<ibDatabaseLayerPostgres> db(new ibDatabaseLayerPostgres);
		success = db->Open(
			m_textCtrlServer->GetValue(),
			m_textCtrlPort->GetValue(),
			m_textCtrlDataBase->GetValue(),
			m_textCtrlUser->GetValue(),
			m_textCtrlPassword->GetValue()
		);
		db->Close();
	}
#else
	wxMessageBox(_("No database driver available"));
	event.Skip();
	return;
#endif

	if (success)
		wxMessageBox(_("Connected to DB!"));
	else
		wxMessageBox(_("Failed to connect to DB!"));

	event.Skip();
}

void ibDialogConnection::SaveConnectionOnButtonClick(wxCommandEvent& event)
{
	if (!m_textCtrlName->IsEmpty()) { EndModal(wxID_OK); }
	event.Skip();
}
