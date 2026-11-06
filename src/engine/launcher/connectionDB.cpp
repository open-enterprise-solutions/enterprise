#include "connectionDB.h"

#include <wx/dirdlg.h>   // wxDirSelector — the base's folder, chosen in the system's dialog
#include <wx/xml/xml.h>

#include "core/net/ports.h"                 // ibPort::Clients — the port a cluster's address leaves out
#include "protocol/connectionServer.h"      // the socket a client opens on a cluster's base or a publication

void ibDialogConnection::InitConnection(const wxString& strName, const ibListInfo& info)
{
	m_textCtrlName->SetValue(strName);

	switch (info.m_connection) {
	case ibLauncherConnection::Server:
		m_radioServer->SetValue(true);
		break;
	case ibLauncherConnection::Web:
		m_radioWeb->SetValue(true);
		break;
	default:
		m_radioFile->SetValue(true);
		break;
	}
	m_textCtrlFilePath->SetValue(info.m_strFilePath);
	m_textCtrlServer->SetValue(info.m_strServer);
	m_textCtrlDataBase->SetValue(info.m_strDatabase);
	m_textCtrlAddress->SetValue(info.m_strAddress);

	UpdateModeVisibility();
}

ibListInfo ibDialogConnection::GetListInfo() const
{
	ibListInfo info;
	info.m_connection = m_radioServer->GetValue() ? ibLauncherConnection::Server
		: m_radioWeb->GetValue() ? ibLauncherConnection::Web : ibLauncherConnection::File;
	info.m_strFilePath = m_textCtrlFilePath->GetValue();
	// A link copied from the browser's address bar comes with what stood around it.
	info.m_strServer = m_textCtrlServer->GetValue().Strip(wxString::both);
	info.m_strDatabase = m_textCtrlDataBase->GetValue().Strip(wxString::both);
	info.m_strAddress = m_textCtrlAddress->GetValue().Strip(wxString::both);
	return info;
}

void ibDialogConnection::UpdateModeVisibility()
{
	const bool bFile = m_radioFile->GetValue();

	m_sizerFileMode->Show(bFile);
	m_sizerServerMode->Show(m_radioServer->GetValue());
	m_sizerWebMode->Show(m_radioWeb->GetValue());

	m_buttonTestConnection->Show(!bFile);

	Layout();
	Fit();
}

ibDialogConnection::ibDialogConnection(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) :
	wxDialog(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);
	// Interior palette — powder-blue dialog frame.
	this->SetBackgroundColour(wxColour(0xB8, 0xC9, 0xD4));  // #B8C9D4 powder blue

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// Name row
	wxBoxSizer* sizerName = new wxBoxSizer(wxHORIZONTAL);
	m_staticTextName = new ibStaticText(this, wxID_ANY, _("Name IB:"), wxDefaultPosition, wxDefaultSize, 0);
	sizerName->Add(m_staticTextName, 1, wxALIGN_CENTER_VERTICAL, 0);
	m_textCtrlName = new ibTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sizerName->Add(m_textCtrlName, 3, wxALL | wxEXPAND, FromDIP(5));
	mainSizer->Add(sizerName, 0, wxEXPAND, FromDIP(5));

	// Mode radio buttons
	wxBoxSizer* sizerMode = new wxBoxSizer(wxHORIZONTAL);
	m_radioFile = new ibRadioButton(this, wxID_ANY, _("File (local database)"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	m_radioServer = new ibRadioButton(this, wxID_ANY, _("Server cluster"), wxDefaultPosition, wxDefaultSize, 0);
	m_radioWeb = new ibRadioButton(this, wxID_ANY, _("Web server"), wxDefaultPosition, wxDefaultSize, 0);
	m_radioFile->SetValue(true);
	sizerMode->Add(m_radioFile, 1, wxALL, FromDIP(5));
	sizerMode->Add(m_radioServer, 1, wxALL, FromDIP(5));
	sizerMode->Add(m_radioWeb, 1, wxALL, FromDIP(5));
	mainSizer->Add(sizerMode, 0, wxEXPAND, FromDIP(5));

	m_radioFile->Bind(wxEVT_RADIOBUTTON, &ibDialogConnection::OnModeChanged, this);
	m_radioServer->Bind(wxEVT_RADIOBUTTON, &ibDialogConnection::OnModeChanged, this);
	m_radioWeb->Bind(wxEVT_RADIOBUTTON, &ibDialogConnection::OnModeChanged, this);

	// File mode panel
	m_sizerFileMode = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* sizerFilePath = new wxBoxSizer(wxHORIZONTAL);
	m_staticTextFilePath = new ibStaticText(this, wxID_ANY, _("Database folder:"), wxDefaultPosition, wxDefaultSize, 0);
	sizerFilePath->Add(m_staticTextFilePath, 1, wxALIGN_CENTER_VERTICAL, 0);
	m_textCtrlFilePath = new ibTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sizerFilePath->Add(m_textCtrlFilePath, 3, wxALL | wxEXPAND, FromDIP(5));
	m_buttonFilePath = new ibButton(this, wxID_ANY, wxT("..."), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	sizerFilePath->Add(m_buttonFilePath, 0, wxTOP | wxBOTTOM | wxRIGHT | wxEXPAND, FromDIP(5));
	m_sizerFileMode->Add(sizerFilePath, 0, wxEXPAND, FromDIP(5));
	mainSizer->Add(m_sizerFileMode, 0, wxEXPAND, FromDIP(5));

	// Server mode panel
	m_sizerServerMode = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* sizerServer = new wxBoxSizer(wxHORIZONTAL);
	m_staticTextServer = new ibStaticText(this, wxID_ANY, _("Server:"), wxDefaultPosition, wxDefaultSize, 0);
	sizerServer->Add(m_staticTextServer, 1, wxALIGN_CENTER_VERTICAL, 0);
	m_textCtrlServer = new ibTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	// The port after a colon, left out — the cluster's own.
	m_textCtrlServer->SetHint(wxString::Format(wxT("localhost[:%u]"), static_cast<unsigned>(ibPort::Clients.m_first)));
	sizerServer->Add(m_textCtrlServer, 3, wxALL | wxEXPAND, FromDIP(5));
	m_sizerServerMode->Add(sizerServer, 0, wxEXPAND, FromDIP(5));

	wxBoxSizer* sizerDataBase = new wxBoxSizer(wxHORIZONTAL);
	m_staticTextDataBase = new ibStaticText(this, wxID_ANY, _("Base:"), wxDefaultPosition, wxDefaultSize, 0);
	sizerDataBase->Add(m_staticTextDataBase, 1, wxALIGN_CENTER_VERTICAL, 0);
	m_textCtrlDataBase = new ibTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sizerDataBase->Add(m_textCtrlDataBase, 3, wxALL | wxEXPAND, FromDIP(5));
	m_sizerServerMode->Add(sizerDataBase, 0, wxEXPAND, FromDIP(5));

	mainSizer->Add(m_sizerServerMode, 0, wxEXPAND, FromDIP(5));

	// Web mode panel
	m_sizerWebMode = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* sizerAddress = new wxBoxSizer(wxHORIZONTAL);
	m_staticTextAddress = new ibStaticText(this, wxID_ANY, _("Address:"), wxDefaultPosition, wxDefaultSize, 0);
	sizerAddress->Add(m_staticTextAddress, 1, wxALIGN_CENTER_VERTICAL, 0);
	m_textCtrlAddress = new ibTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	m_textCtrlAddress->SetHint(wxT("https://server/<base>/"));
	sizerAddress->Add(m_textCtrlAddress, 3, wxALL | wxEXPAND, FromDIP(5));
	m_sizerWebMode->Add(sizerAddress, 0, wxEXPAND, FromDIP(5));

	mainSizer->Add(m_sizerWebMode, 0, wxEXPAND, FromDIP(5));

	// Buttons
	wxBoxSizer* bSizerButton = new wxBoxSizer(wxHORIZONTAL);

	m_buttonTestConnection = new ibButton(this, wxID_ANY, _("Test connection"), wxDefaultPosition, wxDefaultSize, 0);
	bSizerButton->Add(m_buttonTestConnection, 0, wxALL, FromDIP(5));

	// The primary action — the theme's accent (the kit's ibButton::SetPrimary), not a colour of its own.
	m_buttonSaveConnection = new ibButton(this, wxID_ANY, _("Save connection"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonSaveConnection->SetPrimary();
	bSizerButton->Add(m_buttonSaveConnection, 1, wxALL, FromDIP(5));

	mainSizer->Add(bSizerButton, 1, wxEXPAND, FromDIP(5));

	this->SetSizer(mainSizer);

	UpdateModeVisibility();

	this->Centre(wxBOTH);

	// Connect Events
	m_buttonFilePath->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ibDialogConnection::FilePathOnButtonClick), NULL, this);
	m_buttonTestConnection->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ibDialogConnection::TestConnectionOnButtonClick), NULL, this);
	m_buttonSaveConnection->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ibDialogConnection::SaveConnectionOnButtonClick), NULL, this);
}

void ibDialogConnection::OnModeChanged(wxCommandEvent& event)
{
	UpdateModeVisibility();
	event.Skip();
}

void ibDialogConnection::FilePathOnButtonClick(wxCommandEvent& event)
{
	// The kit has no picker: the field and its button, and the system's folder dialog.
	const wxString path = wxDirSelector(_("Select database folder"), m_textCtrlFilePath->GetValue(),
		wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST, wxDefaultPosition, this);
	if (!path.IsEmpty())
		m_textCtrlFilePath->SetValue(path);
	event.Skip();
}

void ibDialogConnection::TestConnectionOnButtonClick(wxCommandEvent& event)
{
	// THE CLIENT'S OWN ROAD — the socket it opens on the address (--address), opened and closed again.
	ibProtocolConnectionServer server;
	ibString error;
	if (server.Open(GetListInfo().GetAddress(), ibEmptyString, error))
		wxMessageBox(_("Connected to DB!"), _("Configure connect"), wxOK | wxICON_INFORMATION, this);
	else
		wxMessageBox(error, _("Failed to connect to DB!"), wxOK | wxICON_ERROR, this);

	event.Skip();
}

void ibDialogConnection::SaveConnectionOnButtonClick(wxCommandEvent& event)
{
	if (!m_textCtrlName->IsEmpty()) { EndModal(wxID_OK); }
	event.Skip();
}
