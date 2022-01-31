#include "activeUsersWnd.h"
#include "databaseLayer/databaseLayer.h"
#include "metadata/metadata.h"
#include "appData.h"

void CActiveUsersWnd::RefreshUsers()
{
	m_activeUsers->ClearAll();
	m_activeUsers->AppendColumn(_("User"), wxLIST_FORMAT_LEFT, 190);
	m_activeUsers->AppendColumn(_("Application"), wxLIST_FORMAT_LEFT, 120);
	m_activeUsers->AppendColumn(_("Started"), wxLIST_FORMAT_LEFT, 120);
	m_activeUsers->AppendColumn(_("Computer"), wxLIST_FORMAT_LEFT, 145);

	DatabaseResultSet *resultSet =
		databaseLayer->RunQueryWithResults("SELECT userName, application, started, computer FROM %s", IConfigMetadata::GetActiveUsersTableName());

	while (resultSet->Next()) {
		
		wxString userName = resultSet->GetResultString("userName");
		eRunMode runMode = (eRunMode)resultSet->GetResultInt("application");
		wxDateTime startedDate = resultSet->GetResultDate("started");
		wxString computerName = resultSet->GetResultString("computer");

		long index = m_activeUsers->InsertItem(m_activeUsers->GetItemCount(), userName);
		
		m_activeUsers->SetItem(index, 0, userName);
		m_activeUsers->SetItem(index, 1, appData->GetModeDescr(runMode));
		m_activeUsers->SetItem(index, 2, startedDate.Format("%d.%m.%Y %H:%M:%S"));
		m_activeUsers->SetItem(index, 3, computerName);
	}

	resultSet->Close();
}

CActiveUsersWnd::CActiveUsersWnd(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : 
	wxDialog(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	m_activeUsers = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	mainSizer->Add(m_activeUsers, 1, wxALL | wxEXPAND, 5);

	this->SetSizer(mainSizer);
	this->Layout();

	this->Centre(wxBOTH);

	RefreshUsers();

	m_activeUsersScanner = new wxTimer;
	m_activeUsersScanner->Bind(wxEVT_TIMER, &CActiveUsersWnd::OnIdleHandler, this);
	m_activeUsersScanner->Start(1000 * 5);
}

CActiveUsersWnd::~CActiveUsersWnd()
{
	if (m_activeUsersScanner->IsRunning()) {
		m_activeUsersScanner->Stop();
	}
	m_activeUsersScanner->Unbind(wxEVT_TIMER, &CActiveUsersWnd::OnIdleHandler, this);
	delete m_activeUsersScanner;
}

void CActiveUsersWnd::OnIdleHandler(wxTimerEvent & event)
{
	RefreshUsers();
}