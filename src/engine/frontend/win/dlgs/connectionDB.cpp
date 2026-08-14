#include "connectionDB.h"

#include <wx/xml/xml.h>

ibDialogConnection::ibDialogConnection(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) :
	wxDialog(parent, id, title, pos, size, style)
{
	wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);
	// Drop the INACTIVECAPTION yellow tint — native window background reads
	// correctly against every OS theme.

	const int kPad = FromDIP(6);

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	wxFlexGridSizer* grid = new wxFlexGridSizer(/*rows*/ 0, /*cols*/ 2, kPad, kPad);
	grid->AddGrowableCol(1, 1);

	auto addRow = [&](wxStaticText*& label, const wxString& text,
		wxTextCtrl*& ctrl, long ctrlStyle) {
		label = new wxStaticText(this, wxID_ANY, text);
		ctrl  = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, ctrlStyle);
		grid->Add(label, 0, wxALIGN_CENTER_VERTICAL);
		grid->Add(ctrl,  1, wxEXPAND);
	};

	addRow(m_staticTextServer,   _("Server:"),   m_textCtrlServer,   0);
	addRow(m_staticTextPort,     _("Port:"),     m_textCtrlPort,     0);
	addRow(m_staticTextUser,     _("User:"),     m_textCtrlUser,     0);
	addRow(m_staticTextPassword, _("Password:"), m_textCtrlPassword, wxTE_PASSWORD);
	addRow(m_staticTextDataBase, _("Database:"), m_textCtrlDataBase, 0);

	mainSizer->Add(grid, 0, wxALL | wxEXPAND, kPad * 2);

	wxBoxSizer* bSizerButton = new wxBoxSizer(wxHORIZONTAL);
	m_buttonTestConnection = new wxButton(this, wxID_ANY, _("Test connection"));
	m_buttonSaveConnection = new wxButton(this, wxID_ANY, _("Save connection"));
	// Right-aligned button bar, default theme colouring.
	bSizerButton->AddStretchSpacer(1);
	bSizerButton->Add(m_buttonTestConnection, 0, wxRIGHT, kPad);
	bSizerButton->Add(m_buttonSaveConnection, 0);

	mainSizer->Add(bSizerButton, 0, wxALL | wxEXPAND, kPad * 2);

	wxDialog::SetSizer(mainSizer);
	mainSizer->SetSizeHints(this);
	wxDialog::Layout();
	wxDialog::Centre(wxBOTH);

	// Connect Events
	m_buttonTestConnection->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ibDialogConnection::TestConnectionOnButtonClick), NULL, this);
	m_buttonSaveConnection->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ibDialogConnection::SaveConnectionOnButtonClick), NULL, this);
}

#include "backend/appData.h"

void ibDialogConnection::LoadConnectionData()
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

void ibDialogConnection::SaveConnectionData()
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

#ifdef OES_USE_POSTGRESQL
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#include <memory>   // the probe OWNS its driver — see TestConnectionOnButtonClick
#endif

void ibDialogConnection::TestConnectionOnButtonClick(wxCommandEvent& event)
{
#ifdef OES_USE_POSTGRESQL
	// ⚠ OWNED. This was a bare `new` that was Opened, Closed and never deleted — a driver object and
	// its connection state leaked on every click of "test". Closing is not owning: the object outlives
	// the connection it closed. The launcher's twin already holds its probe in a shared_ptr.
	const std::unique_ptr<ibDatabaseLayerPostgres> postgresDatabaseLayer(new ibDatabaseLayerPostgres);
	const bool success = postgresDatabaseLayer->Open(
		m_textCtrlServer->GetValue(),
		m_textCtrlPort->GetValue(),
		wxT(""),
		m_textCtrlUser->GetValue(),
		m_textCtrlPassword->GetValue()
	);
	const wxString failure = success ? wxString() : postgresDatabaseLayer->GetErrorMessage();

	postgresDatabaseLayer->Close();

	// ⚠ AND IT SAYS WHAT HAPPENED. The result was computed into a local and dropped: a button labelled
	// "test the connection" that answers nothing at all, whichever way the test went. The driver's own
	// message is read BEFORE Close, because closing clears the error state it would have reported.
	if (success)
		wxMessageBox(_("Connection established"), _("Test connection"), wxOK | wxICON_INFORMATION, this);
	else
		wxMessageBox(failure.IsEmpty() ? _("Could not connect with these settings") : failure,
			_("Test connection"), wxOK | wxICON_ERROR, this);
#else
	wxMessageBox(_("PostgreSQL driver not available"));
#endif
	event.Skip();
}

void ibDialogConnection::SaveConnectionOnButtonClick(wxCommandEvent& event)
{
	SaveConnectionData();
	event.Skip();
}
