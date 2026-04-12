#include "appData.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "backend/backend_mainFrame.h"
#include "backend/backend_exception.h"

///////////////////////////////////////////////////////////////////////////////
#define timeInterval 5
///////////////////////////////////////////////////////////////////////////////

wxCriticalSection ibApplicationData::ibApplicationDataSessionUpdater::ms_sessionLocker;

///////////////////////////////////////////////////////////////////////////////
//								ibApplicationDataSessionUpdater
///////////////////////////////////////////////////////////////////////////////

void ibApplicationData::ibApplicationDataSessionUpdater::Job_ClearLostSession()
{
	m_session_db->BeginTransaction();

	wxDateTime prevDateTime = m_currentDateTime.GetValue();
	(void)prevDateTime.Subtract(wxTimeSpan(0, 0, timeInterval));

	const wxString& strLastActive = prevDateTime.FormatISOCombined(' ');
	ibDatabaseResultSet* dbResultSetRecord = m_session_db->RunQueryWithResults(
		wxT("SELECT lastActive FROM %s WHERE lastActive < '%s';"),
		session_table,
		strLastActive
	);

	//clear
	m_session_db->RunQuery(
		wxT("DELETE FROM %s WHERE lastActive < '%s';"),
		session_table,
		strLastActive
	);

	m_session_db->Commit();

	if (dbResultSetRecord != nullptr)
		dbResultSetRecord->Close();

	m_session_db->CloseResultSet(dbResultSetRecord);
}

void ibApplicationData::ibApplicationDataSessionUpdater::Job_CalcActiveSession()
{
	m_session_db->BeginTransaction();

	auto dbSessionCountResult = m_session_db->RunQueryWithResults(
		wxT("SELECT session, userName, lastActive FROM %s WHERE session = '%s';"),
		session_table,
		m_session.str()
	);

	const wxDateTime& currentTime = wxDateTime::Now();

	if (dbSessionCountResult != nullptr &&
		dbSessionCountResult->Next()) {

		m_session_db->RunQuery(
			wxT("UPDATE %s SET lastActive = '%s', userName = '%s' WHERE session = '%s';"),
			session_table,
			currentTime.FormatISOCombined(' '),
			appData->GetUserName(),
			m_session.str()
		);
	}
	else {

		//start lost session 
		ibPreparedStatement* dbSessionPrepareData = m_session_db->PrepareStatement(
			wxT("INSERT INTO %s (session, userName, application, started, lastActive, computer) VALUES (?,?,?,?,?,?);"),
			session_table
		);

		dbSessionPrepareData->SetParamString(1, m_session.str());
		dbSessionPrepareData->SetParamString(2, appData->GetUserName()); //empty user!
		dbSessionPrepareData->SetParamInt(3, appData->GetAppMode());
		dbSessionPrepareData->SetParamDate(4, appData->GetStartedDate());
		dbSessionPrepareData->SetParamDate(5, currentTime);
		dbSessionPrepareData->SetParamString(6, appData->GetComputerName());

		dbSessionPrepareData->RunQuery();

		if (dbSessionPrepareData != nullptr)
			dbSessionPrepareData->Close();

		m_session_db->CloseStatement(dbSessionPrepareData);
	}

	m_session_db->Commit();

	if (dbSessionCountResult != nullptr)
		dbSessionCountResult->Close();

	m_session_db->CloseResultSet(dbSessionCountResult);
}

void ibApplicationData::ibApplicationDataSessionUpdater::Job_UpdateActiveSession()
{
	//append sessions 
	ibDatabaseResultSet* dbSessionResult = m_session_db->RunQueryWithResults(
		//wxT("SELECT userName, application, started, computer, session FROM %s ORDER BY started, session WITH LOCK SKIP LOCKED;"),
		wxT("SELECT userName, application, started, computer, session FROM %s ORDER BY started, session;"),
		session_table
	);

	wxCriticalSectionLocker enter(ms_sessionLocker);

	if (dbSessionResult != nullptr) {
		m_sessionArray.ClearSession();
		while (dbSessionResult->Next()) {
			m_sessionArray.AppendSession(
				static_cast<ibRunMode>(dbSessionResult->GetResultInt("application")),
				dbSessionResult->GetResultDate("started"),
				dbSessionResult->GetResultString("userName"),
				dbSessionResult->GetResultString("computer"),
				dbSessionResult->GetResultString("session")
			);
		}

		dbSessionResult->Close();
	}

	m_session_db->CloseResultSet(dbSessionResult);
}

void ibApplicationData::ibApplicationDataSessionUpdater::ClearLostSessionUpdater()
{
	wxCriticalSectionLocker enter(ms_sessionLocker);

	m_session_db->BeginTransaction();

	const wxDateTime& currentTime = wxDateTime::Now();
	ibDatabaseResultSet* dbResultSetRecord = m_session_db->RunQueryWithResults(
		wxT("SELECT lastActive FROM %s WHERE lastActive < '%s';"),
		session_table,
		currentTime.Subtract(wxTimeSpan(0, 0, timeInterval)).FormatISOCombined(' ')
	);

	//clear 
	m_session_db->RunQuery(
		wxT("DELETE FROM %s WHERE lastActive < '%s';"),
		session_table,
		currentTime.Subtract(wxTimeSpan(0, 0, timeInterval)).FormatISOCombined(' ')
	);

	m_session_db->Commit();

	if (dbResultSetRecord != nullptr)
		dbResultSetRecord->Close();

	m_session_db->CloseResultSet(dbResultSetRecord);

	//clear session 
	m_sessionArray.ClearSession();

	//append sessions 
	ibDatabaseResultSet* dbSessionResult = m_session_db->RunQueryWithResults(
		//wxT("SELECT userName, application, started, computer, session FROM %s ORDER BY started, session WITH LOCK SKIP LOCKED;"),
		wxT("SELECT userName, application, started, computer, session FROM %s ORDER BY started, session;"),
		session_table
	);

	if (dbSessionResult != nullptr) {
		while (dbSessionResult->Next()) {
			m_sessionArray.AppendSession(
				static_cast<ibRunMode>(dbSessionResult->GetResultInt("application")),
				dbSessionResult->GetResultDate("started"),
				dbSessionResult->GetResultString("userName"),
				dbSessionResult->GetResultString("computer"),
				dbSessionResult->GetResultString("session")
			);
		}
		dbSessionResult->Close();
	}

	m_session_db->CloseResultSet(dbSessionResult);
}

///////////////////////////////////////////////////////////////////////////////

ibApplicationData::ibApplicationDataSessionUpdater::ibApplicationDataSessionUpdater(ibApplicationData* application, const ibGuid& session) :
	wxThread(wxTHREAD_JOINABLE),
	m_sessionCreated(false), m_sessionStarted(false),
	m_sessionUpdaterLoop(false),
	m_session_db(application->m_db != nullptr ? application->m_db->Clone() : nullptr),
	m_session(session)
{
	wxThread::SetPriority(wxPRIORITY_MIN);
}

bool ibApplicationData::ibApplicationDataSessionUpdater::InitSessionUpdater()
{
	if (m_session_db == nullptr)
		return false;

	if (wxThread::Run() != wxThreadError::wxTHREAD_NO_ERROR)
		return false;

	m_sessionUpdaterLoop = true;

	while (m_sessionUpdaterLoop) {

		if (m_sessionCreated)
			break;

		wxMilliSleep(50);
	}

	m_sessionUpdaterLoop = false;
	return m_sessionCreated;
}

void ibApplicationData::ibApplicationDataSessionUpdater::StartSessionUpdater()
{
	m_sessionStarted = true;
}

const ibApplicationDataSessionArray ibApplicationData::ibApplicationDataSessionUpdater::GetSessionArray() const
{
	wxCriticalSectionLocker enter(ms_sessionLocker);
	return m_sessionArray;
}

bool ibApplicationData::ibApplicationDataSessionUpdater::VerifySessionUpdater() const
{
	if (appData->GetAppMode() == eDESIGNER_MODE) {
		for (unsigned int idx = 0; idx < m_sessionArray.GetSessionCount(); idx++) {
			if (eDESIGNER_MODE == m_sessionArray.GetSessionApplication(idx)) {
				try {
					ibBackendCoreException::Error(
						_("Another designer process is already running:\n%s, %s, %s"),
						m_sessionArray.GetStartedDate(idx),
						m_sessionArray.GetComputerName(idx),
						m_sessionArray.GetUserName(idx)
					);
				}
				catch (...) {
				}
				return false;
			}
		}
	}

	return true;
}

ibApplicationData::ibApplicationDataSessionUpdater::~ibApplicationDataSessionUpdater()
{
	if (m_session_db != nullptr) m_session_db->Close();
}

///////////////////////////////////////////////////////////////////////////////

wxThread::ExitCode ibApplicationData::ibApplicationDataSessionUpdater::Entry()
{
	//�lear lost session 
	ClearLostSessionUpdater();

	Job_UpdateActiveSession();

	//verify session updater
	if (!VerifySessionUpdater())
		return (wxThread::ExitCode)1;

	//start empty session 
	ibPreparedStatement* dbSessionPrepareData = m_session_db->PrepareStatement(
		wxT("INSERT INTO %s (session, userName, application, started, lastActive, computer) VALUES (?,?,?,?,?,?);"),
		session_table
	);

	if (dbSessionPrepareData == nullptr)
		return (wxThread::ExitCode)1;

	dbSessionPrepareData->SetParamString(1, m_session.str());
	dbSessionPrepareData->SetParamString(2, wxEmptyString); //empty user!
	dbSessionPrepareData->SetParamInt(3, appData->GetAppMode());
	dbSessionPrepareData->SetParamDate(4, appData->GetStartedDate());
	dbSessionPrepareData->SetParamDate(5, wxDateTime::Now());
	dbSessionPrepareData->SetParamString(6, appData->GetComputerName());

	dbSessionPrepareData->RunQuery();
	dbSessionPrepareData->Close();

	m_sessionArray.AppendSession(
		appData->GetAppMode(), appData->GetStartedDate(), appData->GetUserName(), appData->GetComputerName(), m_session.str()
	);

	if (dbSessionPrepareData != nullptr)
		dbSessionPrepareData->Close();

	m_session_db->CloseStatement(dbSessionPrepareData);
	m_sessionCreated = true;

	bool lastSessionStarted = m_sessionStarted;
	while (!TestDestroy()) {

		m_currentDateTime = wxDateTime::UNow();

		Job_ClearLostSession();
		Job_CalcActiveSession();
		Job_UpdateActiveSession();

		const wxTimeSpan& job_allTotal_done = wxDateTime::UNow() - m_currentDateTime;

		for (unsigned int milliseconds =
			job_allTotal_done.GetMilliseconds().GetValue();
			milliseconds < 3000;
			milliseconds += 250)
		{
			if (m_sessionStarted != lastSessionStarted)
				break;

			if (TestDestroy())
				break;

			wxMilliSleep(250);
		}

		lastSessionStarted = m_sessionStarted;
	};

	const int result = m_session_db->RunQuery(
		wxT("DELETE FROM %s WHERE session = '%s';"),
		session_table,
		m_session.str()
	);

	m_sessionArray.ClearSession();
	m_sessionCreated = m_sessionStarted = false;

	if (result != DATABASE_LAYER_QUERY_RESULT_ERROR)
		return (wxThread::ExitCode)1;

	return (wxThread::ExitCode)0;
}

///////////////////////////////////////////////////////////////////////////////
//								ibApplicationData
///////////////////////////////////////////////////////////////////////////////

bool ibApplicationData::TableAlreadyCreated()
{
	return db_query->TableExists(user_table) &&
		db_query->TableExists(session_table);
}

///////////////////////////////////////////////////////////////////////////////

void ibApplicationData::CreateTableUser()
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

void ibApplicationData::CreateTableSession()
{
	if (!db_query->TableExists(session_table)) {

		db_query->RunQuery(wxT("create table %s ("
			"session              VARCHAR(36) NOT NULL PRIMARY KEY,"
			"userName             VARCHAR(64) NOT NULL,"
			"application	   INTEGER  NOT NULL,"
			"started		   TIMESTAMP  NOT NULL,"
			"lastActive		   TIMESTAMP  NOT NULL,"
			"computer          VARCHAR(128) NOT NULL);"),
			session_table
		);

		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD) {

			db_query->RunQuery(
				wxT("create index if not exists session_index_1 on %s (session, userName);"),
				session_table
			);

			db_query->RunQuery(
				wxT("create index if not exists session_index_2 on %s (session);"),
				session_table
			);

			db_query->RunQuery(
				wxT("create index if not exists session_index_3 on %s (lastActive);"),
				session_table
			);
		}
		else
		{
			db_query->RunQuery(
				wxT("create index session_index_1 on %s (session, userName);"),
				session_table
			);

			db_query->RunQuery(
				wxT("create index session_index_2 on %s (session);"),
				session_table
			);

			db_query->RunQuery(
				wxT("create index session_index_3 on %s (lastActive);"),
				session_table
			);
		}
	}
}

void ibApplicationData::CreateTableEvent()
{
	if (!db_query->TableExists(event_table)) {
	}
}

bool ibApplicationData::ClearTableUser()
{
	if (!db_query->TableExists(user_table))
		return false;

	db_query->RunQuery(wxT("DELETE FROM %s;"), user_table);
	return true;
}

///////////////////////////////////////////////////////////////////////////////

enum
{
	eBlockPswd = 0x0234530,
	eBlockRole = 0x0234540,
	eBlockLang = 0x0234550,
};

#include "fileSystem/fs.h"

ibApplicationDataUserInfo ibApplicationData::ReadUserData(const ibGuid& userGuid) const
{
	ibApplicationDataUserInfo userInfo;
	if (!userGuid.isValid())
		return userInfo;

	ibDatabaseResultSet* dbUserResult = db_query->RunQueryWithResults(
		wxT("SELECT * FROM %s WHERE guid = '%s';"), user_table, userGuid.str());

	if (dbUserResult != nullptr && dbUserResult->Next()) {

		userInfo.m_strUserGuid = dbUserResult->GetResultString(wxT("guid"));
		userInfo.m_strUserName = dbUserResult->GetResultString(wxT("name"));
		userInfo.m_strUserFullName = dbUserResult->GetResultString(wxT("fullName"));

		wxMemoryBuffer buffer;
		dbUserResult->GetResultBlob(wxT("binaryData"), buffer);
		ibReaderMemory userReader(buffer);

		wxMemoryBuffer bufferPassword;
		if (userReader.r_chunk(eBlockPswd, bufferPassword)) {
			ReadUserData_Password(bufferPassword, userInfo);
		}

		wxMemoryBuffer bufferRole;
		if (userReader.r_chunk(eBlockRole, bufferRole)) {
			ReadUserData_Role(bufferRole, userInfo);
		}

		wxMemoryBuffer bufferLang;
		if (userReader.r_chunk(eBlockLang, bufferLang)) {
			ReadUserData_Language(bufferLang, userInfo);
		}

		if (dbUserResult != nullptr)
			dbUserResult->Close();

		db_query->CloseResultSet(dbUserResult);
	}

	return userInfo;
}

ibApplicationDataUserInfo ibApplicationData::ReadUserData(const wxString& strUserName) const
{
	ibApplicationDataUserInfo userInfo;
	if (strUserName.IsEmpty())
		return userInfo;

	ibDatabaseResultSet* dbUserResult = db_query->RunQueryWithResults(
		wxT("SELECT * FROM %s WHERE name = '%s';"), user_table, strUserName);

	if (dbUserResult != nullptr && dbUserResult->Next()) {

		userInfo.m_strUserGuid = dbUserResult->GetResultString(wxT("guid"));

		userInfo.m_strUserName = dbUserResult->GetResultString(wxT("name"));
		userInfo.m_strUserFullName = dbUserResult->GetResultString(wxT("fullName"));

		wxMemoryBuffer buffer;
		dbUserResult->GetResultBlob(wxT("binaryData"), buffer);
		ibReaderMemory userReader(buffer);

		wxMemoryBuffer bufferPassword;
		if (userReader.r_chunk(eBlockPswd, bufferPassword)) {
			ReadUserData_Password(bufferPassword, userInfo);
		}

		wxMemoryBuffer bufferRole;
		if (userReader.r_chunk(eBlockRole, bufferRole)) {
			ReadUserData_Role(bufferRole, userInfo);
		}

		wxMemoryBuffer bufferLang;
		if (userReader.r_chunk(eBlockLang, bufferLang)) {
			ReadUserData_Language(bufferLang, userInfo);
		}

		if (dbUserResult != nullptr)
			dbUserResult->Close();

		db_query->CloseResultSet(dbUserResult);
	}

	return userInfo;
}

bool ibApplicationData::SaveUserData(const ibApplicationDataUserInfo& userInfo) const
{
	ibPreparedStatement* dbUserPrepareData = nullptr;
	if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
		dbUserPrepareData = db_query->PrepareStatement(
			wxT("INSERT INTO %s (guid, name, fullName, changed, dataSize, binaryData) VALUES(?, ?, ?, ?, ?, ?) ON CONFLICT (guid) DO UPDATE SET guid = excluded.guid, name = excluded.name, fullName = excluded.fullName, changed = excluded.changed, dataSize = excluded.dataSize, binaryData = excluded.binaryData; "), user_table);
	else
		dbUserPrepareData = db_query->PrepareStatement(
			wxT("UPDATE OR INSERT INTO %s (guid, name, fullName, changed, dataSize, binaryData) VALUES(?, ?, ?, ?, ?, ?) MATCHING (guid);"), user_table);

	if (dbUserPrepareData == nullptr)
		return false;

	dbUserPrepareData->SetParamString(1, userInfo.m_strUserGuid);
	dbUserPrepareData->SetParamString(2, userInfo.m_strUserName);
	dbUserPrepareData->SetParamString(3, userInfo.m_strUserFullName);
	dbUserPrepareData->SetParamDate(4, wxDateTime::Now());

	ibWriterMemory writer;

	writer.w_chunk(eBlockPswd, SaveUserData_Password(userInfo));
	writer.w_chunk(eBlockRole, SaveUserData_Role(userInfo));
	writer.w_chunk(eBlockLang, SaveUserData_Language(userInfo));

	dbUserPrepareData->SetParamNumber(5, writer.size());
	dbUserPrepareData->SetParamBlob(6, writer.pointer(), writer.size());

	const int result = dbUserPrepareData->RunQuery();

	if (dbUserPrepareData != nullptr)
		dbUserPrepareData->Close();

	db_query->CloseStatement(dbUserPrepareData);
	return result != DATABASE_LAYER_QUERY_RESULT_ERROR;
}

///////////////////////////////////////////////////////////////////////////////

bool ibApplicationData::LoadUserInfoFromBuffer(wxMemoryBuffer& buffer)
{
	ibReaderMemory reader = buffer;

	ibReaderMemory* prevReaderMemory = nullptr; u64 meta_id = 0;

	while (!reader.eof()) {

		ibReaderMemory* readerMemory = reader.open_chunk_iterator(meta_id, &*prevReaderMemory);
		if (!readerMemory)
			break;

		ibApplicationDataUserInfo userInfo;

		userInfo.m_strUserGuid = readerMemory->r_stringZ();
		userInfo.m_strUserName = readerMemory->r_stringZ();
		userInfo.m_strUserFullName = readerMemory->r_stringZ();

		wxMemoryBuffer bufferPassword;
		if (readerMemory->r_chunk(eBlockPswd, bufferPassword)) {
			ReadUserData_Password(bufferPassword, userInfo);
		}

		wxMemoryBuffer bufferRole;
		if (readerMemory->r_chunk(eBlockRole, bufferRole)) {
			ReadUserData_Role(bufferRole, userInfo);
		}

		wxMemoryBuffer bufferLang;
		if (readerMemory->r_chunk(eBlockLang, bufferLang)) {
			ReadUserData_Language(bufferLang, userInfo);
		}

		SaveUserData(userInfo);

		prevReaderMemory = readerMemory;
	};

	return true;
}

bool ibApplicationData::SaveUserInfoToBuffer(wxMemoryBuffer& buffer) const
{
	ibDatabaseResultSet* dbUserResult = db_query->RunQueryWithResults(wxT("SELECT guid FROM %s;"), user_table);

	if (dbUserResult == nullptr)
		return false;

	ibWriterMemory writer; unsigned int idx = 0;

	while (dbUserResult->Next()) {

		const ibApplicationDataUserInfo& userInfo = ReadUserData(ibGuid(dbUserResult->GetResultString(wxT("guid"))));

		ibWriterMemory userWriter;

		userWriter.w_stringZ(userInfo.m_strUserGuid);
		userWriter.w_stringZ(userInfo.m_strUserName);
		userWriter.w_stringZ(userInfo.m_strUserFullName);

		userWriter.w_chunk(eBlockPswd, SaveUserData_Password(userInfo));
		userWriter.w_chunk(eBlockRole, SaveUserData_Role(userInfo));
		userWriter.w_chunk(eBlockLang, SaveUserData_Language(userInfo));

		writer.w_chunk(idx++, userWriter.buffer());
	}

	buffer = writer.buffer();

	if (dbUserResult != nullptr)
		dbUserResult->Close();

	db_query->CloseResultSet(dbUserResult);
	return true;
}

///////////////////////////////////////////////////////////////////////////////

bool ibApplicationData::HasAllowedUser() const
{
	ibDatabaseResultSet* dbUserResult = db_query->RunQueryWithResults(
		wxT("SELECT name FROM %s;"), user_table);

	if (dbUserResult == nullptr) return false;
	bool hasUsers = false;
	if (dbUserResult->Next()) hasUsers = true;

	if (dbUserResult != nullptr)
		dbUserResult->Close();

	db_query->CloseResultSet(dbUserResult);
	return hasUsers;
}

std::vector<ibApplicationDataShortUserInfo> ibApplicationData::GetAllowedUser() const
{
	ibDatabaseResultSet* dbUserResult = db_query->RunQueryWithResults(
		wxT("SELECT guid, name, fullName FROM %s;"), user_table);

	std::vector<ibApplicationDataShortUserInfo> userInfo;

	if (dbUserResult == nullptr)
		return userInfo;

	while (dbUserResult->Next()) {

		ibApplicationDataShortUserInfo entry;
		entry.m_strUserGuid = dbUserResult->GetResultString(wxT("guid"));
		entry.m_strUserName = dbUserResult->GetResultString(wxT("name"));
		entry.m_strUserFullName = dbUserResult->GetResultString(wxT("fullName"));
		userInfo.push_back(entry);
	}

	if (dbUserResult != nullptr)
		dbUserResult->Close();

	db_query->CloseResultSet(dbUserResult);
	return userInfo;
}

bool ibApplicationData::StartSession(const wxString& strUserName, const wxString& strUserPassword)
{
	if (!CloseSession())
		return false;

	m_sessionUpdater = std::shared_ptr<ibApplicationDataSessionUpdater>(
		new ibApplicationDataSessionUpdater(this, m_sessionGuid)
	);

	bool succes = true;

	if (!m_sessionUpdater->InitSessionUpdater())
		return false;

	if (!AuthenticationAndSetUser(strUserName, strUserPassword)) {

		succes = false;

		if (backend_mainFrame != nullptr &&
			backend_mainFrame->AuthenticationUser(strUserName, strUserPassword)) {
			succes = true;
		}
	}

	if (succes)
		m_sessionUpdater->StartSessionUpdater();
	else if (!succes && !CloseSession())
		return false;

	m_connected_to_db = succes;
	return succes;
}

bool ibApplicationData::CloseSession()
{
	if (m_sessionUpdater != nullptr) {

		if (m_sessionUpdater->IsRunning() &&
			m_sessionUpdater->Delete() != wxTHREAD_NO_ERROR)
			return false;

		m_sessionUpdater = nullptr;
	}

	m_connected_to_db = false;
	return true;
}