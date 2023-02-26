#ifndef _APPDATA_H__
#define _APPDATA_H__

#include <set>

#include "core/core.h"
#include "core/common/formdefs.h"
#include "core/common/types.h"

#include <3rdparty/guid/guid.h>

#include <wx/stc/stc.h>
#include <wx/thread.h>

#define appData				(ApplicationData::Get())
#define appDataDestroy()	(ApplicationData::Destroy())

#define databaseLayer		(ApplicationData::GetObjectDatabase())

enum eRunMode {
	eDESIGNER_MODE = 1,
	eENTERPRISE_MODE = 2,
	eSERVICE_MODE = 3
};

enum eDBMode {
	eFirebird,
	ePostgres
};

class DatabaseLayer;

// This class is a singleton class.
class CORE_API ApplicationData {

	eRunMode m_runMode;

	wxString m_projectPath;
	wxString m_projectDir;
	wxString m_userPath; //каталог пользователя для записи настроек

	Guid m_sessionGuid;

	wxString m_userName;
	wxString m_userPassword;
	wxString m_computerName;

	wxDateTime m_startedDate;
	wxDateTime m_lastActivity;

	DatabaseLayer* m_objDb;  // Base de datos de objetos
	wxTimer* m_sessionTimer;

	wxCriticalSection m_sessionLocker;

private:

	bool m_bSingleMode; //Монопольный режим
	static ApplicationData* s_instance;

	// hiden constructor
	ApplicationData(DatabaseLayer* db, eRunMode runMode);
	void RefreshActiveUsers();

public:

	~ApplicationData();

	static bool CreateAppData(eDBMode dbMode, eRunMode runMode, const wxString& strDatabase); //for file db 
	static bool CreateAppData(eDBMode dbMode, eRunMode runMode, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	static bool CreateAppData(eDBMode dbMode, eRunMode runMode, const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	static bool CreateAppData(eDBMode dbMode, eRunMode runMode, const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword, const wxString& strRole);

	static ApplicationData* Get() {
		return s_instance;
	}

	// Force the static appData instance to Init()
	static bool Initialize(const wxString& user, const wxString& password);
	static void Destroy();

	// Initialize application
	bool Connect(const wxString& user, const wxString& password);
	bool Disconnect();

	bool SaveConfiguration();

	static DatabaseLayer* GetObjectDatabase() {
		if (s_instance != NULL)
			return s_instance->m_objDb;
		return NULL;
	}

	eRunMode GetAppMode() const {
		return m_runMode;
	}

	bool DesignerMode() const {
		return m_runMode == eRunMode::eDESIGNER_MODE;
	}

	bool EnterpriseMode() const {
		return m_runMode == eRunMode::eENTERPRISE_MODE;
	}

	bool ServiceMode() const {
		return m_runMode == eRunMode::eSERVICE_MODE;
	}

	wxString GetModeDescr(const eRunMode& mode) const {
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

	bool AlwaysShowWndows() const {
		return DesignerMode() ||
			EnterpriseMode();
	}

	bool SingleMode() const {
		return m_bSingleMode;
	}

	const wxString& GetUserName() const {
		return m_userName;
	};

	const wxString& GetUserPassword() const {
		return m_userPassword;
	};

	wxString GetComputerName() const {
		return m_computerName;
	}

	const wxString& GetProjectPath() const {  /** Path to the mtd file that is opened. */
		return m_projectPath;
	}

	const wxString& GetApplicationPath() const {
		return m_projectDir;
	};

	wxArrayString GetAllowedUsers() const;

	wxString ComputeMd5() const;
	wxString ComputeMd5(const wxString& userPassword) const;

	bool HasAllowedUsers() const;
	bool CheckLoginAndPassword(const wxString& userName, const wxString& md5Password) const;
	bool ShowAuthorizationWnd();

	bool StartSession(const wxString& userName, const wxString& md5Password);
	bool CloseSession();

protected:
	void OnIdleHandler(wxTimerEvent& event);
};


#endif
