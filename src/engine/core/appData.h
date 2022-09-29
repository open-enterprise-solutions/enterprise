#ifndef _APPDATA_H__
#define _APPDATA_H__

#include <set>

#include "core.h"

#include "common/formdefs.h"
#include "common/types.h"
#include "guid/guid.h"

#include <wx/stc/stc.h>
#include <wx/thread.h>

class CValue;
class CConfigMetadata;
class IMetaObject;
class CMethods;
class IModuleManager;
class CProcUnit;
class CMetadataTree;

struct CByteCode;

class DatabaseLayer;

#define appData         	(ApplicationData::Get())
#define appDataCreate(path) (ApplicationData::Get(path))
#define appDataDestroy()  	(ApplicationData::Destroy())

#define databaseLayer      appData->GetObjectDatabase()

enum eRunMode
{
	START_MODE = 0,

	DESIGNER_MODE = 1,
	ENTERPRISE_MODE = 2,
	SERVICE_MODE = 3
};

// This class is a singleton class.
class CORE_API ApplicationData {

	eRunMode m_runMode;

	Guid m_sessionGuid;

	wxString m_projectPath;
	wxString m_projectDir;
	wxString m_userPath; //каталог пользователя для записи настроек

	wxString m_userName;
	wxString m_userPassword;
	wxString m_computerName;

	wxDateTime m_startedDate;
	wxDateTime m_lastActivity;

	DatabaseLayer* m_objDb;  // Base de datos de objetos
	wxTimer *m_sessionTimer;

	wxCriticalSection m_sessionLocker;

private:

	bool m_bSingleMode; //Монопольный режим
	static ApplicationData *s_instance;

	// hiden constructor
	ApplicationData(const wxString &rootdir = wxT(".")); //for file db 

	void RefreshActiveUsers(); 

public:

	~ApplicationData();
	static ApplicationData* Get(const wxString &rootdir = wxT("."));

	// Force the static appData instance to Init()
	static bool Initialize(eRunMode eMode, const wxString &user, const wxString &password);
	static void Destroy();

	// Initialize application
	bool Connect(eRunMode eMode, const wxString &user, const wxString &password);
	bool Disconnect();

	bool SaveConfiguration();

	DatabaseLayer *GetObjectDatabase() const { return m_objDb; }

	eRunMode GetAppMode() const {
		return m_runMode;
	}

	bool DesignerMode() const {
		return m_runMode == eRunMode::DESIGNER_MODE;
	}

	bool EnterpriseMode() const {
		return m_runMode == eRunMode::ENTERPRISE_MODE;
	}

	bool ServiceMode() const {
		return m_runMode == eRunMode::SERVICE_MODE;
	}

	wxString GetModeDescr(eRunMode mode) const {
		switch (mode)
		{
		case DESIGNER_MODE: return _("Designer");
		case ENTERPRISE_MODE: return _("Thick client");
		case SERVICE_MODE: return _("Deamon");
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

	const wxString &GetUserName() const {
		return m_userName;
	};

	const wxString &GetUserPassword() const {
		return m_userPassword;
	};

	wxString GetComputerName() const {
		return m_computerName;
	}

	const wxString &GetProjectPath() const {  /** Path to the mtd file that is opened. */
		return m_projectPath;
	}

	const wxString &GetApplicationPath() const {
		return m_projectDir;
	};

	wxArrayString GetAllowedUsers() const;

	wxString ComputeMd5() const;
	wxString ComputeMd5(const wxString &userPassword) const;

	bool HasAllowedUsers() const;
	bool CheckLoginAndPassword(const wxString &userName, const wxString &md5Password) const;
	bool ShowAuthorizationWnd();

	bool StartSession(const wxString &userName, const wxString &md5Password);
	bool CloseSession();

protected:
	void OnIdleHandler(wxTimerEvent& event);
};


#endif
