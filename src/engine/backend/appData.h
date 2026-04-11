#ifndef __APP_DATA_H__
#define __APP_DATA_H__

#include "backend/backend_core.h"

#define appData				(ibApplicationData::Get())

#define appDataCreateFile(mode,database,locale) (ibApplicationData::CreateFileAppDataEnv(mode,database,locale))
#define appDataCreateServer(mode,server,port,user,password,database,locale) (ibApplicationData::CreateServerAppDataEnv(mode,server,port,user,password,database,locale))

#define appDataDestroy()	(ibApplicationData::DestroyAppDataEnv())

#define db_query  (ibApplicationData::GetDatabaseLayer())

enum ibRunMode {
	eLAUNCHER_MODE = 1,		// for create db, only backmode  
	eDESIGNER_MODE = 2,		// backmode + frontmode
	eENTERPRISE_MODE = 3,	// backmode + frontmode
	eSERVICE_MODE = 4		// only backmode 
};

enum ibDatabaseMode {
	eFILE,
	eSERVER,

	eNONE = 1000
};

//////////////////////////////////////////////////////////////////
#define _app_start_default_flag 0x0000
#define _app_start_create_debug_server_flag 0x0080
//////////////////////////////////////////////////////////////////

class BACKEND_API ibDatabaseLayer;

#pragma region session  
class BACKEND_API ibApplicationDataSessionArray {

	struct ibApplicationDataSessionUnit {

		ibApplicationDataSessionUnit(ibRunMode runMode, const wxDateTime& startedDateTime,
			const wxString strUserName, const wxString strComputerName, const wxString& strSession) :
			m_runMode(runMode), m_startedDate(startedDateTime), m_strUserName(strUserName), m_strComputerName(strComputerName), m_strSession(strSession)
		{
		}

		ibRunMode m_runMode;
		wxDateTime m_startedDate;
		wxString m_strUserName;
		wxString m_strComputerName;
		wxString m_strSession;
	};

public:

	ibApplicationDataSessionArray() : m_sessionArrayHash(wxNewUniqueGuid) {}

	void AppendSession(ibRunMode runMode, const wxDateTime& startedTime,
		const wxString strUserName, const wxString strComputerName, const wxString& strSession) {
		m_listSession.emplace_back(runMode, startedTime, strUserName, strComputerName, strSession);
	}

	wxString GetSessionArrayHash() const { return wxString(m_sessionArrayHash.str()); }

	wxString GetUserName(unsigned int idx) const;
	wxString GetComputerName(unsigned int idx) const;
	wxString GetSession(unsigned int idx) const;
	wxString GetStartedDate(unsigned int idx) const;
	wxString GetApplication(unsigned int idx) const;

	void ClearSession() {
		m_sessionArrayHash = wxNewUniqueGuid;
		m_listSession.clear();
	}

	ibRunMode GetSessionApplication(unsigned int idx) const;
	unsigned int const GetSessionCount() const { return m_listSession.size(); }

private:
	ibGuid m_sessionArrayHash;
	std::vector<ibApplicationDataSessionUnit> m_listSession;
};
#pragma endregion

#pragma region config
struct ibApplicationDataConfigInfo {

	bool IsSetLocale() const { return !m_strLocale.IsEmpty(); }

	//Locale info 
	wxString m_strLocale;
};
#pragma endregion 

#pragma region user
struct ibApplicationDataShortUserInfo {
	wxString m_strUserGuid;
	wxString m_strUserName;
	wxString m_strUserFullName;
};

struct ibApplicationDataUserInfo {

	struct ibApplicationDataUserRole {
		wxString m_strRoleGuid;
		wxString m_strRoleName;
		ibRoleID m_miRoleId = wxNOT_FOUND;
	};

	bool IsOk() const { return !m_strUserGuid.IsEmpty(); }

	bool IsSetPassword() const { return !m_strUserName.IsEmpty() && !m_strUserPassword.IsEmpty(); }
	bool IsSetRole() const { return m_roleArray.size() > 0; }
	bool IsSetLanguage() const { return !m_strLanguageGuid.IsEmpty() && !m_strLanguageCode.IsEmpty(); }

	//User info 
	wxString m_strUserGuid;
	wxString m_strUserName;
	wxString m_strUserFullName;
	wxString m_strUserPassword;

	//Role info 
	std::vector<ibApplicationDataUserRole> m_roleArray;

	//Language info 
	wxString m_strLanguageGuid;
	wxString m_strLanguageName;
	wxString m_strLanguageCode;
};
#pragma endregion

// This class is a singleton class.
class BACKEND_API ibApplicationData {

#pragma region session  
	class ibApplicationDataSessionUpdater : public wxThread {
	public:
		ibApplicationDataSessionUpdater(ibApplicationData* application, const ibGuid& session);
		virtual ~ibApplicationDataSessionUpdater();
		bool InitSessionUpdater();
		void StartSessionUpdater();
		const ibApplicationDataSessionArray GetSessionArray() const;
	protected:

		virtual ExitCode Entry();

		// This one is called by Delete() before actually deleting the thread and
		// is executed in the context of the thread that called Delete().
		virtual void OnDelete() { m_sessionUpdaterLoop = false; }

		// This one is called by Kill() before killing the thread and is executed
		// in the context of the thread that called Kill().
		virtual void OnKill() { m_sessionUpdaterLoop = false; }

		// called when the thread exits - in the context of this thread
		//
		// NB: this function will not be called if the thread is Kill()ed
		virtual void OnExit() { m_sessionUpdaterLoop = false; }

	private:

		void Job_ClearLostSession();
		void Job_CalcActiveSession();
		void Job_UpdateActiveSession();

		void ClearLostSessionUpdater();
		bool VerifySessionUpdater() const;

		bool m_sessionCreated, m_sessionStarted;
		bool m_sessionUpdaterLoop;

		ibApplicationDataSessionArray m_sessionArray;
		std::shared_ptr<ibDatabaseLayer> m_session_db;
		const ibGuid m_session;
		wxDateTime m_currentDateTime;
		static wxCriticalSection ms_sessionLocker;
	};
#pragma endregion

	ibApplicationData(ibRunMode runMode);

public:

	virtual ~ibApplicationData();
	static ibApplicationData* Get() { return s_instance; }

	///////////////////////////////////////////////////////////////////////////
	static bool CreateAppDataEnv(ibRunMode runMode = ibRunMode::eENTERPRISE_MODE);
	///////////////////////////////////////////////////////////////////////////

	static bool CreateFileAppDataEnv(ibRunMode runMode, const wxString& strDirDatabase, const wxString& strLocale = wxT(""));
	static bool CreateServerAppDataEnv(ibRunMode runMode, const wxString& strServer, const wxString& strPort,
		const wxString& strUser = wxT(""), const wxString& strPassword = wxT(""), const wxString& strDatabase = wxT(""), const wxString& strLocale = wxT(""));

	static bool SetLocaleAppDataEnv(const wxString& strLocale = wxT(""));
	static bool DestroyAppDataEnv();
	///////////////////////////////////////////////////////////////////////////

	// Initialize application
	bool InitLocale(const wxString& locale = wxT(""));

	bool Connect(const wxString& strUserName, const wxString& strUserPassword, const int flags = _app_start_default_flag);
	bool Disconnect();

#pragma region config
	// Read setting from file
	void ReadEngineConfig();
#pragma endregion

	static std::shared_ptr<ibDatabaseLayer> GetDatabaseLayer() {
		if (s_instance != nullptr)
			return s_instance->m_db;
		return nullptr;
	}

#pragma region execute 
	long RunApplication(const wxString& strAppName, bool searchDebug = true) const;
	long RunApplication(const wxString& strAppName, const wxString& strUserName, const wxString& strUserPassword, bool searchDebug = true) const;
#pragma endregion

	ibRunMode GetAppMode() const { return m_runMode; }

	bool LauncherMode() const { return m_runMode == ibRunMode::eLAUNCHER_MODE; }
	bool DesignerMode() const { return m_runMode == ibRunMode::eDESIGNER_MODE; }
	bool EnterpriseMode() const { return m_runMode == ibRunMode::eENTERPRISE_MODE; }
	bool ServiceMode() const { return m_runMode == ibRunMode::eSERVICE_MODE; }

	inline wxString GetRunModeDescr(const ibRunMode& mode) const {
		switch (mode)
		{
		case eLAUNCHER_MODE:
			return wxEmptyString;
		case eDESIGNER_MODE:
			return _("Designer");
		case eENTERPRISE_MODE:
			return _("Thick client");
		case eSERVICE_MODE:
			return _("Daemon");
		}
		return wxEmptyString;
	}

	inline wxString GetRunModeDescr() const { return GetRunModeDescr(m_runMode); }

	ibDatabaseMode GetDatabaseMode() const { return m_dbMode; }

	inline wxString GetDatabaseModeDescr(const ibDatabaseMode& mode) const {
		switch (mode)
		{
		case eNONE:
			return wxEmptyString;
		case eFILE:
			return _("File");
		case eSERVER:
			return _("Server");
		}
		return wxEmptyString;
	}

	inline wxString GetDatabaseModeDescr() const { return GetDatabaseModeDescr(m_dbMode); }

	bool AuthenticationAndSetUser(const wxString& strUserName, const wxString& strUserPassword);

	bool ExclusiveMode() const { return m_exclusiveMode; }

	const wxString& GetUserName() const { return m_userInfo.m_strUserName; }
	const wxString& GetUserPassword() const { return m_userInfo.m_strUserFullName; }
	wxString GetComputerName() const { return m_strComputer; }
	wxDateTime GetStartedDate() const { return m_startedDate; }

	wxString GetLocale() const { return m_locale.GetCanonicalName(); }

	std::vector<ibApplicationDataShortUserInfo> GetAllowedUser() const;

#pragma region session  

	const ibApplicationDataSessionArray GetSessionArray() const { return m_sessionUpdater->GetSessionArray(); }

#pragma endregion 

	const std::vector<ibApplicationDataUserInfo::ibApplicationDataUserRole>& GetUserRoleArray() const { return m_userInfo.m_roleArray; }

#pragma region language  

	ibGuid GetUserLanguageGuid() const { return m_userInfo.m_strLanguageGuid; }
	wxString GetUserLanguageCode() const { return m_userInfo.m_strLanguageCode; }

#pragma endregion 

	static bool IsForceExit() {
		wxCriticalSectionLocker enter(m_cs_force_exit);
		return m_forceExit;
	}

	static void ForceExit() {
		wxCriticalSectionLocker enter(m_cs_force_exit);
		if (!m_forceExit) {
			wxTheApp->CallAfter(
				[]() {
					wxTheApp->Exit();
				}
			);
			m_forceExit = true;
		}
	}

#pragma region user  
	ibApplicationDataUserInfo ReadUserData(const ibGuid& userGuid) const;
	ibApplicationDataUserInfo ReadUserData(const wxString& userName) const;
	bool SaveUserData(const ibApplicationDataUserInfo& userInfo) const;
#pragma endregion

#pragma region database

	bool LoadDatabase(const wxString& strFullPath);
	bool SaveDatabase(const wxString& strFullPath);

	bool ClearDatabase();

#pragma endregion 

	wxString GetDatabaseDescription();

private:

	bool HasAllowedUser() const;
	bool StartSession(const wxString& userName, const wxString& md5Password);
	bool CloseSession();

	void ReadUserData_Password(const wxMemoryBuffer& buffer, ibApplicationDataUserInfo& userInfo) const;
	void ReadUserData_Role(const wxMemoryBuffer& buffer, ibApplicationDataUserInfo& userInfo) const;
	void ReadUserData_Language(const wxMemoryBuffer& buffer, ibApplicationDataUserInfo& userInfo) const;

	wxMemoryBuffer SaveUserData_Password(const ibApplicationDataUserInfo& userInfo) const;
	wxMemoryBuffer SaveUserData_Role(const ibApplicationDataUserInfo& userInfo) const;
	wxMemoryBuffer SaveUserData_Language(const ibApplicationDataUserInfo& userInfo) const;

	bool LoadUserInfoFromBuffer(wxMemoryBuffer& buffer);
	bool SaveUserInfoToBuffer(wxMemoryBuffer& buffer) const;

	wxString ComputeMd5() const { return ComputeMd5(m_userInfo.m_strUserPassword); }
	wxString ComputeMd5(const wxString& userPassword) const;

	static bool TableAlreadyCreated();
	static void CreateTableUser();
	static void CreateTableSession();
	static void CreateTableEvent();

	static bool ClearTableUser();

private:

	static ibApplicationData* s_instance;

	ibRunMode m_runMode;
	wxString m_strComputer;
	wxDateTime m_startedDate;
	wxDateTime m_lastActivity;
	ibGuid m_sessionGuid;

#pragma region config
	ibApplicationDataConfigInfo m_configInfo;
#pragma endregion 
#pragma region user
	ibApplicationDataUserInfo m_userInfo;
#pragma endregion 

	static bool m_forceExit;
	static wxCriticalSection m_cs_force_exit;

	std::shared_ptr<ibDatabaseLayer> m_db;
	std::shared_ptr<ibApplicationDataSessionUpdater> m_sessionUpdater;

	bool m_connected_to_db = false;
	bool m_created_metadata = false;
	bool m_run_metadata = false;

	ibDatabaseMode m_dbMode;

	// FILE ENTRY
	wxString m_strFile;

	// SERVER ENTRY
	wxString m_strServer;
	wxString m_strPort;
	wxString m_strDatabase;

	wxString m_strUser;
	wxString m_strPassword;

	// LOCALE
	wxLocale m_locale;
	int m_locale_lang;

	bool m_exclusiveMode = false; //Exclusive mode
};

///////////////////////////////////////////////////////////////////////////////
#define user_table		wxT("sys_user")
#define session_table	wxT("sys_session")
#define sequence_table	wxT("sys_sequence")
#define event_table		wxT("sys_event")
///////////////////////////////////////////////////////////////////////////////

#endif
