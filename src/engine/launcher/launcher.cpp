#include "launcher.h"
#include "backend/appData.h"

#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/xml/xml.h>

namespace {

wxString FindSiblingExecutable(const wxString& exeName) {
	wxFileName selfPath(wxStandardPaths::Get().GetExecutablePath());
	wxString dir = selfPath.GetPath();

#if defined(__WXOSX__) || defined(__APPLE__)
	// If running inside a .app bundle, the executable is at:
	//   Foo.app/Contents/MacOS/foo
	// The sibling app would be at:
	//   <parent>/Bar.app/Contents/MacOS/bar
	// First, try the same directory (flat layout in build/bin/Debug)
	wxString flatPath = dir + wxFileName::GetPathSeparator() + exeName;
	if (wxFileExists(flatPath))
		return flatPath;

	// Try sibling .app bundle: go up 3 levels from Contents/MacOS/launcher
	// to reach the directory containing launcher.app, then look for exeName.app
	wxFileName bundlePath(dir);
	bundlePath.RemoveLastDir(); // MacOS -> Contents
	bundlePath.RemoveLastDir(); // Contents -> launcher.app
	bundlePath.RemoveLastDir(); // launcher.app -> parent dir
	wxString siblingApp = bundlePath.GetFullPath() + exeName + ".app"
		+ wxFileName::GetPathSeparator() + "Contents"
		+ wxFileName::GetPathSeparator() + "MacOS"
		+ wxFileName::GetPathSeparator() + exeName;
	if (wxFileExists(siblingApp))
		return siblingApp;

	// Fallback: same directory
	return flatPath;
#else
	// Windows / Linux: sibling executable in the same directory
	wxString path = dir + wxFileName::GetPathSeparator() + exeName;
#ifdef __WXMSW__
	path += ".exe";
#endif
	return path;
#endif
}

wxString BuildLaunchCommand(const wxString& exeName, const CListInfo& info) {
	wxString exePath = FindSiblingExecutable(exeName);
	wxString cmd = wxT("\"") + exePath + wxT("\"");

	if (info.m_bFileMode) {
		if (!info.m_strFilePath.IsEmpty())
			cmd += wxT(" -file \"") + info.m_strFilePath + wxT("\"");
	}
	else {
		if (!info.m_strServer.IsEmpty())
			cmd += wxT(" -srv ") + info.m_strServer;
		if (!info.m_strPort.IsEmpty())
			cmd += wxT(" -p ") + info.m_strPort;
		if (!info.m_strDatabase.IsEmpty())
			cmd += wxT(" -db ") + info.m_strDatabase;
		if (!info.m_strUser.IsEmpty())
			cmd += wxT(" -usr ") + info.m_strUser;
		if (!info.m_strPassword.IsEmpty())
			cmd += wxT(" -pwd ") + info.m_strPassword;
	}

	// Pass locale from appData singleton (already read from backend.conf)
	if (appData != nullptr) {
		wxString locale = appData->GetLocale();
		if (!locale.IsEmpty())
			cmd += wxT(" -lc ") + locale;
	}

	return cmd;
}

} // anonymous namespace

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
					else if (nodeItem->GetName() == "filemode" && nodeItem->GetChildren()) {
						listInfo.m_bFileMode = (nodeItem->GetChildren()->GetContent() == "1");
					}
					else if (nodeItem->GetName() == "filepath" && nodeItem->GetChildren()) {
						listInfo.m_strFilePath = nodeItem->GetChildren()->GetContent();
					}
					else if (nodeItem->GetName() == "server" && nodeItem->GetChildren()) {
						listInfo.m_strServer = nodeItem->GetChildren()->GetContent();
					}
					else if (nodeItem->GetName() == "port" && nodeItem->GetChildren()) {
						listInfo.m_strPort = nodeItem->GetChildren()->GetContent();
					}
					else if (nodeItem->GetName() == "database" && nodeItem->GetChildren()) {
						listInfo.m_strDatabase = nodeItem->GetChildren()->GetContent();
					}
					else if (nodeItem->GetName() == "user" && nodeItem->GetChildren()) {
						listInfo.m_strUser = nodeItem->GetChildren()->GetContent();
					}
					else if (nodeItem->GetName() == "password" && nodeItem->GetChildren()) {
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

		wxXmlNode* xmlNode_filemode = new wxXmlNode(wxXML_ELEMENT_NODE, "filemode");
		xmlNode_filemode->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, list.second.m_bFileMode ? "1" : "0"));
		xmlItem->AddChild(xmlNode_filemode);

		wxXmlNode* xmlNode_filepath = new wxXmlNode(wxXML_ELEMENT_NODE, "filepath");
		xmlNode_filepath->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, list.second.m_strFilePath));
		xmlItem->AddChild(xmlNode_filepath);

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
	m_listIBwnd->SetFont(wxFont(13, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));

	m_listIBwnd->Bind(wxEVT_LISTBOX, &ibFrameLauncher::OnSelectedList, this);
	m_listIBwnd->Bind(wxEVT_LISTBOX_DCLICK, &ibFrameLauncher::OnSelectedDClickList, this);

	sizerLeft->Add(m_listIBwnd, 1, wxALL | wxEXPAND, 8);

	m_staticDBName = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	m_staticDBName->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
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

static wxString FormatInfoLabel(const CListInfo& info) {
	if (info.m_bFileMode) {
		return wxT("file: ") + info.m_strFilePath;
	}
	return wxT("srv: ") + info.m_strServer + wxT("; db: ") + info.m_strDatabase;
}

void ibFrameLauncher::OnSelectedList(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	m_staticDBName->SetLabel(FormatInfoLabel(itSelection->second));
}

void ibFrameLauncher::OnSelectedDClickList(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	wxString executeCmd = BuildLaunchCommand("enterprise", itSelection->second);
	wxExecute(executeCmd);
	Close(true);
}

void ibFrameLauncher::OnButtonEnterprise(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	wxString executeCmd = BuildLaunchCommand("enterprise", itSelection->second);
	wxExecute(executeCmd);
	Close(true);
}

void ibFrameLauncher::OnButtonDesigner(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	wxString executeCmd = BuildLaunchCommand("designer", itSelection->second);
	wxExecute(executeCmd);
	Close(true);
}

#include "connectionDB.h"

void ibFrameLauncher::OnButtonAdd(wxCommandEvent& event) {
	ibDialogConnection* connection_db = new ibDialogConnection(this, wxID_ANY);
	const int res = connection_db->ShowModal();
	if (res == wxID_OK) {

		CListInfo listInfo;

		listInfo.m_bFileMode = connection_db->IsFileMode();
		listInfo.m_strFilePath = connection_db->GetFilePath();
		listInfo.m_strServer = connection_db->GetServer();
		listInfo.m_strPort = connection_db->GetPort();
		listInfo.m_strDatabase = connection_db->GetDBName();
		listInfo.m_strUser = connection_db->GetUser();
		listInfo.m_strPassword = connection_db->GetPassword();

		m_listInfoBase.emplace_back(connection_db->GetNameIB(), listInfo);

		m_listIBwnd->AppendString(connection_db->GetNameIB());
		m_listIBwnd->Select(m_listIBwnd->GetCount() - 1);

		m_staticDBName->SetLabel(FormatInfoLabel(listInfo));
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
		itSelection->second.m_bFileMode,
		itSelection->second.m_strFilePath,
		itSelection->second.m_strServer, itSelection->second.m_strPort,
		itSelection->second.m_strDatabase,
		itSelection->second.m_strUser, itSelection->second.m_strPassword
	);
	const int res = connection_db->ShowModal();
	if (res == wxID_OK) {

		CListInfo listInfo;

		listInfo.m_bFileMode = connection_db->IsFileMode();
		listInfo.m_strFilePath = connection_db->GetFilePath();
		listInfo.m_strServer = connection_db->GetServer();
		listInfo.m_strPort = connection_db->GetPort();
		listInfo.m_strDatabase = connection_db->GetDBName();
		listInfo.m_strUser = connection_db->GetUser();
		listInfo.m_strPassword = connection_db->GetPassword();

		m_listIBwnd->Insert(connection_db->GetNameIB(), selection);
		m_listIBwnd->Delete(selection + 1);
		itSelection->first = connection_db->GetNameIB();
		itSelection->second = listInfo;

		m_staticDBName->SetLabel(FormatInfoLabel(listInfo));
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
