////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuider
//	Description : app info
////////////////////////////////////////////////////////////////////////////

#include "appData.h"

//sandbox
#include "metadata/moduleManager/moduleManager.h"
#include "metadata/metadata.h"

//databases
#include "databaseLayer/odbc/odbcDatabaseLayer.h"
#include "databaseLayer/postgres/postgresDatabaseLayer.h"
#include "databaseLayer/firebird/firebirdDatabaseLayer.h"

//mainFrame
#include "frontend/mainFrame.h"

///////////////////////////////////////////////////////////////////////////////
//								ApplicationData
///////////////////////////////////////////////////////////////////////////////

ApplicationData* ApplicationData::s_instance = NULL;

ApplicationData* ApplicationData::Get(const wxString &rootdir)
{
	if (!s_instance) {
		s_instance = new ApplicationData(rootdir);
	}
	return s_instance;
}

void ApplicationData::Destroy()
{
	metadataDestroy();

	if (s_instance) {
		s_instance->Disconnect();
		//Close connection and delete ptr 
		wxDELETE(s_instance);
	}
}

#define timeInterval 5

void ApplicationData::OnIdleHandler(wxTimerEvent &event)
{
	RefreshActiveUsers();
	event.Skip();
}

bool ApplicationData::Initialize(eRunMode modeRun, const wxString &user, const wxString &password)
{
	return appData->Connect(modeRun, user, password);
}

void ApplicationData::RefreshActiveUsers()
{
	wxDateTime currentTime = wxDateTime::Now();

	// fisrt update current session
	PreparedStatement *preparedStatement =
		databaseLayer->PrepareStatement("UPDATE %s SET lastActive = ? WHERE session = ?", IConfigMetadata::GetActiveUsersTableName());

	preparedStatement->SetParamDate(1, currentTime);
	preparedStatement->SetParamString(2, m_sessionGuid.str());

	preparedStatement->RunQuery();
	preparedStatement->Close();

	if (m_sesssionLocker.TryEnter()) {
		PreparedStatement *preparedStatement =
			databaseLayer->PrepareStatement("DELETE FROM %s WHERE lastActive < ?", IConfigMetadata::GetActiveUsersTableName());
		preparedStatement->SetParamDate(1, currentTime.Subtract(wxTimeSpan(0, 0, timeInterval)));
		preparedStatement->RunQuery();
		preparedStatement->Close();
		m_sesssionLocker.Leave();
	}
}

ApplicationData::ApplicationData(const wxString &projectDir) :
	m_projectDir(projectDir), m_objDb(new FirebirdDatabaseLayer()), m_runMode(eRunMode::START_MODE), m_sessionTimer(NULL)
{
	//start new connection 
	if (m_objDb->Open(projectDir + wxT("\\") + wxT("sys.database"))) {

		//create new session 
		m_sessionGuid = Guid::newGuid();

		m_startedDate = wxDateTime::Now();
		m_computerName = wxGetHostName();
	}
}

ApplicationData::~ApplicationData()
{
	//close session disp 
	wxDELETE(m_sessionTimer);
	//Close connection 
	wxDELETE(m_objDb);
}

bool ApplicationData::Connect(eRunMode runMode, const wxString &user, const wxString &password)
{
	m_runMode = runMode;

	if (!metadataCreate(runMode)) {
		return false;
	}

	if (!StartSession(user, password)) { //start session
		return false;
	}

	m_sessionTimer = new wxTimer();
	m_sessionTimer->Bind(wxEVT_TIMER, &ApplicationData::OnIdleHandler, this);

	if (!m_sessionTimer->Start(1000 * timeInterval)) {
		wxDELETE(m_sessionTimer);
		return false;
	}

	mainFrameCreate(runMode);

	if (!metadata->LoadMetadata()) {
		mainFrameDestroy();
		return false;
	}

	if (CMainFrame::Get()) {
		mainFrame->SetFocus();     // focus on my window
		mainFrame->Raise();
		mainFrame->Maximize();
		mainFrame->Show();    // show the window	
	}

	//set project dir 
	m_projectDir = wxGetCwd();

	return true;
}

bool ApplicationData::Disconnect()
{
	if (m_sessionTimer) {
		m_sessionTimer->Unbind(wxEVT_TIMER, &ApplicationData::OnIdleHandler, this);
		if (m_sessionTimer->IsRunning()) {
			m_sessionTimer->Stop();
		}
	}

	if (!CloseSession()) {
		return false;
	}

	return true;
}

wxArrayString ApplicationData::GetAllowedUsers() const
{
	DatabaseResultSet *resultSet =
		databaseLayer->RunQueryWithResults("SELECT name FROM %s;", IConfigMetadata::GetUsersTableName());
	if (resultSet == NULL) {
		return wxArrayString();
	}
	wxArrayString arrayUsers;
	while (resultSet->Next()) {
		arrayUsers.push_back(resultSet->GetResultString("name"));
	}
	resultSet->Close();
	return arrayUsers;
}

wxString ApplicationData::ComputeMd5() const
{
	return ComputeMd5(m_userPassword);
}

#include "email/utils/wxmd5.hpp"

wxString ApplicationData::ComputeMd5(const wxString &userPassword) const
{
	if (userPassword.Length() > 0) {
		return wxMD5::ComputeMd5(userPassword);
	}

	return wxEmptyString;
}

bool ApplicationData::HasAllowedUsers() const
{
	DatabaseResultSet *resultSet =
		databaseLayer->RunQueryWithResults("SELECT name FROM %s;", IConfigMetadata::GetUsersTableName());
	if (resultSet == NULL)
		return false;
	bool hasUsers = false;
	if (resultSet->Next()) {
		hasUsers = true;
	}
	resultSet->Close();
	return hasUsers;
}

bool ApplicationData::CheckLoginAndPassword(const wxString &userName, const wxString &userPassword) const
{
	if (!HasAllowedUsers()) {
		return true;
	}

	DatabaseResultSet *resultSet =
		databaseLayer->RunQueryWithResults("SELECT * FROM %s WHERE name = '%s';", IConfigMetadata::GetUsersTableName(), userName);

	if (resultSet == NULL)
		return false;

	if (resultSet->Next()) {
		wxString md5Password; wxMemoryBuffer buffer;
		resultSet->GetResultBlob("binaryData", buffer);
		CMemoryReader reader(buffer.GetData(), buffer.GetDataLen());
		reader.r_stringZ(md5Password);
		if (md5Password == ApplicationData::ComputeMd5(userPassword)) {
			return true;
		}
	}

	return false;
}

#include "frontend/windows/authorizationWnd.h"

bool ApplicationData::ShowAuthorizationWnd()
{
	if (HasAllowedUsers()) {

		CAuthorizationWnd *autorization = new CAuthorizationWnd();

		autorization->SetLogin(m_userName);
		autorization->SetPassword(m_userPassword);

		int result = autorization->ShowModal();

		autorization->Destroy();

		if (result == wxID_CANCEL) {
			return false;
		}

		m_userName = autorization->GetLogin();
		m_userPassword = autorization->GetPassword();
	}

	return true;
}

#include "databaseLayer/databaseErrorCodes.h"

bool ApplicationData::StartSession(const wxString &userName, const wxString &userPassword)
{
	if (!CloseSession()) {
		return false;
	}

	bool hasError = false;

	RefreshActiveUsers();

	if (m_runMode == eRunMode::DESIGNER_MODE) {
		DatabaseResultSet *resultSet = databaseLayer->RunQueryWithResults("SELECT * FROM %s WHERE application = %i", IConfigMetadata::GetActiveUsersTableName(), m_runMode);
		if (resultSet == NULL) {
			return false;
		}
		if (resultSet->Next()) {
			hasError = true;
		}
		resultSet->Close();
	}

	if (hasError) {
		if (AlwaysShowWndows()) {
			wxMessageBox(_("Error locking infobase for configuration!"));
		}
		return false;
	}

	//start empty session 
	PreparedStatement *preparedStatement =
		databaseLayer->PrepareStatement("INSERT INTO %s (session, userName, application, started, lastActive, computer) VALUES (?,?,?,?,?,?);", IConfigMetadata::GetActiveUsersTableName());

	if (preparedStatement == NULL) {
		return false;
	}

	preparedStatement->SetParamString(1, m_sessionGuid.str());
	preparedStatement->SetParamString(2, wxEmptyString); //empty user!
	preparedStatement->SetParamInt(3, m_runMode);
	preparedStatement->SetParamDate(4, m_startedDate);
	preparedStatement->SetParamDate(5, wxDateTime::Now());
	preparedStatement->SetParamString(6, m_computerName);

	hasError = preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
	preparedStatement->Close();

	if (hasError) {
		return false;
	}

	bool successful =
		CheckLoginAndPassword(userName, userPassword);

	if (!successful && AlwaysShowWndows()) {
		if (!ShowAuthorizationWnd()) {
			return false;
		}
		successful = true;
	}
	else if (successful) {
		m_userName = userName;
		m_userPassword = userPassword;
	}

	if (successful) {

		//update empty session 
		PreparedStatement *preparedStatement =
			databaseLayer->PrepareStatement("UPDATE OR INSERT INTO %s (session, userName, application, started, lastActive, computer) VALUES (?,?,?,?,?,?) MATCHING(session);", IConfigMetadata::GetActiveUsersTableName());
		if (preparedStatement == NULL) {
			return false;
		}
		preparedStatement->SetParamString(1, m_sessionGuid.str());
		preparedStatement->SetParamString(2, m_userName);
		preparedStatement->SetParamInt(3, m_runMode);
		preparedStatement->SetParamDate(4, m_startedDate);
		preparedStatement->SetParamDate(5, wxDateTime::Now());
		preparedStatement->SetParamString(6, m_computerName);

		hasError = preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
		preparedStatement->Close();

		return !hasError;
	}

	return false;
}

bool ApplicationData::CloseSession()
{
	int result =
		databaseLayer->RunQuery("DELETE FROM %s WHERE session = '%s'", IConfigMetadata::GetActiveUsersTableName(), m_sessionGuid.str());
	return result != DATABASE_LAYER_QUERY_RESULT_ERROR;
}

bool ApplicationData::SaveConfiguration()
{
	return metadata->SaveMetadata(saveConfigFlag);
}
