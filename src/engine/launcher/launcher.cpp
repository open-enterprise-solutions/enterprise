#include "launcher.h"
#include "backend/appData.h"
#include "core/launchTicket.h"
#include "core/fileSystem.h"
#include "core/startProcess.h"   // ibStartWebServer

#include <wx/filename.h>
#include <wx/log.h>
#include "deskgui/univ/messageDialog.h"   // ibMessageBox — a start that failed is said
#include <wx/stdpaths.h>
#include <wx/utils.h>   // wxLaunchDefaultBrowser, wxYieldIfNeeded
#include <wx/file.h>    // the list's file, read and written whole

#include "core/exception.h"         // ibCoreException — a list that is not well-formed
#include "core/xml/xmlDocument.h"   // ibXmlDocument — the list as read
#include "core/xml/xmlText.h"       // ibXmlWriter — the list as written

namespace {

wxString BuildLaunchCommand(const wxString& exeName, const ibListInfo& info, ibLaunchSecrets& secrets) {
	// The program beside this one — a flat build's designer beside launcher, a bundle's designer.app beside launcher.app
	// (ibExecutablePath).
	wxString cmd = wxT("\"") + wxString(ibExecutablePath(exeName)) + wxT("\"");
	secrets = ibLaunchSecrets();

	if (info.m_connection == ibLauncherConnection::File) {
		if (!info.m_strFilePath.IsEmpty())
			cmd += wxT(" -file \"") + info.m_strFilePath + wxT("\"");
	}
	else {
		// The cluster's base and the web's publication — one road in the client, the server's protocol.
		cmd += wxT(" -address \"") + info.GetAddress() + wxT("\"");
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

wxString ibListInfo::GetAddress() const {
	switch (m_connection) {
	case ibLauncherConnection::Server:
		return wxT("oes://") + m_strServer + wxT("/") + m_strDatabase;
	case ibLauncherConnection::Web:
		return m_strAddress;
	default:
		return wxEmptyString;
	}
}

void ibDialogLauncher::LoadListIB() {

	wxLogNull logNo;

	wxString directory =
		wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir::Dir_Cache) + wxFileName::GetPathSeparator() + wxT("OES");

	// Make sure the directory exists.
	wxFileName strFileName(directory, "ib_list.xml");

	ibXmlDocument document;
	try {
		wxFile file;
		wxString text;
		if (file.Open(strFileName.GetFullPath()) && file.ReadAll(&text, wxConvUTF8))
			document.Load(text, strFileName.GetFullPath());
	}
	catch (const ibCoreException&) {
		// A list that is not well-formed is as none.
	}
	if (document.GetRoot() != nullptr) {
		const ibXmlNode* root = document.GetRoot();
		const ibXmlNode* node = root->GetChildren();
		while (node != nullptr) {
			if (node->GetName() == "item") {
				ibListInfo listInfo; wxString strNameIB;
				// A list written before the cluster and the web: filemode, 1 — a file base, 0 — a base on a DBMS server,
				// which comes back as the cluster's base of the same name on the same host (its port, the DBMS's, is not read).
				wxString strFileMode;
				bool hasConnection = false;
				const ibXmlNode* nodeItem = node->GetChildren();
				while (nodeItem != nullptr) {
					if (nodeItem->GetName() == "name" && nodeItem->GetChildren()) {
						strNameIB = nodeItem->GetChildren()->GetContent();
					}
					else if (nodeItem->GetName() == "connection" && nodeItem->GetChildren()) {
						const wxString connection = nodeItem->GetChildren()->GetContent();
						listInfo.m_connection = connection == "server" ? ibLauncherConnection::Server
							: connection == "web" ? ibLauncherConnection::Web : ibLauncherConnection::File;
						hasConnection = true;
					}
					else if (nodeItem->GetName() == "filemode" && nodeItem->GetChildren()) {
						strFileMode = nodeItem->GetChildren()->GetContent();
					}
					else if (nodeItem->GetName() == "filepath" && nodeItem->GetChildren()) {
						listInfo.m_strFilePath = nodeItem->GetChildren()->GetContent();
					}
					else if (nodeItem->GetName() == "server" && nodeItem->GetChildren()) {
						listInfo.m_strServer = nodeItem->GetChildren()->GetContent();
					}
					else if (nodeItem->GetName() == "database" && nodeItem->GetChildren()) {
						listInfo.m_strDatabase = nodeItem->GetChildren()->GetContent();
					}
					else if (nodeItem->GetName() == "address" && nodeItem->GetChildren()) {
						listInfo.m_strAddress = nodeItem->GetChildren()->GetContent();
					}
					nodeItem = nodeItem->GetNext();
				}
				if (!hasConnection && strFileMode == "0")
					listInfo.m_connection = ibLauncherConnection::Server;

				m_listInfoBase.emplace_back(strNameIB, listInfo);
				m_listIBwnd->AppendString(strNameIB);
			}
			// Past a node that is not an item too: it stood in the loop for ever.
			node = node->GetNext();
		}
	}
}

void ibDialogLauncher::SaveListIB() {

	ibXmlWriter writer(ibT("ib_list.xml"), ibCodePage::Of(ibCodePage::kUtf8));
	writer.SetIndent(true);
	writer.WriteXmlDeclaration(ibT("UTF-8"));
	writer.WriteStartElement(ibT("ib"), ibString());

	// <tag>text</tag>, inside the item.
	const auto writeText = [&writer](const ibString& tag, const ibString& text) {
		writer.WriteStartElement(tag, ibString());
		writer.WriteText(text);
		writer.WriteEndElement();
	};

	for (auto& list : m_listInfoBase) {
		writer.WriteStartElement(ibT("item"), ibString());
		writeText(ibT("name"), list.first);

		const ibLauncherConnection connection = list.second.m_connection;
		writeText(ibT("connection"),
			connection == ibLauncherConnection::Server ? ibT("server") : connection == ibLauncherConnection::Web ? ibT("web") : ibT("file"));

		writeText(ibT("filepath"), list.second.m_strFilePath);
		writeText(ibT("server"), list.second.m_strServer);
		writeText(ibT("database"), list.second.m_strDatabase);
		writeText(ibT("address"), list.second.m_strAddress);

		writer.WriteEndElement();
	}

	writer.WriteEndElement();
	writer.Finish();

	wxString directory =
		wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir::Dir_Cache) + wxFileName::GetPathSeparator() + wxT("OES");

	// Make sure the directory exists.
	wxFileName strFileName(directory, "ib_list.xml");
	strFileName.Mkdir(0777, wxPATH_MKDIR_FULL);

	wxFile file(strFileName.GetFullPath(), wxFile::write);
	if (file.IsOpened())
		file.Write(writer.Take(), wxConvUTF8);
}

ibDialogLauncher::ibDialogLauncher(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : ibDialog(parent, id, title, pos, size, style)
{
	Bind(wxEVT_CLOSE_WINDOW, &ibDialogLauncher::OnCloseWindow, this);

	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizerLeft = new wxBoxSizer(wxVERTICAL);

	// the bases' names two points over the window's font, the row the theme's for that text (GetListboxItemHeight) - as
	// every list's (four over until 2026-11-13, then two - "too tall", then none - "the text is small", then one - "the
	// row is right, the font is crooked": 13 pixels of Segoe UI; then two, chosen from four sizes drawn; Max each time);
	// on GTK3/Cocoa the same
	m_listIBwnd = new ibListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE);
	wxFont listFont = GetFont();
	listFont.SetPointSize(listFont.GetPointSize() + 2);
	m_listIBwnd->SetFont(listFont);

	m_listIBwnd->Bind(wxEVT_LISTBOX, &ibDialogLauncher::OnSelectedList, this);
	m_listIBwnd->Bind(wxEVT_LISTBOX_DCLICK, &ibDialogLauncher::OnSelectedDClickList, this);

	sizerLeft->Add(m_listIBwnd, 1, wxALL | wxEXPAND, 8);

	// the chosen base's address under the list, in the window's own font
	m_staticDBName = new ibStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sizerLeft->Add(m_staticDBName, 0, wxALL | wxEXPAND, FromDIP(5));

	mainSizer->Add(sizerLeft, 1, wxEXPAND, FromDIP(5));

	wxBoxSizer* sizerRight = new wxBoxSizer(wxVERTICAL);

	// The workspace twice - its window and the browser - then, apart, the studio.
	m_buttonEnterprise = new ibButton(this, wxID_ANY, _("Workspace (GUI)"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonEnterprise->SetBitmap(ibGetLauncherPicture(ibLauncherPicture::Enterprise));
	m_buttonEnterprise->Bind(wxEVT_BUTTON, &ibDialogLauncher::OnButtonEnterprise, this);

	sizerRight->Add(m_buttonEnterprise, 0, wxALL | wxEXPAND, FromDIP(5));

	m_buttonWeb = new ibButton(this, wxID_ANY, _("Workspace (Web)"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonWeb->SetBitmap(ibGetLauncherPicture(ibLauncherPicture::Web));
	m_buttonWeb->Bind(wxEVT_BUTTON, &ibDialogLauncher::OnButtonWeb, this);

	sizerRight->Add(m_buttonWeb, 0, wxALL | wxEXPAND, FromDIP(5));
	sizerRight->AddSpacer(FromDIP(10));

	m_buttonDesigner = new ibButton(this, wxID_ANY, _("Studio (GUI)"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonDesigner->SetBitmap(ibGetLauncherPicture(ibLauncherPicture::Designer));
	m_buttonDesigner->Bind(wxEVT_BUTTON, &ibDialogLauncher::OnButtonDesigner, this);

	sizerRight->Add(m_buttonDesigner, 0, wxALL | wxEXPAND, FromDIP(5));
	sizerRight->Add(0, 0, 1, wxEXPAND, FromDIP(5));

	m_buttonAdd = new ibButton(this, wxID_ANY, _("Add"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonAdd->SetBitmap(ibGetLauncherPicture(ibLauncherPicture::Add));
	sizerRight->Add(m_buttonAdd, 0, wxALL | wxEXPAND, FromDIP(5));

	m_buttonAdd->Bind(wxEVT_BUTTON, &ibDialogLauncher::OnButtonAdd, this);

	m_buttonEdit = new ibButton(this, wxID_ANY, _("Edit"), wxDefaultPosition, wxDefaultSize, 0);

	m_buttonEdit->SetBitmap(ibGetLauncherPicture(ibLauncherPicture::Edit));
	sizerRight->Add(m_buttonEdit, 0, wxALL | wxEXPAND, FromDIP(5));

	m_buttonEdit->Bind(wxEVT_BUTTON, &ibDialogLauncher::OnButtonEdit, this);

	m_buttonDelete = new ibButton(this, wxID_ANY, _("Delete"), wxDefaultPosition, wxDefaultSize, 0);

	m_buttonDelete->SetBitmap(ibGetLauncherPicture(ibLauncherPicture::Delete));
	sizerRight->Add(m_buttonDelete, 0, wxALL | wxEXPAND, FromDIP(5));

	m_buttonDelete->Bind(wxEVT_BUTTON, &ibDialogLauncher::OnButtonDelete, this);

	sizerRight->Add(0, 0, 1, wxEXPAND, FromDIP(5));

	m_buttonExit = new ibButton(this, wxID_EXIT, _("Exit"), wxDefaultPosition, wxDefaultSize, 0);

	m_buttonExit->SetBitmap(ibGetLauncherPicture(ibLauncherPicture::Exit));
	sizerRight->Add(m_buttonExit, 0, wxALL | wxEXPAND, FromDIP(5));

	m_buttonExit->Bind(wxEVT_BUTTON, &ibDialogLauncher::OnButtonClose, this);

	mainSizer->Add(sizerRight, 0, wxEXPAND, FromDIP(5));

	this->SetSizer(mainSizer);
	this->Layout();

	this->Centre(wxBOTH);

	LoadListIB();
}

ibDialogLauncher::~ibDialogLauncher()
{
	SaveListIB();
}

static wxString FormatInfoLabel(const ibListInfo& info) {
	if (info.m_connection == ibLauncherConnection::File) {
		return wxT("file: ") + info.m_strFilePath;
	}
	return info.GetAddress();
}

void ibDialogLauncher::OnSelectedList(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	m_staticDBName->SetLabel(FormatInfoLabel(itSelection->second));
}

// ONE ROAD TO START AN APPLICATION — the double click, Enterprise and Designer were three copies of it.
void ibDialogLauncher::StartApplication(const wxString& appName) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	ibLaunchSecrets secrets;
	const wxString executeCmd = BuildLaunchCommand(appName, itSelection->second, secrets);

	// ⭐ A START THAT FAILED IS SAID, AND THE LAUNCHER STAYS. The spawn answers 0 when nothing was started; the
	// launcher used to close regardless, so a missing executable looked exactly like a click that did nothing —
	// which is how the designer "would not start" on a Mac with nothing on screen to say why.
	if (ibStartProcess(executeCmd, secrets) == 0) {
		ibMessageBox(ibString::Format(_("Could not start %s.\n\n%s"), appName, executeCmd),
			_("Launch OES"), wxOK | wxICON_ERROR, this);
		return;
	}
	Close(true);
}

void ibDialogLauncher::OnSelectedDClickList(wxCommandEvent& event) {
	StartApplication(wxT("appworkspace"));
}

void ibDialogLauncher::OnButtonEnterprise(wxCommandEvent& event) {
	StartApplication(wxT("appworkspace"));
}

void ibDialogLauncher::OnButtonDesigner(wxCommandEvent& event) {
	StartApplication(wxT("appstudio"));
}

void ibDialogLauncher::OnButtonWeb(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	const ibListInfo& info = itSelection->second;

	// A PUBLICATION is on a web server already: its page is the link.
	if (info.m_connection == ibLauncherConnection::Web) {
		wxLaunchDefaultBrowser(info.m_strAddress);
		Close(true);
		return;
	}

	// THE WEB SERVER publishes the base — a file base under its folder's name, the cluster's under its name there — and
	// the browser opens its page; a login is the browser's (webserver/webServer.h).
	wxString cmd = wxT("\"") + wxString(ibExecutablePath(wxT("webserver"))) + wxT("\"");
	if (info.m_connection == ibLauncherConnection::File)
		cmd += wxT(" --file=\"") + info.m_strFilePath + wxT("\"");
	else
		cmd += wxT(" --server=\"") + info.GetAddress() + wxT("\"");
	if (appData != nullptr) {
		wxString locale = appData->GetLocale();
		if (!locale.IsEmpty())
			cmd += wxT(" --locale=") + locale;
	}

	// The window keeps painting while the base opens, and the browser is launched before the launcher closes.
	ibString url;
	if (ibStartWebServer(cmd, url, []() { wxYieldIfNeeded(); }) == 0) {
		ibMessageBox(ibString::Format(_("Could not start %s.\n\n%s"), wxT("webserver"), cmd),
			_("Launch OES"), wxOK | wxICON_ERROR, this);
		return;
	}
	if (!url.IsEmpty())
		wxLaunchDefaultBrowser(url);
	Close(true);
}

#include "connectionDB.h"

void ibDialogLauncher::OnButtonAdd(wxCommandEvent& event) {
	ibDialogConnection* connection_db = new ibDialogConnection(this, wxID_ANY);
	const int res = connection_db->ShowModal();
	if (res == wxID_OK) {

		const ibListInfo listInfo = connection_db->GetListInfo();

		m_listInfoBase.emplace_back(connection_db->GetNameIB(), listInfo);

		m_listIBwnd->AppendString(connection_db->GetNameIB());
		m_listIBwnd->Select(m_listIBwnd->GetCount() - 1);

		m_staticDBName->SetLabel(FormatInfoLabel(listInfo));
	}

	connection_db->Destroy();
	event.Skip();
}

void ibDialogLauncher::OnButtonEdit(wxCommandEvent& event) {

	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;

	ibDialogConnection* connection_db = new ibDialogConnection(this, wxID_ANY);
	connection_db->InitConnection(itSelection->first, itSelection->second);
	const int res = connection_db->ShowModal();
	if (res == wxID_OK) {

		const ibListInfo listInfo = connection_db->GetListInfo();

		m_listIBwnd->Insert(connection_db->GetNameIB(), selection);
		m_listIBwnd->Delete(selection + 1);
		itSelection->first = connection_db->GetNameIB();
		itSelection->second = listInfo;

		m_staticDBName->SetLabel(FormatInfoLabel(listInfo));
	}
	connection_db->Destroy();
	event.Skip();
}

void ibDialogLauncher::OnButtonDelete(wxCommandEvent& event) {
	int selection = m_listIBwnd->GetSelection();
	if (selection == wxNOT_FOUND) return;
	auto itSelection = m_listInfoBase.begin() + selection;
	m_listIBwnd->Delete(m_listIBwnd->GetSelection());
	m_listInfoBase.erase(itSelection);
	event.Skip();
}

void ibDialogLauncher::OnButtonClose(wxCommandEvent& event) {
	Close(true);
}

// A modeless dialog closed is only hidden (wxDialogBase::EndDialog): the launcher's is destroyed, as a frame is.
void ibDialogLauncher::OnCloseWindow(wxCloseEvent& event) {
	Destroy();
}
