////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuider
//	Description : app info
////////////////////////////////////////////////////////////////////////////

#include "appData.h"

//databases
#include <3rdparty/databaseLayer/odbc/odbcDatabaseLayer.h>
#include <3rdparty/databaseLayer/postgres/postgresDatabaseLayer.h>
#include <3rdparty/databaseLayer/firebird/firebirdDatabaseLayer.h>

//sandbox
#include "core/metadata/moduleManager/moduleManager.h"
#include "core/metadata/metadata.h"

///////////////////////////////////////////////////////////////////////////////
//								ApplicationData
///////////////////////////////////////////////////////////////////////////////

ApplicationData* ApplicationData::s_instance = NULL;

bool ApplicationData::CreateAppData(eDBMode dbMode, eRunMode runMode, const wxString& strDatabase)
{
	if (s_instance != NULL)
		return false;

	DatabaseLayer* db = NULL;

	if (dbMode == eDBMode::eFirebird)
		db = new FirebirdDatabaseLayer(strDatabase + wxT("\\") + wxT("sys.database"));
	else if (dbMode == eDBMode::ePostgres)
		db = new PostgresDatabaseLayer(strDatabase + wxT("\\") + wxT("sys.database"));

	if (db != NULL) {
		bool result = db->IsOpen();
		if (!result) {
			delete db; return NULL;
		}
		s_instance = new ApplicationData(db, runMode);
		return true;
	}

	return false;
}

bool ApplicationData::CreateAppData(eDBMode dbMode, eRunMode runMode, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	if (s_instance != NULL)
		return false;

	DatabaseLayer* db = NULL;

	if (dbMode == eDBMode::eFirebird)
		db = new FirebirdDatabaseLayer(strDatabase + wxT("\\") + wxT("sys.database"), strUser, strPassword);
	else if (dbMode == eDBMode::ePostgres)
		db = new PostgresDatabaseLayer(strDatabase + wxT("\\") + wxT("sys.database"), strUser, strPassword);

	if (db != NULL) {
		bool result = db->IsOpen();
		if (!result) {
			delete db; return NULL;
		}
		s_instance = new ApplicationData(db, runMode);
		return true;
	}

	return false;
}

bool ApplicationData::CreateAppData(eDBMode dbMode, eRunMode runMode, const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword)
{
	if (s_instance != NULL)
		return false;

	DatabaseLayer* db = NULL;

	if (dbMode == eDBMode::eFirebird)
		db = new FirebirdDatabaseLayer(strServer, strDatabase, strUser, strPassword);
	else if (dbMode == eDBMode::ePostgres)
		db = new PostgresDatabaseLayer(strServer, strDatabase, strUser, strPassword);

	if (db != NULL) {
		bool result = db->IsOpen();
		if (!result) {
			delete db; return NULL;
		}
		s_instance = new ApplicationData(db, runMode);
		return true;
	}

	return false;
}

bool ApplicationData::CreateAppData(eDBMode dbMode, eRunMode runMode, const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword, const wxString& strRole)
{
	if (s_instance != NULL)
		return false;

	DatabaseLayer* db = NULL;

	if (dbMode == eDBMode::eFirebird)
		db = new FirebirdDatabaseLayer(strDatabase);
	else if (dbMode == eDBMode::ePostgres)
		db = new PostgresDatabaseLayer(strDatabase);

	if (db != NULL) {
		bool result = db->IsOpen();
		if (!result) {
			delete db; return NULL;
		}
		s_instance = new ApplicationData(db, runMode);
		return true;
	}

	return false;
}

void ApplicationData::Destroy()
{
	metadataDestroy();

	if (s_instance != NULL) {

		s_instance->Disconnect();
		//Close connection and delete ptr 
		wxDELETE(s_instance);
	}
}

#define timeInterval 5

void ApplicationData::OnIdleHandler(wxTimerEvent& event)
{
	RefreshActiveUsers();
	event.Skip();
}

bool ApplicationData::Initialize(const wxString& user, const wxString& password)
{
	return appData->Connect(user, password);
}

void ApplicationData::RefreshActiveUsers()
{
	const wxDateTime &currentTime = wxDateTime::Now();

	// fisrt update current session
	PreparedStatement* preparedStatement =
		databaseLayer->PrepareStatement("UPDATE %s SET lastActive = ? WHERE session = ?", IConfigMetadata::GetActiveUsersTableName());
	if (preparedStatement == NULL)
		return; 
	preparedStatement->SetParamDate(1, currentTime);
	preparedStatement->SetParamString(2, m_sessionGuid.str());

	preparedStatement->RunQuery();
	databaseLayer->CloseStatement(preparedStatement);

	if (m_sessionLocker.TryEnter()) {
		PreparedStatement* preparedStatement =
			databaseLayer->PrepareStatement("DELETE FROM %s WHERE lastActive < ?", IConfigMetadata::GetActiveUsersTableName());
		if (preparedStatement == NULL)
			return;
		preparedStatement->SetParamDate(1, currentTime.Subtract(wxTimeSpan(0, 0, timeInterval)));
		preparedStatement->RunQuery();
		databaseLayer->CloseStatement(preparedStatement);
		m_sessionLocker.Leave();
	}
}

ApplicationData::ApplicationData(DatabaseLayer* db, eRunMode runMode) :
	m_objDb(db), m_runMode(runMode),
	m_sessionGuid(wxNewUniqueGuid),
	m_startedDate(wxDateTime::Now()),
	m_computerName(wxGetHostName()),
	m_sessionTimer(NULL)
{
}

ApplicationData::~ApplicationData()
{
	//close session disp 
	wxDELETE(m_sessionTimer);
	//Close connection 
	wxDELETE(m_objDb);
}

bool ApplicationData::Connect(const wxString& user, const wxString& password)
{
	if (!metadataCreate(m_runMode)) {
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

	if (!metadata->LoadMetadata())
		return false;

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
	DatabaseResultSet* resultSet =
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

#include <3rdparty/email/utils/wxmd5.hpp>

wxString ApplicationData::ComputeMd5(const wxString& userPassword) const
{
	if (userPassword.Length() > 0) {
		return wxMD5::ComputeMd5(userPassword);
	}

	return wxEmptyString;
}

bool ApplicationData::HasAllowedUsers() const
{
	DatabaseResultSet* resultSet =
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

bool ApplicationData::CheckLoginAndPassword(const wxString& userName, const wxString& userPassword) const
{
	if (!HasAllowedUsers()) {
		return true;
	}

	DatabaseResultSet* resultSet =
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

		CAuthorizationWnd* autorization = new CAuthorizationWnd();

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

#include <3rdparty/databaseLayer/databaseErrorCodes.h>

bool ApplicationData::StartSession(const wxString& userName, const wxString& userPassword)
{
	if (!CloseSession()) {
		return false;
	}

	bool hasError = false;

	RefreshActiveUsers();

	if (m_runMode == eRunMode::eDESIGNER_MODE) {
		DatabaseResultSet* resultSet = databaseLayer->RunQueryWithResults("SELECT * FROM %s WHERE application = %i", IConfigMetadata::GetActiveUsersTableName(), m_runMode);
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
	PreparedStatement* preparedStatement =
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
	databaseLayer->CloseStatement(preparedStatement);

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

		if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL) {
			//update empty session 
			PreparedStatement* preparedStatement =
				databaseLayer->PrepareStatement("INSERT INTO % s(session, userName, application, started, lastActive, computer) VALUES(? , ? , ? , ? , ? , ? )"
					"ON CONFLICT (session) DO UPDATE SET session = excluded.session, "
					"userName = excluded.userName, application = excluded.application, "
					"started = excluded.started,"" lastActive = excluded.lastActive, "
					"computer = excluded.computer; ", IConfigMetadata::GetActiveUsersTableName()
				);
			if (preparedStatement == NULL)
				return false;
			preparedStatement->SetParamString(1, m_sessionGuid.str());
			preparedStatement->SetParamString(2, m_userName);
			preparedStatement->SetParamInt(3, m_runMode);
			preparedStatement->SetParamDate(4, m_startedDate);
			preparedStatement->SetParamDate(5, wxDateTime::Now());
			preparedStatement->SetParamString(6, m_computerName);

			hasError = preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
			databaseLayer->CloseStatement(preparedStatement);
			return !hasError;
		}
		else {
			//update empty session 
			PreparedStatement* preparedStatement =
				databaseLayer->PrepareStatement("UPDATE OR INSERT INTO %s (session, userName, application, started, lastActive, computer) VALUES (?,?,?,?,?,?) MATCHING(session);", IConfigMetadata::GetActiveUsersTableName());
			if (preparedStatement == NULL)
				return false;
			preparedStatement->SetParamString(1, m_sessionGuid.str());
			preparedStatement->SetParamString(2, m_userName);
			preparedStatement->SetParamInt(3, m_runMode);
			preparedStatement->SetParamDate(4, m_startedDate);
			preparedStatement->SetParamDate(5, wxDateTime::Now());
			preparedStatement->SetParamString(6, m_computerName);

			hasError = preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
			databaseLayer->CloseStatement(preparedStatement);
			return !hasError;
		}
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
