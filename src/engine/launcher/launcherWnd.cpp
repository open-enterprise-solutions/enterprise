#include "launcherWnd.h"

#include <wx/config.h>
#include <wx/fileconf.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/xml/xml.h>

void CLauncherWnd::LoadIBList() {

	wxLogNull logNo;

	wxString directory =
		wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir::Dir_Cache) + wxT("\\OES");

	// Make sure the directory exists.
	wxFileName fileName(directory, "ib_list.xml");

	wxXmlDocument document;
	if (document.Load(fileName.GetFullPath())) {
		wxXmlNode* root = document.GetRoot();
		if (root->GetName() == "list") {
			wxXmlNode* node = root->GetChildren();
			while (node != NULL) {
				wxString data;
				if (node->GetName() == "item")
				{
					wxString nameIB, pathIB; 
					wxXmlNode* nodeItem = node->GetChildren();
					while (nodeItem != NULL) {
						if (nodeItem->GetName() == "nameIB") {
							nameIB = nodeItem->GetChildren()->GetContent();
						}
						else if (nodeItem->GetName() == "pathIB") {
							pathIB = nodeItem->GetChildren()->GetContent();
						}
						nodeItem = nodeItem->GetNext();
					}
					m_aList.emplace_back(nameIB, pathIB);
					m_listIB->AppendString(nameIB);
				}

				node = node->GetNext();
			}
		}
	}
}

void CLauncherWnd::SaveIBList() {

	wxXmlDocument document;

	wxXmlNode* root = new wxXmlNode(wxXML_ELEMENT_NODE, "list");
	document.SetRoot(root);

	for (auto list : m_aList) {

		wxXmlNode *xmlItem = new wxXmlNode(wxXML_ELEMENT_NODE, "item");
		wxXmlNode *xmlNode_name = new wxXmlNode(wxXML_ELEMENT_NODE, "nameIB");
		xmlNode_name->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, list.first));
		xmlItem->AddChild(xmlNode_name);
		wxXmlNode *xmlNode_path = new wxXmlNode(wxXML_ELEMENT_NODE, "pathIB");
		xmlNode_path->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString, list.second));
		xmlItem->AddChild(xmlNode_path);
		root->AddChild(xmlItem);
	}

	wxString directory =
		wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir::Dir_Cache) + wxT("\\OES");

	// Make sure the directory exists.
	wxFileName fileName(directory, "ib_list.xml");
	fileName.Mkdir(0777, wxPATH_MKDIR_FULL);

	document.Save(fileName.GetFullPath());
}

CLauncherWnd::CLauncherWnd(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : wxFrame(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);
	this->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));

	wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizerLeft = new wxBoxSizer(wxVERTICAL);

	m_listIB = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, NULL, wxLB_SINGLE);
	m_listIB->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("Arial")));

	m_listIB->Bind(wxEVT_LISTBOX, &CLauncherWnd::OnSelectedList, this);
	m_listIB->Bind(wxEVT_LISTBOX_DCLICK, &CLauncherWnd::OnSelectedDClickList, this);

	sizerLeft->Add(m_listIB, 1, wxALL | wxEXPAND, 5);

	m_staticPath = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	m_staticPath->Wrap(-1);
	sizerLeft->Add(m_staticPath, 0, wxALL | wxEXPAND, 5);

	mainSizer->Add(sizerLeft, 1, wxEXPAND, 5);

	wxBoxSizer* sizerRight = new wxBoxSizer(wxVERTICAL);

	m_buttonEnterprise = new wxButton(this, wxID_ANY, _("Enterprise"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonEnterprise->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_FORWARD, wxART_BUTTON));
	m_buttonEnterprise->Bind(wxEVT_BUTTON, &CLauncherWnd::OnButtonEnterprise, this);

	sizerRight->Add(m_buttonEnterprise, 0, wxALL | wxEXPAND, 5);

	m_buttonDesigner = new wxButton(this, wxID_ANY, _("Designer"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonDesigner->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_FORWARD, wxART_BUTTON));
	m_buttonDesigner->Bind(wxEVT_BUTTON, &CLauncherWnd::OnButtonDesigner, this);

	sizerRight->Add(m_buttonDesigner, 0, wxALL | wxEXPAND, 5);
	sizerRight->Add(0, 0, 1, wxEXPAND, 5);

	m_buttonAdd = new wxButton(this, wxID_ANY, _("Add"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonAdd->SetBitmap(wxArtProvider::GetBitmap(wxART_NEW, wxART_BUTTON));
	sizerRight->Add(m_buttonAdd, 0, wxALL | wxEXPAND, 5);

	m_buttonAdd->Bind(wxEVT_BUTTON, &CLauncherWnd::OnButtonAdd, this);

	m_buttonEdit = new wxButton(this, wxID_ANY, _("Edit"), wxDefaultPosition, wxDefaultSize, 0);

	m_buttonEdit->SetBitmap(wxArtProvider::GetBitmap(wxART_PASTE, wxART_BUTTON));
	sizerRight->Add(m_buttonEdit, 0, wxALL | wxEXPAND, 5);

	m_buttonEdit->Bind(wxEVT_BUTTON, &CLauncherWnd::OnButtonEdit, this);

	m_buttonDelete = new wxButton(this, wxID_ANY, _("Delete"), wxDefaultPosition, wxDefaultSize, 0);

	m_buttonDelete->SetBitmap(wxArtProvider::GetBitmap(wxART_DELETE, wxART_BUTTON));
	sizerRight->Add(m_buttonDelete, 0, wxALL | wxEXPAND, 5);

	m_buttonDelete->Bind(wxEVT_BUTTON, &CLauncherWnd::OnButtonDelete, this);

	sizerRight->Add(0, 0, 1, wxEXPAND, 5);

	m_buttonExit = new wxButton(this, wxID_EXIT, _("Exit"), wxDefaultPosition, wxDefaultSize, 0);

	m_buttonExit->SetBitmap(wxArtProvider::GetBitmap(wxART_QUIT, wxART_BUTTON));
	sizerRight->Add(m_buttonExit, 0, wxALL | wxEXPAND, 5);

	m_buttonExit->Bind(wxEVT_BUTTON, &CLauncherWnd::OnButtonClose, this);

	mainSizer->Add(sizerRight, 0, wxEXPAND, 5);

	this->SetSizer(mainSizer);
	this->Layout();

	this->Centre(wxBOTH);

	LoadIBList();
}

CLauncherWnd::~CLauncherWnd()
{
	SaveIBList();
}

void CLauncherWnd::OnSelectedList(wxCommandEvent &event) {
	int selection = m_listIB->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_aList.begin() + selection;
	m_staticPath->SetLabel(itSelection->second);
}

void CLauncherWnd::OnSelectedDClickList(wxCommandEvent &event) {
	int selection = m_listIB->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_aList.begin() + selection;
	wxExecute("oes -m enterprise -ib " + itSelection->second);
	Close(true);
}

void CLauncherWnd::OnButtonEnterprise(wxCommandEvent &event) {
	int selection = m_listIB->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_aList.begin() + selection;
	wxExecute("oes -m enterprise -ib " + itSelection->second);
	Close(true);
}

void CLauncherWnd::OnButtonDesigner(wxCommandEvent &event) {
	int selection = m_listIB->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_aList.begin() + selection;
	wxExecute("oes -m designer -ib " + itSelection->second);
	Close(true);
}

#include "selectIBWnd.h"

void CLauncherWnd::OnButtonAdd(wxCommandEvent &event) {
	CSelectIBWnd *selectWnd = new CSelectIBWnd(this, wxID_ANY);
	int res = selectWnd->ShowModal();
	if (res == wxID_OK) {
		m_aList.emplace_back(selectWnd->GetIbName(), selectWnd->GetIbPath());
		m_listIB->AppendString(selectWnd->GetIbName());
		m_listIB->Select(m_listIB->GetCount() - 1);
	}
	event.Skip();
}

void CLauncherWnd::OnButtonEdit(wxCommandEvent &event) {

	int selection = m_listIB->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_aList.begin() + selection;

	CSelectIBWnd *selectWnd = new CSelectIBWnd(this, wxID_ANY);
	selectWnd->Init(itSelection->first, itSelection->second);
	int res = selectWnd->ShowModal();
	if (res == wxID_OK) {
		m_listIB->Insert(selectWnd->GetIbName(), selection);
		m_listIB->Delete(selection);
		itSelection->first = selectWnd->GetIbName();
		itSelection->second = selectWnd->GetIbPath();
	}
	event.Skip();
}

void CLauncherWnd::OnButtonDelete(wxCommandEvent &event) {
	int selection = m_listIB->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_aList.begin() + selection;
	m_listIB->Delete(m_listIB->GetSelection());
	m_aList.erase(itSelection);
	event.Skip();
}

void CLauncherWnd::OnButtonClose(wxCommandEvent &event) {
	Close(true);
}