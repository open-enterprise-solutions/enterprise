#include "appData.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "backend/backend_mainFrame.h"
#include "backend/backend_exception.h"

///////////////////////////////////////////////////////////////////////////////
#define timeInterval 3
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//								CApplicationDataSessionThread
///////////////////////////////////////////////////////////////////////////////

wxThread::ExitCode CApplicationData::CApplicationDataSessionThread::Entry()
{
	while (!TestDestroy()) {
		if (appData == nullptr) break;
		appData->RefreshActiveUser();
		wxSleep(timeInterval);
	}
	return (wxThread::ExitCode)0;
}

///////////////////////////////////////////////////////////////////////////////

static wxCriticalSection sm_sessionLocker;

///////////////////////////////////////////////////////////////////////////////
//								CApplicationData
///////////////////////////////////////////////////////////////////////////////

bool CApplicationData::TableAlreadyCreated()
{
	return db_query->TableExists(user_table) &&
		db_query->TableExists(session_table);
}

///////////////////////////////////////////////////////////////////////////////

void CApplicationData::CreateTableUser()
{
	if (!db_query->TableExists(user_table)) {
		if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL) {
			db_query->RunQuery("create table %s ("
				"guid              VARCHAR(36)   NOT NULL PRIMARY KEY,"
				"name              VARCHAR(64)  NOT NULL,"
				"fullName          VARCHAR(128)  NOT NULL,"
				"changed		   TIMESTAMP  NOT NULL,"
				"dataSize          INTEGER       NOT NULL,"
				"binaryData        BYTEA      NOT NULL);", user_table);
		}
		else {
			db_query->RunQuery("create table %s ("
				"guid              VARCHAR(36)   NOT NULL PRIMARY KEY,"
				"name              VARCHAR(64)  NOT NULL,"
				"fullName          VARCHAR(128)  NOT NULL,"
				"changed		   TIMESTAMP  NOT NULL,"
				"dataSize          INTEGER       NOT NULL,"
				"binaryData        BLOB      NOT NULL);", user_table);
		}
		db_query->RunQuery("create index if not exists user_index on %s (guid, name);", user_table);
	}
}

void CApplicationData::CreateTableSession()
{
	if (!db_query->TableExists(session_table)) {

		db_query->RunQuery("CREATE TABLE %s ("
			"session              VARCHAR(36) NOT NULL PRIMARY KEY,"
			"userName             VARCHAR(64) NOT NULL,"
			"application	   INTEGER  NOT NULL,"
			"started		   TIMESTAMP  NOT NULL,"
			"lastActive		   TIMESTAMP  NOT NULL,"
			"computer          VARCHAR(128) NOT NULL);", session_table);

		db_query->RunQuery("create index if not exists session_index on %s (session, userName);", session_table);
	}
}

void CApplicationData::CreateTableEvent()
{
	if (!db_query->TableExists(event_table)) {
	}
}

///////////////////////////////////////////////////////////////////////////////
bool CApplicationData::HasAllowedUser() const
{
	IDatabaseResultSet* result = db_query->RunQueryWithResults("SELECT name FROM %s;", user_table);
	if (result == nullptr) return false;
	bool hasUsers = false;
	if (result->Next()) hasUsers = true;
	result->Close();
	return hasUsers;
}

#include "fileSystem/fs.h"

bool CApplicationData::AuthenticationUser(const wxString& userName, const wxString& userPassword) const
{
	if (!HasAllowedUser()) return true;
	IDatabaseResultSet* resultSet = db_query->RunQueryWithResults("SELECT * FROM %s WHERE name = '%s';", user_table, userName);
	if (resultSet == nullptr) return false;
	if (resultSet->Next()) {
		wxString md5Password; wxMemoryBuffer buffer;
		resultSet->GetResultBlob("binaryData", buffer);
		CMemoryReader reader(buffer.GetData(), buffer.GetDataLen());
		reader.r_stringZ(md5Password);
		if (md5Password == CApplicationData::ComputeMd5(userPassword)) return true;
	}
	return false;
}

void CApplicationData::RefreshActiveUser()
{
	const wxDateTime& currentTime = wxDateTime::Now();
	// fisrt update current session
	IPreparedStatement* preparedStatement = db_query->PrepareStatement("UPDATE %s SET lastActive = ? WHERE session = ?", session_table);
	if (preparedStatement == nullptr) return;
	preparedStatement->SetParamDate(1, currentTime);
	preparedStatement->SetParamString(2, m_sessionGuid.str());
	const int changedRows = preparedStatement->RunQuery();
	db_query->CloseStatement(preparedStatement);
	if (sm_sessionLocker.TryEnter()) {
		IPreparedStatement* preparedStatement = db_query->PrepareStatement("DELETE FROM %s WHERE lastActive < ?", session_table);
		if (preparedStatement == nullptr) return;
		preparedStatement->SetParamDate(1, currentTime.Subtract(wxTimeSpan(0, 0, timeInterval)));
		preparedStatement->RunQuery();
		db_query->CloseStatement(preparedStatement);
		sm_sessionLocker.Leave();
	}
}

bool CApplicationData::AuthenticationAndSetUser(const wxString& userName, const wxString& userPassword)
{
	if (AuthenticationUser(userName, userPassword)) {
		m_strUserIB = userName;
		m_strPasswordIB = userPassword;
		return true;
	}
	return false;
}

wxArrayString CApplicationData::GetAllowedUser() const
{
	IDatabaseResultSet* resultSet = db_query->RunQueryWithResults("SELECT name FROM %s;", user_table);
	if (resultSet == nullptr) return wxArrayString();
	wxArrayString arrayUsers;
	while (resultSet->Next()) arrayUsers.Add(resultSet->GetResultString("name"));
	resultSet->Close();
	return arrayUsers;
}

bool CApplicationData::StartSession(const wxString& userName, const wxString& userPassword)
{
	if (!CloseSession()) return false;
	bool hasError = false;
	RefreshActiveUser();
	if (m_runMode == eRunMode::eDESIGNER_MODE) {
		IDatabaseResultSet* resultSet = db_query->RunQueryWithResults("SELECT * FROM %s WHERE application = %i", session_table, m_runMode);
		if (resultSet == nullptr) {
			return false;
		}
		if (resultSet->Next()) {
			hasError = true;
		}
		resultSet->Close();
	}
	if (hasError) {
		try {
			CBackendException::Error(_("Error locking infobase for configuration!"));
		}
		catch (...) {

		}
		return false;
	}

	sm_sessionLocker.Enter();

	//start empty session 
	IPreparedStatement* preparedStatement = db_query->PrepareStatement("INSERT INTO %s (session, userName, application, started, lastActive, computer) VALUES (?,?,?,?,?,?);", session_table);
	if (preparedStatement == nullptr) return false;
	preparedStatement->SetParamString(1, m_sessionGuid.str());
	preparedStatement->SetParamString(2, wxEmptyString); //empty user!
	preparedStatement->SetParamInt(3, m_runMode);
	preparedStatement->SetParamDate(4, m_startedDate);
	preparedStatement->SetParamDate(5, wxDateTime::Now());
	preparedStatement->SetParamString(6, m_strComputer);

	hasError = preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
	db_query->CloseStatement(preparedStatement);

	if (hasError)  return false;

	if (!AuthenticationAndSetUser(userName, userPassword)) {
		if (backend_mainFrame == nullptr) 
			return false;
		if (!backend_mainFrame->AuthenticationUser(userName, userPassword)) 
			return false;
	}

	if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL) {
		//update empty session 
		IPreparedStatement* preparedStatement =
			db_query->PrepareStatement("INSERT INTO %s (session, userName, application, started, lastActive, computer) VALUES(? , ? , ? , ? , ? , ? )"
				"ON CONFLICT (session) DO UPDATE SET session = excluded.session, "
				"userName = excluded.userName, application = excluded.application, "
				"started = excluded.started, lastActive = excluded.lastActive, "
				"computer = excluded.computer; ", session_table
			);
		if (preparedStatement == nullptr) return false;
		preparedStatement->SetParamString(1, m_sessionGuid.str());
		preparedStatement->SetParamString(2, m_strUserIB);
		preparedStatement->SetParamInt(3, m_runMode);
		preparedStatement->SetParamDate(4, m_startedDate);
		preparedStatement->SetParamDate(5, wxDateTime::Now());
		preparedStatement->SetParamString(6, m_strComputer);

		hasError = preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
		db_query->CloseStatement(preparedStatement);
	}
	else {
		//update empty session 
		IPreparedStatement* preparedStatement =
			db_query->PrepareStatement("UPDATE OR INSERT INTO %s (session, userName, application, started, lastActive, computer) VALUES (?,?,?,?,?,?) MATCHING(session);", session_table);
		if (preparedStatement == nullptr)
			return false;
		preparedStatement->SetParamString(1, m_sessionGuid.str());
		preparedStatement->SetParamString(2, m_strUserIB);
		preparedStatement->SetParamInt(3, m_runMode);
		preparedStatement->SetParamDate(4, m_startedDate);
		preparedStatement->SetParamDate(5, wxDateTime::Now());
		preparedStatement->SetParamString(6, m_strComputer);

		hasError = preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
		db_query->CloseStatement(preparedStatement);
		return !hasError;
	}
	if (!hasError) {
		m_sessionThread = std::shared_ptr<CApplicationDataSessionThread>(new CApplicationDataSessionThread);
		if (m_sessionThread->Run() != wxThreadError::wxTHREAD_NO_ERROR) return false;
	}
	return !hasError;
}

bool CApplicationData::CloseSession()
{
	const int result = db_query->RunQuery("DELETE FROM %s WHERE session = '%s'", session_table, m_sessionGuid.str());
	if (m_sessionThread != nullptr && m_sessionThread->IsRunning())
		m_sessionThread->Delete();
	return result != DATABASE_LAYER_QUERY_RESULT_ERROR;
}