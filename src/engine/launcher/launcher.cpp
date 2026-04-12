#include "launcher.h"

#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/xml/xml.h>

void ibFrameLauncher::LoadListIB() {

	wxLogNull logNo;

	wxString directory =
		wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir::Dir_Cache) + wxFileName::GetPathSeparator() + wxT("OES");

	// Make sure the directory exists.
	wxFileName strFileName(directory, "ib_list.xml");

	wxXmlDocument document;
	if (document.Load(strFileName.GetFullPath())) {
		wxXmlNode* root = document.GetRoot();
		wxXmlNode* node = root->GetChildren();
		while (node != nullptr) {
			if (node->GetName() == "item") {
				CListInfo listInfo; wxString strNameIB;
				wxXmlNode* nodeItem = node->GetChildren();
				while (nodeItem != nullptr) {
					if (nodeItem->GetName() == "name" && nodeItem->GetChildren()) {
						strNameIB = nodeItem->GetChildren()->GetContent();
					}
					if (nodeItem->GetName() == "server" && nodeItem->GetChildren()) {
						listInfo.m_strServer = nodeItem->GetChildren()->GetContent();
					}
					else if (nodeItem->GetName() == "port" && node->GetChildren()) {
						listInfo.m_strPort = nodeItem->GetChildren()->GetContent();
					}
					else if (nodeItem->GetName() == "database" && node->GetChildren()) {
						listInfo.m_strDatabase = nodeItem->GetChildren()->GetContent();
					}
					else if (nodeItem->GetName() == "user" && node->GetChildren()) {
						listInfo.m_strUser = nodeItem->GetChildren()->GetContent();
					}
					else if (nodeItem->GetName() == "password" && node->GetChildren()) {
						listInfo.m_strPassword = nodeItem->GetChildren()->GetContent();
					}
					nodeItem = nodeItem->GetNext();
				}

				m_listInfoBase.emplace_back(strNameIB, listInfo);
				m_listIBwnd->AppendString(strNameIB);

				node = node->GetNext();
			}
		}
	}
}

void ibFrameLauncher::SaveListIB() {

	wxXmlDocument document;
	wxXmlNode* root = new wxXmlNode(wxXML_ELEMENT_NODE, "ib");
	document.SetRoot(root);
	
	for (auto& list : m_listInfoBase) {
		wxXmlNode* xmlItem = new wxXmlNode(wxXML_ELEMENT_NODE, "item");
		wxXmlNode* xmlNode_name = new wxXmlNode(wxXML_ELEMENT_NODE, "name");
		xmlNode_name->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, list.first));
		xmlItem->AddChild(xmlNode_name);
		wxXmlNode* xmlNode_server = new wxXmlNode(wxXML_ELEMENT_NODE, "server");
		xmlNode_server->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, list.second.m_strServer));
		xmlItem->AddChild(xmlNode_server);
		wxXmlNode* xmlNode_port = new wxXmlNode(wxXML_ELEMENT_NODE, "port");
		xmlNode_port->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, list.second.m_strPort));
		xmlItem->AddChild(xmlNode_port);
		wxXmlNode* xmlNode_db = new wxXmlNode(wxXML_ELEMENT_NODE, "database");
		xmlNode_db->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, list.second.m_strDatabase));
		xmlItem->AddChild(xmlNode_db);
		wxXmlNode* xmlNode_user = new wxXmlNode(wxXML_ELEMENT_NODE, "user");
		xmlNode_user->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, list.second.m_strUser));
		xmlItem->AddChild(xmlNode_user);
		wxXmlNode* xmlNode_password = new wxXmlNode(wxXML_ELEMENT_NODE, "password");
		xmlNode_password->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, list.second.m_strPassword));
		xmlItem->AddChild(xmlNode_password);
		root->AddChild(xmlItem);
	}

	wxString directory =
		wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir::Dir_Cache) + wxFileName::GetPathSeparator() + wxT("OES");

	// Make sure the directory exists.
	wxFileName strFileName(directory, "ib_list.xml");
	strFileName.Mkdir(0777, wxPATH_MKDIR_FULL);

	document.Save(strFileName.GetFullPath());
}

ibFrameLauncher::ibFrameLauncher(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : wxFrame(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);
	this->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));

	wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizerLeft = new wxBoxSizer(wxVERTICAL);

	m_listIBwnd = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE);
	m_listIBwnd->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("Arial")));

	m_listIBwnd->Bind(wxEVT_LISTBOX, &ibFrameLauncher::OnSelectedList, this);
	m_listIBwnd->Bind(wxEVT_LISTBOX_DCLICK, &ibFrameLauncher::OnSelectedDClickList, this);

	sizerLeft->Add(m_listIBwnd, 1, wxALL | wxEXPAND, 5);

	m_staticDBName = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	m_staticDBName->Wrap(-1);
	sizerLeft->Add(m_staticDBName, 0, wxALL | wxEXPAND, 5);

	mainSizer->Add(sizerLeft, 1, wxEXPAND, 5);

	wxBoxSizer* sizerRight = new wxBoxSizer(wxVERTICAL);

	m_buttonEnterprise = new wxButton(this, wxID_ANY, _("Enterprise"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonEnterprise->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_FORWARD, wxART_BUTTON));
	m_buttonEnterprise->Bind(wxEVT_BUTTON, &ibFrameLauncher::OnButtonEnterprise, this);

	sizerRight->Add(m_buttonEnterprise, 0, wxALL | wxEXPAND, 5);

	m_buttonDesigner = new wxButton(this, wxID_ANY, _("Designer"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonDesigner->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_FORWARD, wxART_BUTTON));
	m_buttonDesigner->Bind(wxEVT_BUTTON, &ibFrameLauncher::OnButtonDesigner, this);

	sizerRight->Add(m_buttonDesigner, 0, wxALL | wxEXPAND, 5);
	sizerRight->Add(0, 0, 1, wxEXPAND, 5);

	m_buttonAdd = new wxButton(this, wxID_ANY, _("Add"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonAdd->SetBitmap(wxArtProvider::GetBitmap(wxART_NEW, wxART_BUTTON));
	sizerRight->Add(m_buttonAdd, 0, wxALL | wxEXPAND, 5);

	m_buttonAdd->Bind(wxEVT_BUTTON, &ibFrameLauncher::OnButtonAdd, this);

	m_buttonEdit = new wxButton(this, wxID_ANY, _("Edit"), wxDefaultPosition, wxDefaultSize, 0);

	m_buttonEdit->SetBitmap(wxArtProvider::GetBitmap(wxART_PASTE, wxART_BUTTON));
	sizerRight->Add(m_buttonEdit, 0, wxALL | wxEXPAND, 5);

	m_buttonEdit->Bind(wxEVT_BUTTON, &ibFrameLauncher::OnButtonEdit, this);

	m_buttonDelete = new wxButton(this, wxID_ANY, _("Delete"), wxDefaultPosition, wxDefaultSize, 0);

	m_buttonDelete->SetBitmap(wxArtProvider::GetBitmap(wxART_DELETE, wxART_BUTTON));
	sizerRight->Add(m_buttonDelete, 0, wxALL | wxEXPAND, 5);

	m_buttonDelete->Bind(wxEVT_BUTTON, &ibFrameLauncher::OnButtonDelete, this);

	sizerRight->Add(0, 0, 1, wxEXPAND, 5);

	m_buttonExit = new wxButton(this, wxID_EXIT, _("Exit"), wxDefaultPosition, wxDefaultSize, 0);

	m_buttonExit->SetBitmap(wxArtProvider::GetBitmap(wxART_QUIT, wxART_BUTTON));
	sizerRight->Add(m_buttonExit, 0, wxALL | wxEXPAND, 5);

	m_buttonExit->Bind(wxEVT_BUTTON, &ibFrameLauncher::OnButtonClose, this);

	mainSizer->Add(sizerRight, 0, wxEXPAND, 5);

	this->SetSizer(mainSizer);
	this->Layout();

	this->Centre(wxBOTH);

	LoadListIB();
}

ibFrameLauncher::~ibFrameLauncher()
{
	SaveListIB();
}

void ibFrameLauncher::OnSelectedList(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	m_staticDBName->SetLabel("srv: " + itSelection->second.m_strServer + "; db: " + itSelection->second.m_strDatabase);
}

void ibFrameLauncher::OnSelectedDClickList(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	wxString executeCmd = "enterprise ";
	if (!itSelection->second.m_strServer.IsEmpty())
		executeCmd += " /srv " + itSelection->second.m_strServer;
	if (!itSelection->second.m_strPort.IsEmpty())
		executeCmd += " /p " + itSelection->second.m_strPort;
	if (!itSelection->second.m_strDatabase.IsEmpty())
		executeCmd += " /db " + itSelection->second.m_strDatabase;
	if (!itSelection->second.m_strUser.IsEmpty())
		executeCmd += " /usr " + itSelection->second.m_strUser;
	if (!itSelection->second.m_strPassword.IsEmpty())
		executeCmd += " /pwd " + itSelection->second.m_strPassword;
	wxExecute(executeCmd);
	Close(true);
}

void ibFrameLauncher::OnButtonEnterprise(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	wxString executeCmd = "enterprise ";
	if (!itSelection->second.m_strServer.IsEmpty())
		executeCmd += " /srv " + itSelection->second.m_strServer;
	if (!itSelection->second.m_strPort.IsEmpty())
		executeCmd += " /p " + itSelection->second.m_strPort;
	if (!itSelection->second.m_strDatabase.IsEmpty())
		executeCmd += " /db " + itSelection->second.m_strDatabase;
	if (!itSelection->second.m_strUser.IsEmpty())
		executeCmd += " /usr " + itSelection->second.m_strUser;
	if (!itSelection->second.m_strPassword.IsEmpty())
		executeCmd += " /pwd " + itSelection->second.m_strPassword;
	wxExecute(executeCmd);
	Close(true);
}

void ibFrameLauncher::OnButtonDesigner(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	wxString executeCmd = "designer ";
	if (!itSelection->second.m_strServer.IsEmpty())
		executeCmd += " /srv " + itSelection->second.m_strServer;
	if (!itSelection->second.m_strPort.IsEmpty())
		executeCmd += " /p " + itSelection->second.m_strPort;
	if (!itSelection->second.m_strDatabase.IsEmpty())
		executeCmd += " /db " + itSelection->second.m_strDatabase;
	if (!itSelection->second.m_strUser.IsEmpty())
		executeCmd += " /usr " + itSelection->second.m_strUser;
	if (!itSelection->second.m_strPassword.IsEmpty())
		executeCmd += " /pwd " + itSelection->second.m_strPassword;
	wxExecute(executeCmd);
	Close(true);
}

#include "connectionDB.h"

void ibFrameLauncher::OnButtonAdd(wxCommandEvent& event) {
	ibDialogConnection* connection_db = new ibDialogConnection(this, wxID_ANY);
	const int res = connection_db->ShowModal();
	if (res == wxID_OK) {

		CListInfo listInfo; 

		listInfo.m_strServer = connection_db->GetServer();
		listInfo.m_strPort = connection_db->GetPort();
		listInfo.m_strDatabase = connection_db->GetDBName();
		listInfo.m_strUser = connection_db->GetUser();
		listInfo.m_strPassword = connection_db->GetUser();

		m_listInfoBase.emplace_back(connection_db->GetNameIB(), listInfo);
	
		m_listIBwnd->AppendString(connection_db->GetNameIB());
		m_listIBwnd->Select(m_listIBwnd->GetCount() - 1);

		m_staticDBName->SetLabel("srv: " + listInfo.m_strServer + "; db: " + listInfo.m_strDatabase);
	}

	connection_db->Destroy();
	event.Skip();
}

void ibFrameLauncher::OnButtonEdit(wxCommandEvent& event) {

	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;

	ibDialogConnection* connection_db = new ibDialogConnection(this, wxID_ANY);
	connection_db->InitConnection(itSelection->first, 
		itSelection->second.m_strServer, itSelection->second.m_strPort,
		itSelection->second.m_strDatabase,
		itSelection->second.m_strUser, itSelection->second.m_strPassword
	);
	const int res = connection_db->ShowModal();
	if (res == wxID_OK) {

		CListInfo listInfo;

		listInfo.m_strServer = connection_db->GetServer();
		listInfo.m_strPort = connection_db->GetPort();
		listInfo.m_strDatabase = connection_db->GetDBName();
		listInfo.m_strUser = connection_db->GetUser();
		listInfo.m_strPassword = connection_db->GetUser();

		m_listIBwnd->Insert(connection_db->GetNameIB(), selection);
		m_listIBwnd->Delete(selection);
		itSelection->first = connection_db->GetNameIB();
		itSelection->second = listInfo;

		m_staticDBName->SetLabel("srv: " + listInfo.m_strServer + "; db: " + listInfo.m_strDatabase);
	}
	connection_db->Destroy();
	event.Skip();
}

void ibFrameLauncher::OnButtonDelete(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	m_listIBwnd->Delete(m_listIBwnd->GetSelection());
	m_listInfoBase.erase(itSelection);
	event.Skip();
}

void ibFrameLauncher::OnButtonClose(wxCommandEvent& event) {
	Close(true);
}