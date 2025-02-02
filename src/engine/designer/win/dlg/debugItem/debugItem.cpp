#include "debugItem.h"

#include "backend/debugger/debugClient.h"


void CDialogDebugItem::RefreshDebugList()
{
	m_listAvailable.clear(); m_listAttached.clear();

	m_availableList->ClearAll();
	m_availableList->AppendColumn(_("User"), wxLIST_FORMAT_LEFT, 250);
	m_availableList->AppendColumn(_("Computer"), wxLIST_FORMAT_LEFT, 150);
	m_availableList->AppendColumn(_("Port"), wxLIST_FORMAT_LEFT, 50);

	for (auto connection : debugClient->GetConnections()) {
		if (connection->GetConnectionType() != ConnectionType::ConnectionType_Scanner)
			continue;
		if (!connection->IsConnected())
			continue;
		long index = m_availableList->InsertItem(m_availableList->GetItemCount(), connection->GetUserName());
		m_availableList->SetItem(index, 0, connection->GetUserName());
		m_availableList->SetItem(index, 1, connection->GetComputerName());
		m_availableList->SetItem(index, 2, stringUtils::IntToStr(connection->GetPort()));

		m_listAvailable.push_back(
			std::pair<unsigned short, wxString>(connection->GetPort(), connection->GetHostName())
		);
	}

	m_attachedList->ClearAll();
	m_attachedList->AppendColumn(_("User"), wxLIST_FORMAT_LEFT, 250);
	m_attachedList->AppendColumn(_("Computer"), wxLIST_FORMAT_LEFT, 150);
	m_attachedList->AppendColumn(_("Port"), wxLIST_FORMAT_LEFT, 50);

	for (auto connection : debugClient->GetConnections()) {
		if (connection->GetConnectionType() != ConnectionType::ConnectionType_Debugger)
			continue;
		if (!connection->IsConnected())
			continue;
		long index = m_attachedList->InsertItem(m_attachedList->GetItemCount(), connection->GetUserName());
		m_attachedList->SetItem(index, 0, connection->GetUserName());
		m_attachedList->SetItem(index, 1, connection->GetComputerName());
		m_attachedList->SetItem(index, 2, stringUtils::IntToStr(connection->GetPort()));

		m_listAttached.push_back(
			std::pair<unsigned short, wxString>(connection->GetPort(), connection->GetHostName())
		);
	}
}

CDialogDebugItem::CDialogDebugItem(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : wxDialog(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	wxStaticBoxSizer* sbSizerAvailable = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, _("Available debug items:")), wxVERTICAL);
	m_availableList = new wxListCtrl(sbSizerAvailable->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	m_availableList->Bind(wxEVT_LIST_ITEM_ACTIVATED, &CDialogDebugItem::OnAvailableItemSelected, this);

	sbSizerAvailable->Add(m_availableList, 1, wxEXPAND);
	mainSizer->Add(sbSizerAvailable, 1, wxEXPAND);

	wxStaticBoxSizer* sbSizerAttached = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, _("Attached debug items:")), wxVERTICAL);
	m_attachedList = new wxListCtrl(sbSizerAttached->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	m_attachedList->Bind(wxEVT_LIST_ITEM_ACTIVATED, &CDialogDebugItem::OnAttachedItemSelected, this);
	sbSizerAttached->Add(m_attachedList, 1, wxEXPAND);

	mainSizer->Add(sbSizerAttached, 1, wxEXPAND);

	this->SetSizer(mainSizer);
	this->Layout();
	this->Centre(wxBOTH);

	RefreshDebugList();

	m_connectionScanner = new wxTimer;
	m_connectionScanner->Bind(wxEVT_TIMER, &CDialogDebugItem::OnIdleHandler, this);
	m_connectionScanner->Start(1000);
}

CDialogDebugItem::~CDialogDebugItem()
{
	m_attachedList->Unbind(wxEVT_LIST_ITEM_ACTIVATED, &CDialogDebugItem::OnAttachedItemSelected, this);
	m_availableList->Unbind(wxEVT_LIST_ITEM_ACTIVATED, &CDialogDebugItem::OnAvailableItemSelected, this);

	if (m_connectionScanner->IsRunning()) {
		m_connectionScanner->Stop();
	}
	m_connectionScanner->Unbind(wxEVT_TIMER, &CDialogDebugItem::OnIdleHandler, this);
	delete m_connectionScanner;
}

void CDialogDebugItem::OnAttachedItemSelected(wxListEvent &event)
{
	auto foundedConnection =
		debugClient->FindDebugger(m_listAttached[event.GetIndex()].second, m_listAttached[event.GetIndex()].first);

	if (foundedConnection != nullptr) {
		foundedConnection->DetachConnection();
	}

	m_attachedList->DeleteItem(event.GetIndex());
	m_listAttached.erase(m_listAttached.begin() + event.GetIndex());
}

void CDialogDebugItem::OnAvailableItemSelected(wxListEvent &event)
{
	debugClient->ConnectToDebugger(
		m_listAvailable[event.GetIndex()].second, 
		m_listAvailable[event.GetIndex()].first
	);

	m_availableList->DeleteItem(event.GetIndex());
	m_listAvailable.erase(m_listAvailable.begin() + event.GetIndex());
}

void CDialogDebugItem::OnIdleHandler(wxTimerEvent &event)
{
	RefreshDebugList();
}
