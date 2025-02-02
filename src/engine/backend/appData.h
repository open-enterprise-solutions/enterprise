#ifndef __APP_DATA_H__
#define __APP_DATA_H__

#include "backend/backend_core.h"

#define appData				(CApplicationData::Get())

#define appDataCreate(mode,server,port,user,password,database) (CApplicationData::CreateAppDataEnv(mode,server,port,user,password,database))
#define appDataDestroy()	(CApplicationData::DestroyAppDataEnv())

#define db_query  (CApplicationData::GetDatabaseLayer())

enum eRunMode {
	eLAUNCHER_MODE = 1,		// for create db, only backmode  
	eDESIGNER_MODE = 2,		// backmode + frontmode
	eENTERPRISE_MODE = 3,	// backmode + frontmode
	eSERVICE_MODE = 4		// only backmode 
};

//////////////////////////////////////////////////////////////////
#define _app_start_default_flag 0x0000
#define _app_start_create_debug_server_flag 0x0080
//////////////////////////////////////////////////////////////////

class IDatabaseLayer;

// This class is a singleton class.
class BACKEND_API CApplicationData {

	class CApplicationDataSessionThread : public wxThread {
	public:
		CApplicationDataSessionThread() : wxThread(wxTHREAD_JOINABLE) {}

	protected:
		virtual ExitCode Entry();
	};

	// hiden constructor
	CApplicationData(eRunMode runMode);
public:

	virtual ~CApplicationData();
	static CApplicationData* Get() { return s_instance; }

	///////////////////////////////////////////////////////////////////////////
	static bool CreateAppDataEnv(eRunMode runMode, const wxString& strServer = _(""), const wxString& strPort = _(""),
		const wxString& strUser = _(""), const wxString& strPassword = _(""), const wxString& strDatabase = _(""));
	static bool DestroyAppDataEnv();
	///////////////////////////////////////////////////////////////////////////

	// Initialize application
	bool Connect(const wxString& user, const wxString& password, const int flags = _app_start_default_flag);
	bool Disconnect();

	static std::shared_ptr<IDatabaseLayer> GetDatabaseLayer() {
		if (s_instance != nullptr)
			return s_instance->m_db;
		return nullptr;
	}

#pragma region execute 
	long RunApplication(const wxString& strAppName, bool searchDebug = true) const;
	long RunApplication(const wxString& strAppName, const wxString& user, const wxString& password, bool searchDebug = true) const;
#pragma endregion

	eRunMode GetAppMode() const { return m_runMode; }

	bool DesignerMode() const { return m_runMode == eRunMode::eDESIGNER_MODE; }
	bool EnterpriseMode() const { return m_runMode == eRunMode::eENTERPRISE_MODE; }
	bool ServiceMode() const { return m_runMode == eRunMode::eSERVICE_MODE; }

	inline wxString GetModeDescr(const eRunMode& mode) const {
		switch (mode)
		{
		case eDESIGNER_MODE:
			return _("Designer");
		case eENTERPRISE_MODE:
			return _("Thick client");
		case eSERVICE_MODE:
			return _("Deamon");
		}
		return wxEmptyString;
	}

	bool AuthenticationAndSetUser(const wxString& userName, const wxString& md5Password);

	bool ExclusiveMode() const { return m_exclusiveMode; }
	const wxString& GetUserName() const { return m_strUserIB; }
	const wxString& GetUserPassword() const { return m_strPasswordIB; }
	wxString GetComputerName() const { return m_strComputer; }
	wxArrayString GetAllowedUser() const;

private:
	bool HasAllowedUser() const;
	void RefreshActiveUser();
	bool AuthenticationUser(const wxString& userName, const wxString& md5Password) const;
	bool StartSession(const wxString& userName, const wxString& md5Password);
	bool CloseSession();
private:
	wxString ComputeMd5() const { return ComputeMd5(m_strPasswordIB); }
	wxString ComputeMd5(const wxString& userPassword) const;
private:
	static bool TableAlreadyCreated();
	static void CreateTableUser();
	static void CreateTableSession();
	static void CreateTableEvent();
private:
	static CApplicationData* s_instance;

	eRunMode m_runMode;
	wxString m_strUserIB, m_strPasswordIB;
	wxString m_strComputer;
	wxDateTime m_startedDate;
	wxDateTime m_lastActivity;
	Guid m_sessionGuid;
	std::shared_ptr<IDatabaseLayer> m_db;
	std::shared_ptr<wxThread> m_sessionThread;

	bool m_connected_to_db = false;

	// SERVER ENTRY
	wxString m_strServer;
	wxString m_strPort;
	wxString m_strDatabase;
	wxString m_strUser;
	wxString m_strPassword;

	bool m_exclusiveMode; //Монопольный режим
};

///////////////////////////////////////////////////////////////////////////////
#define user_table		wxT("_user")
#define session_table	wxT("_session")
#define event_table		wxT("_event")
///////////////////////////////////////////////////////////////////////////////

#endif
