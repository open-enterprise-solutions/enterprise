#include "connectionDB.h"

#include <wx/xml/xml.h>

CDialogConnection::CDialogConnection(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) :
	wxDialog(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);
	this->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INACTIVECAPTION));

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
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

	m_textCtrlPassword = new wxTextCtrl(this, wxID_ANY, _("wetyu"), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
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
	m_buttonTestConnection->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CDialogConnection::TestConnectionOnButtonClick), NULL, this);
	m_buttonSaveConnection->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CDialogConnection::SaveConnectionOnButtonClick), NULL, this);
}

#include "backend/appData.h"

void CDialogConnection::LoadConnectionData()
{
	//const wxString& strFileName = appData->GetApplicationPath() + wxT("\\") + wxT("connection.dat");
	//wxXmlDocument document;

	//if (!document.Load(strFileName)) {
	//	m_textCtrlServer->SetValue(wxT("localhost"));
	//	m_textCtrlPort->SetValue(wxT("5432"));
	//	m_textCtrlUser->SetValue(wxT("postgres"));
	//	m_textCtrlPassword->SetValue(wxEmptyString);
	//	m_textCtrlDataBase->SetValue(wxEmptyString);
	//	return;
	//}

	//wxXmlNode* root = document.GetRoot();

	//wxXmlNode* node = root->GetChildren();
	//while (node != nullptr) {
	//	if (node->GetName() == "server" && node->GetChildren()) {
	//		m_textCtrlServer->SetValue(node->GetChildren()->GetContent());
	//	}
	//	else if (node->GetName() == "port" && node->GetChildren()) {
	//		m_textCtrlPort->SetValue(node->GetChildren()->GetContent());
	//	}
	//	else if (node->GetName() == "database" && node->GetChildren()) {
	//		m_textCtrlDataBase->SetValue(node->GetChildren()->GetContent());
	//	}
	//	else if (node->GetName() == "user" && node->GetChildren()) {
	//		m_textCtrlUser->SetValue(node->GetChildren()->GetContent());
	//	}
	//	else if (node->GetName() == "password" && node->GetChildren()) {
	//		m_textCtrlPassword->SetValue(node->GetChildren()->GetContent());
	//	}
	//	node = node->GetNext();
	//}
}

void CDialogConnection::SaveConnectionData()
{
	//wxXmlDocument document;
	//wxXmlNode* root = new wxXmlNode(wxXML_ELEMENT_NODE, "connection");
	//document.SetRoot(root);

	//wxXmlNode* xmlNode_server = new wxXmlNode(wxXML_ELEMENT_NODE, "server");
	//xmlNode_server->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, m_textCtrlServer->GetValue()));
	//root->AddChild(xmlNode_server);
	//wxXmlNode* xmlNode_port = new wxXmlNode(wxXML_ELEMENT_NODE, "port");
	//xmlNode_port->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, m_textCtrlPort->GetValue()));
	//root->AddChild(xmlNode_port);
	//wxXmlNode* xmlNode_db = new wxXmlNode(wxXML_ELEMENT_NODE, "database");
	//xmlNode_db->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, m_textCtrlDataBase->GetValue()));
	//root->AddChild(xmlNode_db);
	//wxXmlNode* xmlNode_user = new wxXmlNode(wxXML_ELEMENT_NODE, "user");
	//xmlNode_user->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, m_textCtrlUser->GetValue()));
	//root->AddChild(xmlNode_user);
	//wxXmlNode* xmlNode_password = new wxXmlNode(wxXML_ELEMENT_NODE, "password");
	//xmlNode_password->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, m_textCtrlPassword->GetValue()));
	//root->AddChild(xmlNode_password);

	//const wxString& strFileName = appData->GetApplicationPath() + wxT("\\") + wxT("connection.dat");
	//document.Save(strFileName);
}

#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"

void CDialogConnection::TestConnectionOnButtonClick(wxCommandEvent& event)
{
	CPostgresDatabaseLayer* postgresDatabaseLayer = new CPostgresDatabaseLayer;
	bool sucess = postgresDatabaseLayer->Open(
		m_textCtrlServer->GetValue(),
		m_textCtrlPort->GetValue(),
		wxT(""),
		m_textCtrlUser->GetValue(),
		m_textCtrlPassword->GetValue()
	);

	postgresDatabaseLayer->Close();
	event.Skip();
}

void CDialogConnection::SaveConnectionOnButtonClick(wxCommandEvent& event)
{
	SaveConnectionData();
	event.Skip();
}
