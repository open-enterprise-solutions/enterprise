////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuider
//	Description : app info
////////////////////////////////////////////////////////////////////////////

#include "backend/appData.h"
//databases
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#include "backend/databaseLayer/postgres/postgresDatabaseLayer.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
//sandbox
#include "metadataConfiguration.h"

///////////////////////////////////////////////////////////////////////////////
//								CApplicationData
///////////////////////////////////////////////////////////////////////////////

CApplicationData* CApplicationData::s_instance = nullptr;

///////////////////////////////////////////////////////////////////////////////

CApplicationData::CApplicationData(eRunMode runMode) :
	m_db(nullptr), m_runMode(runMode),
	m_sessionGuid(wxNewUniqueGuid),
	m_startedDate(wxDateTime::Now()),
	m_strComputer(wxGetHostName()),
	m_sessionThread(new CApplicationDataSessionThread())
{
}

CApplicationData::~CApplicationData()
{
}

///////////////////////////////////////////////////////////////////////////////

bool CApplicationData::CreateAppDataEnv(eRunMode runMode, const wxString& strServer, const wxString& strPort,
	const wxString& strUser, const wxString& strPassword, const wxString& strDatabase)
{
	if (s_instance != nullptr) s_instance->DestroyAppDataEnv();
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try {
#endif
		std::shared_ptr<CPostgresDatabaseLayer> db(new CPostgresDatabaseLayer());
		if (db->Open(strServer, strPort, strDatabase, strUser, strPassword)) {

			s_instance = new CApplicationData(runMode);

			s_instance->m_strServer = strServer;
			s_instance->m_strPort = strPort;
			s_instance->m_strUser = strUser;
			s_instance->m_strPassword = strPassword;
			s_instance->m_strDatabase = strDatabase;

			s_instance->m_db = db;
			s_instance->m_exclusiveMode = runMode == eRunMode::eDESIGNER_MODE;

			if (runMode == eRunMode::eDESIGNER_MODE && !CApplicationData::TableAlreadyCreated()) {
				CApplicationData::CreateTableSession();
				CApplicationData::CreateTableUser();
				CApplicationData::CreateTableEvent();
			}
			else if (!CApplicationData::TableAlreadyCreated()) {
				s_instance->DestroyAppDataEnv();
				return false;
			}

			return true;
		}
		return false;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (const DatabaseLayerException* err) {
		return false;
	}
#endif
	return false;
}

bool CApplicationData::DestroyAppDataEnv()
{
	if (s_instance != nullptr && s_instance->m_db != nullptr) {

		bool result = false;
		if (s_instance->m_connected_to_db)
			result = s_instance->Disconnect();
		if (!result) return false;

		s_instance->m_strServer = wxEmptyString;
		s_instance->m_strPort = wxEmptyString;
		s_instance->m_strUser = wxEmptyString;
		s_instance->m_strPassword = wxEmptyString;
		s_instance->m_strDatabase = wxEmptyString;

		if (s_instance->m_db->IsOpen())
			s_instance->m_db->Close();

		s_instance->m_db = nullptr;

		wxDELETE(s_instance);
		return true;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////////

bool CApplicationData::Connect(const wxString& user, const wxString& password, const int flags)
{
	if (m_connected_to_db)
		return false;
	if (!metaDataCreate(m_runMode, flags))
		return false;
	if (!StartSession(user, password)) return false;  //start session	
	m_connected_to_db = true;
	return commonMetaData->RunConfiguration();
}

bool CApplicationData::Disconnect()
{
	if (!m_connected_to_db)
		return false;
	const bool isConfigOpen = commonMetaData->IsConfigOpen();
	if (!CloseSession())
		return false;
	if (isConfigOpen && !commonMetaData->CloseConfiguration(forceCloseFlag))
		return false;
	metaDataDestroy();
	m_connected_to_db = false;
	return true;
}

///////////////////////////////////////////////////////////////////////////////
#pragma region execute 
long CApplicationData::RunApplication(const wxString& strAppName, bool searchDebug) const
{
	wxString executeCmd = strAppName + " ";

	if (!m_strServer.IsEmpty())
		executeCmd += " /srv " + m_strServer;
	if (!m_strPort.IsEmpty())
		executeCmd += " /p " + m_strPort;
	if (!m_strDatabase.IsEmpty())
		executeCmd += " /db " + m_strDatabase;
	if (!m_strUser.IsEmpty())
		executeCmd += " /usr " + m_strUser;
	if (!m_strPassword.IsEmpty())
		executeCmd += " /pwd " + m_strPassword;
	if (searchDebug)
		executeCmd += " /debug";
	if (!m_strUserIB.IsEmpty())
		executeCmd += " /ib_usr " + m_strUserIB;
	if (!m_strPasswordIB.IsEmpty())
		executeCmd += " /ib_pwd " + m_strPasswordIB;

	return wxExecute(executeCmd);
}

long CApplicationData::RunApplication(const wxString& strAppName, const wxString& user, const wxString& password, bool searchDebug) const
{
	wxString executeCmd = strAppName + " ";

	if (!m_strServer.IsEmpty())
		executeCmd += " /srv " + m_strServer;
	if (!m_strPort.IsEmpty())
		executeCmd += " /p " + m_strPort;
	if (!m_strDatabase.IsEmpty())
		executeCmd += " /db " + m_strDatabase;
	if (!m_strUser.IsEmpty())
		executeCmd += " /usr " + m_strUser;
	if (!m_strPassword.IsEmpty())
		executeCmd += " /pwd " + m_strPassword;
	if (searchDebug)
		executeCmd += " /debug";
	if (!user.IsEmpty())
		executeCmd += " /ib_usr " + user;
	if (!password.IsEmpty())
		executeCmd += " /ib_pwd " + password;

	return wxExecute(executeCmd);
}
#pragma endregion
///////////////////////////////////////////////////////////////////////////////

#include <backend/utils/wxmd5.hpp>

wxString CApplicationData::ComputeMd5(const wxString& userPassword) const
{
	if (userPassword.Length() > 0)
		return wxMD5::ComputeMd5(userPassword);
	return wxEmptyString;
}

///////////////////////////////////////////////////////////////////////////////