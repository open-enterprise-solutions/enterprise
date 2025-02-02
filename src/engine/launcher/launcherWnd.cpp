#include "launcherWnd.h"

#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/xml/xml.h>

void CFrameLauncher::LoadIBList() {

	wxLogNull logNo;

	wxString directory =
		wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir::Dir_Cache) + wxT("\\OES");

	// Make sure the directory exists.
	wxFileName strFileName(directory, "ib_list.xml");

	wxXmlDocument document;
	if (document.Load(strFileName.GetFullPath())) {
		wxXmlNode* root = document.GetRoot();
		if (root->GetName() == "list") {
			wxXmlNode* node = root->GetChildren();
			while (node != nullptr) {
				wxString data;
				if (node->GetName() == "item") {
					wxString nameIB, pathIB;
					wxXmlNode* nodeItem = node->GetChildren();
					while (nodeItem != nullptr) {
						if (nodeItem->GetName() == "name") {
							nameIB = nodeItem->GetChildren()->GetContent();
						}
						else if (nodeItem->GetName() == "pathIB") {
							pathIB = nodeItem->GetChildren()->GetContent();
						}
						nodeItem = nodeItem->GetNext();
					}
					m_listInfoBase.emplace_back(nameIB, pathIB);
					m_listIBwnd->AppendString(nameIB);
				}

				node = node->GetNext();
			}
		}
	}

	wxXmlNode* root = document.GetRoot();

	wxXmlNode* node = root->GetChildren();
	while (node != nullptr) {
		if (node->GetName() == "server" && node->GetChildren()) {
			//m_textCtrlServer->SetValue(node->GetChildren()->GetContent());
		}
		else if (node->GetName() == "port" && node->GetChildren()) {
			//m_textCtrlPort->SetValue(node->GetChildren()->GetContent());
		}
		else if (node->GetName() == "database" && node->GetChildren()) {
			//m_textCtrlDataBase->SetValue(node->GetChildren()->GetContent());
		}
		else if (node->GetName() == "user" && node->GetChildren()) {
			//m_textCtrlUser->SetValue(node->GetChildren()->GetContent());
		}
		else if (node->GetName() == "password" && node->GetChildren()) {
			//m_textCtrlPassword->SetValue(node->GetChildren()->GetContent());
		}
		node = node->GetNext();
	}
}

void CFrameLauncher::SaveIBList() {

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
		wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir::Dir_Cache) + wxT("\\OES");

	// Make sure the directory exists.
	wxFileName strFileName(directory, "ib_list.xml");
	strFileName.Mkdir(0777, wxPATH_MKDIR_FULL);

	document.Save(strFileName.GetFullPath());
}

CFrameLauncher::CFrameLauncher(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : wxFrame(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);
	this->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));

	wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizerLeft = new wxBoxSizer(wxVERTICAL);

	m_listIBwnd = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE);
	m_listIBwnd->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("Arial")));

	m_listIBwnd->Bind(wxEVT_LISTBOX, &CFrameLauncher::OnSelectedList, this);
	m_listIBwnd->Bind(wxEVT_LISTBOX_DCLICK, &CFrameLauncher::OnSelectedDClickList, this);

	sizerLeft->Add(m_listIBwnd, 1, wxALL | wxEXPAND, 5);

	m_staticPath = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	m_staticPath->Wrap(-1);
	sizerLeft->Add(m_staticPath, 0, wxALL | wxEXPAND, 5);

	mainSizer->Add(sizerLeft, 1, wxEXPAND, 5);

	wxBoxSizer* sizerRight = new wxBoxSizer(wxVERTICAL);

	m_buttonEnterprise = new wxButton(this, wxID_ANY, _("Enterprise"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonEnterprise->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_FORWARD, wxART_BUTTON));
	m_buttonEnterprise->Bind(wxEVT_BUTTON, &CFrameLauncher::OnButtonEnterprise, this);

	sizerRight->Add(m_buttonEnterprise, 0, wxALL | wxEXPAND, 5);

	m_buttonDesigner = new wxButton(this, wxID_ANY, _("Designer"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonDesigner->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_FORWARD, wxART_BUTTON));
	m_buttonDesigner->Bind(wxEVT_BUTTON, &CFrameLauncher::OnButtonDesigner, this);

	sizerRight->Add(m_buttonDesigner, 0, wxALL | wxEXPAND, 5);
	sizerRight->Add(0, 0, 1, wxEXPAND, 5);

	m_buttonAdd = new wxButton(this, wxID_ANY, _("Add"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonAdd->SetBitmap(wxArtProvider::GetBitmap(wxART_NEW, wxART_BUTTON));
	sizerRight->Add(m_buttonAdd, 0, wxALL | wxEXPAND, 5);

	m_buttonAdd->Bind(wxEVT_BUTTON, &CFrameLauncher::OnButtonAdd, this);

	m_buttonEdit = new wxButton(this, wxID_ANY, _("Edit"), wxDefaultPosition, wxDefaultSize, 0);

	m_buttonEdit->SetBitmap(wxArtProvider::GetBitmap(wxART_PASTE, wxART_BUTTON));
	sizerRight->Add(m_buttonEdit, 0, wxALL | wxEXPAND, 5);

	m_buttonEdit->Bind(wxEVT_BUTTON, &CFrameLauncher::OnButtonEdit, this);

	m_buttonDelete = new wxButton(this, wxID_ANY, _("Delete"), wxDefaultPosition, wxDefaultSize, 0);

	m_buttonDelete->SetBitmap(wxArtProvider::GetBitmap(wxART_DELETE, wxART_BUTTON));
	sizerRight->Add(m_buttonDelete, 0, wxALL | wxEXPAND, 5);

	m_buttonDelete->Bind(wxEVT_BUTTON, &CFrameLauncher::OnButtonDelete, this);

	sizerRight->Add(0, 0, 1, wxEXPAND, 5);

	m_buttonExit = new wxButton(this, wxID_EXIT, _("Exit"), wxDefaultPosition, wxDefaultSize, 0);

	m_buttonExit->SetBitmap(wxArtProvider::GetBitmap(wxART_QUIT, wxART_BUTTON));
	sizerRight->Add(m_buttonExit, 0, wxALL | wxEXPAND, 5);

	m_buttonExit->Bind(wxEVT_BUTTON, &CFrameLauncher::OnButtonClose, this);

	mainSizer->Add(sizerRight, 0, wxEXPAND, 5);

	this->SetSizer(mainSizer);
	this->Layout();

	this->Centre(wxBOTH);

	LoadIBList();
}

CFrameLauncher::~CFrameLauncher()
{
	SaveIBList();
}

void CFrameLauncher::OnSelectedList(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	//m_staticPath->SetLabel(itSelection->second);
}

void CFrameLauncher::OnSelectedDClickList(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	//wxExecute("enterprise -ib " + itSelection->second);
	Close(true);
}

void CFrameLauncher::OnButtonEnterprise(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	//wxExecute("enterprise -ib " + itSelection->second);
	Close(true);
}

void CFrameLauncher::OnButtonDesigner(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	//wxExecute("designer -ib " + itSelection->second);
	Close(true);
}

#include "selectIBWnd.h"

void CFrameLauncher::OnButtonAdd(wxCommandEvent& event) {
	CSelectIBWnd* selectWnd = new CSelectIBWnd(this, wxID_ANY);
	int res = selectWnd->ShowModal();
	if (res == wxID_OK) {
		m_listInfoBase.emplace_back(selectWnd->GetIbName(), selectWnd->GetIbPath());
		m_listIBwnd->AppendString(selectWnd->GetIbName());
		m_listIBwnd->Select(m_listIBwnd->GetCount() - 1);
	}
	selectWnd->Destroy();
	event.Skip();
}

void CFrameLauncher::OnButtonEdit(wxCommandEvent& event) {

	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;

	CSelectIBWnd* selectWnd = new CSelectIBWnd(this, wxID_ANY);
	//selectWnd->Init(itSelection->first, itSelection->second);
	int res = selectWnd->ShowModal();
	//if (res == wxID_OK) {
	//	m_listIBwnd->Insert(selectWnd->GetIbName(), selection);
	//	m_listIBwnd->Delete(selection);
	//	itSelection->first = selectWnd->GetIbName();
	//	itSelection->second = selectWnd->GetIbPath();
	//}
	selectWnd->Destroy();
	event.Skip();
}

void CFrameLauncher::OnButtonDelete(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	m_listIBwnd->Delete(m_listIBwnd->GetSelection());
	m_listInfoBase.erase(itSelection);
	event.Skip();
}

void CFrameLauncher::OnButtonClose(wxCommandEvent& event) {
	Close(true);
}