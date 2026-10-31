#include "debugItem.h"
#include "designer-thin/backend/debugger/debugClient.h"
#include "core/stringUtils.h"

void ibDialogDebugItem::RefreshDebugList()
{
	m_listAvailable.clear(); m_listAttached.clear();

	m_availableList->ClearAll();
	m_availableList->AppendColumn(_("User"), wxLIST_FORMAT_LEFT, 250);
	m_availableList->AppendColumn(_("Computer"), wxLIST_FORMAT_LEFT, 150);
	m_availableList->AppendColumn(_("Port"), wxLIST_FORMAT_LEFT, 50);

	m_attachedList->ClearAll();
	m_attachedList->AppendColumn(_("User"), wxLIST_FORMAT_LEFT, 250);
	m_attachedList->AppendColumn(_("Computer"), wxLIST_FORMAT_LEFT, 150);
	m_attachedList->AppendColumn(_("Port"), wxLIST_FORMAT_LEFT, 50);

	if (debugClient != nullptr) {

		debugClient->SearchServer();

		debugClient->ForEachConnection([this](const auto& connection) {

			if (connection.GetConnectionType() != ConnectionType::ConnectionType_Scanner)
				return;

			if (!connection.IsVerifiedConnection())
				return;

			const long index = m_availableList->InsertItem(m_availableList->GetItemCount(), connection.GetUserName());

			m_availableList->SetItem(index, 0, connection.GetUserName());
			m_availableList->SetItem(index, 1, connection.GetComputerName());
			m_availableList->SetItem(index, 2, stringUtils::IntToStr(connection.GetPort()));

			m_listAvailable.emplace_back(
				std::pair<unsigned short, wxString>(connection.GetPort(), connection.GetHostName())
			);
		});

		debugClient->ForEachConnection([this](const auto& connection) {

			if (connection.GetConnectionType() != ConnectionType::ConnectionType_Debugger)
				return;

			if (!connection.IsVerifiedConnection())
				return;

			const long index = m_attachedList->InsertItem(m_attachedList->GetItemCount(), connection.GetUserName());

			m_attachedList->SetItem(index, 0, connection.GetUserName());
			m_attachedList->SetItem(index, 1, connection.GetComputerName());
			m_attachedList->SetItem(index, 2, stringUtils::IntToStr(connection.GetPort()));

			m_listAttached.emplace_back(
				std::pair<unsigned short, wxString>(connection.GetPort(), connection.GetHostName())
			);
		});
	}
}

ibDialogDebugItem::ibDialogDebugItem(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style)
	: wxDialog(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	wxStaticBoxSizer* sbSizerAvailable = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, _("Available debug items:")), wxVERTICAL);
	m_availableList = new wxListCtrl(sbSizerAvailable->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	m_availableList->Bind(wxEVT_LIST_ITEM_ACTIVATED, &ibDialogDebugItem::OnAvailableItemSelected, this);

	sbSizerAvailable->Add(m_availableList, 1, wxEXPAND);
	mainSizer->Add(sbSizerAvailable, 1, wxEXPAND);

	wxStaticBoxSizer* sbSizerAttached = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, _("Attached debug items:")), wxVERTICAL);
	m_attachedList = new wxListCtrl(sbSizerAttached->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	m_attachedList->Bind(wxEVT_LIST_ITEM_ACTIVATED, &ibDialogDebugItem::OnAttachedItemSelected, this);
	sbSizerAttached->Add(m_attachedList, 1, wxEXPAND);

	mainSizer->Add(sbSizerAttached, 1, wxEXPAND);

	this->SetSizer(mainSizer);
	this->Layout();
	this->Centre(wxBOTH);

	RefreshDebugList();

	m_connectionScanner = new wxTimer;
	m_connectionScanner->Bind(wxEVT_TIMER, &ibDialogDebugItem::OnIdleHandler, this);
	m_connectionScanner->Start(1000);
}

ibDialogDebugItem::~ibDialogDebugItem()
{
	m_attachedList->Unbind(wxEVT_LIST_ITEM_ACTIVATED, &ibDialogDebugItem::OnAttachedItemSelected, this);
	m_availableList->Unbind(wxEVT_LIST_ITEM_ACTIVATED, &ibDialogDebugItem::OnAvailableItemSelected, this);

	if (m_connectionScanner->IsRunning()) 
		m_connectionScanner->Stop();

	m_connectionScanner->Unbind(wxEVT_TIMER, &ibDialogDebugItem::OnIdleHandler, this);
	wxDELETE(m_connectionScanner);
}

void ibDialogDebugItem::OnAttachedItemSelected(wxListEvent& event)
{
	if (debugClient != nullptr) {
		
		debugClient->DetachConnection(
			m_listAttached[event.GetIndex()].second,
			m_listAttached[event.GetIndex()].first
		);

		m_attachedList->DeleteItem(event.GetIndex());
		m_listAttached.erase(m_listAttached.begin() + event.GetIndex());
	}
}

void ibDialogDebugItem::OnAvailableItemSelected(wxListEvent& event)
{
	if (debugClient != nullptr) {
		
		debugClient->AttachConnection(
			m_listAvailable[event.GetIndex()].second,
			m_listAvailable[event.GetIndex()].first
		);

		m_availableList->DeleteItem(event.GetIndex());
		m_listAvailable.erase(m_listAvailable.begin() + event.GetIndex());
	}
}