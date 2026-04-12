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
//								ibApplicationDataSessionArray
///////////////////////////////////////////////////////////////////////////////

wxString ibApplicationDataSessionArray::GetUserName(unsigned int idx) const {
	if (idx > m_listSession.size())
		return wxEmptyString;
	return m_listSession[idx].m_strUserName;
}

wxString ibApplicationDataSessionArray::GetComputerName(unsigned int idx) const {
	if (idx > m_listSession.size())
		return wxEmptyString;
	return m_listSession[idx].m_strComputerName;
}

wxString ibApplicationDataSessionArray::GetSession(unsigned int idx) const {
	if (idx > m_listSession.size())
		return wxEmptyString;
	return m_listSession[idx].m_strSession;
}

wxString ibApplicationDataSessionArray::GetStartedDate(unsigned int idx) const {
	if (idx > m_listSession.size())
		return wxEmptyString;
	const wxDateTime& startedDate = m_listSession[idx].m_startedDate;
	return startedDate.Format(wxT("%d.%m.%Y %H:%M:%S"));
}

wxString ibApplicationDataSessionArray::GetApplication(unsigned int idx) const {
	if (idx > m_listSession.size())
		return wxEmptyString;
	return appData->GetRunModeDescr(m_listSession[idx].m_runMode);
}

///////////////////////////////////////////////////////////////////////////////

ibRunMode ibApplicationDataSessionArray::GetSessionApplication(unsigned int idx) const
{
	if (idx > m_listSession.size())
		return ibRunMode::eLAUNCHER_MODE;
	return m_listSession[idx].m_runMode;
}

///////////////////////////////////////////////////////////////////////////////
//								ibApplicationData
///////////////////////////////////////////////////////////////////////////////

ibApplicationData* ibApplicationData::s_instance = nullptr;

///////////////////////////////////////////////////////////////////////////////

bool ibApplicationData::m_forceExit = false;
wxCriticalSection ibApplicationData::m_cs_force_exit;

///////////////////////////////////////////////////////////////////////////////

ibApplicationData::ibApplicationData(ibRunMode runMode) :
	m_runMode(runMode),
	m_strComputer(wxGetHostName()),
	m_startedDate(wxDateTime::Now()),
	m_sessionGuid(wxNewUniqueGuid),
	m_db(nullptr),
	m_sessionUpdater(nullptr),
	m_dbMode(ibDatabaseMode::eNONE),
	m_locale_lang(wxLanguage::wxLANGUAGE_UNKNOWN)
{
}

ibApplicationData::~ibApplicationData()
{
}

///////////////////////////////////////////////////////////////////////////////

bool ibApplicationData::CreateAppDataEnv(ibRunMode runMode)
{
	if (s_instance != nullptr) s_instance->DestroyAppDataEnv();
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try {
#endif
		s_instance = new ibApplicationData(runMode);
		s_instance->ReadEngineConfig();

		if (!SetLocaleAppDataEnv())
			return false;
		return true;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (const ibDatabaseLayerException* err) {
		return false;
	}
#endif
	return false;
}

#ifdef OES_USE_FIREBIRD
#define sys_db wxT("sys.fdb")
#else
#define sys_db wxT("sys.sqlite")
#endif

bool ibApplicationData::CreateFileAppDataEnv(ibRunMode runMode, const wxString& strDirDatabase, const wxString& strLocale)
{
	if (s_instance != nullptr) s_instance->DestroyAppDataEnv();
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try {
#endif
#ifdef OES_USE_FIREBIRD
		std::shared_ptr<ibDatabaseLayerFirebird> db(new ibDatabaseLayerFirebird());
#else
		std::shared_ptr<ibDatabaseLayerSQLite> db(new ibDatabaseLayerSQLite());
#endif
		wxString pathSep = wxFileName::GetPathSeparator();
		if (db->Open(strDirDatabase + pathSep + sys_db)) {

			s_instance = new ibApplicationData(runMode);
			s_instance->m_strFile = strDirDatabase;

			s_instance->ReadEngineConfig();

			s_instance->m_db = db;
			s_instance->m_exclusiveMode = runMode == ibRunMode::eDESIGNER_MODE;

			s_instance->m_dbMode = ibDatabaseMode::eFILE;

			if (runMode == ibRunMode::eDESIGNER_MODE && !ibApplicationData::TableAlreadyCreated()) {
				ibApplicationData::CreateTableSession();
				ibApplicationData::CreateTableUser();
				ibApplicationData::CreateTableEvent();
			}
			else if (!ibApplicationData::TableAlreadyCreated()) {
				DestroyAppDataEnv();
				return false;
			}

			if (!SetLocaleAppDataEnv(strLocale))
				return false;

			return true;
		}
		return false;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (const ibDatabaseLayerException* err) {
		return false;
	}
#endif
	return false;
}

bool ibApplicationData::CreateServerAppDataEnv(ibRunMode runMode, const wxString& strServer, const wxString& strPort,
	const wxString& strUser, const wxString& strPassword, const wxString& strDatabase, const wxString& strLocale)
{
	if (s_instance != nullptr) s_instance->DestroyAppDataEnv();
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try {
#endif

#ifdef OES_USE_FIREBIRD
		// Try Firebird server first
		std::shared_ptr<ibDatabaseLayerFirebird> db(new ibDatabaseLayerFirebird(strServer, strDatabase, strUser, strPassword));
		if (db->Open()) {
#elif defined(OES_USE_POSTGRESQL)
		std::shared_ptr<ibDatabaseLayerPostgres> db(new ibDatabaseLayerPostgres());
		if (db->Open(strServer, strPort, strDatabase, strUser, strPassword)) {
#else
		wxUnusedVar(runMode); wxUnusedVar(strServer); wxUnusedVar(strPort);
		wxUnusedVar(strUser); wxUnusedVar(strPassword); wxUnusedVar(strDatabase); wxUnusedVar(strLocale);
		return false;
	}
	if (false) {
#endif

			s_instance = new ibApplicationData(runMode);

			s_instance->m_strServer = strServer;
			s_instance->m_strPort = strPort;
			s_instance->m_strUser = strUser;
			s_instance->m_strPassword = strPassword;
			s_instance->m_strDatabase = strDatabase;

			s_instance->ReadEngineConfig();

			s_instance->m_db = db;
			s_instance->m_exclusiveMode = runMode == ibRunMode::eDESIGNER_MODE;

			s_instance->m_dbMode = ibDatabaseMode::eSERVER;

			if (!SetLocaleAppDataEnv(strLocale))
				return false;

			if (runMode == ibRunMode::eDESIGNER_MODE && !ibApplicationData::TableAlreadyCreated()) {
				ibApplicationData::CreateTableSession();
				ibApplicationData::CreateTableUser();
				ibApplicationData::CreateTableEvent();
			}
			else if (!ibApplicationData::TableAlreadyCreated()) {
				DestroyAppDataEnv();
				return false;
			}

			return true;
		}
		return false;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (const ibDatabaseLayerException* err) {
		return false;
	}
#endif
	return false;
}

bool ibApplicationData::SetLocaleAppDataEnv(const wxString& strLocale)
{
	if (s_instance == nullptr)
		return false;

	return s_instance->InitLocale(strLocale);
}

bool ibApplicationData::DestroyAppDataEnv()
{
	if (s_instance != nullptr && s_instance->m_db != nullptr) {

		if (!s_instance->Disconnect()) return false;

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

#include <wx/fileconf.h>
#include <wx/stdpaths.h>

bool ibApplicationData::InitLocale(const wxString& locale)
{
	if (m_locale_lang == wxLanguage::wxLANGUAGE_UNKNOWN) {

#ifdef DEBUG_TRANSLATE
		wxLog::AddTraceMask(wxS("i18n"));
#endif // WXDEBUG

		m_locale_lang = wxLocale::GetSystemLanguage();
		if (m_locale_lang == wxLanguage::wxLANGUAGE_UNKNOWN)
			m_locale_lang = wxLanguage::wxLANGUAGE_DEFAULT;

		if (!locale.IsEmpty()) {
			const wxLanguageInfo* foundedLocale = wxLocale::FindLanguageInfo(locale);
			if (foundedLocale != nullptr)
				m_locale_lang = foundedLocale->Language;
		}
		else if (m_configInfo.IsSetLocale()) {
			const wxLanguageInfo* foundedLocale = wxLocale::FindLanguageInfo(m_configInfo.m_strLocale);
			if (foundedLocale != nullptr)
				m_locale_lang = foundedLocale->Language;
		}

		// Independently of whether we succeeded to set the locale or not, try
		// to load the translations (for the default system language) here.

		const wxString& workingDir = wxGetCwd();

		// normally this wouldn't be necessary as the catalog files would be found
		// in the default locations, but when the program is not installed the
		// catalogs are in the build directory where we wouldn't find them by
		// default

		wxFileName fn(wxStandardPaths::Get().GetExecutablePath());

		wxLocale::AddCatalogLookupPathPrefix(workingDir + wxFILE_SEP_PATH + wxT("lang"));
		wxLocale::AddCatalogLookupPathPrefix(fn.GetPath() + wxFILE_SEP_PATH + wxT("lang"));
#if defined(__WXOSX__) || defined(__APPLE__)
		// On macOS, also check outside .app bundle
		wxFileName bundleLang(fn.GetPath());
		bundleLang.RemoveLastDir(); bundleLang.RemoveLastDir(); bundleLang.RemoveLastDir();
		wxLocale::AddCatalogLookupPathPrefix(bundleLang.GetPath() + wxFILE_SEP_PATH + wxT("lang"));
#endif

		if (!m_locale.Init(m_locale_lang)) {
			if (!m_locale.Init(wxLanguage::wxLANGUAGE_ENGLISH))
				return false;
			m_locale_lang = wxLanguage::wxLANGUAGE_ENGLISH;
		}

		// Initialize the catalogs we'll be using.
		m_locale.AddCatalog(wxT("open_es"));

		// Initialize localization engine 
		ibBackendLocalization::SetUserLanguage(m_locale.GetName());

		//Set default time 
		wxDateTime::SetCountry(wxDateTime::Country::Country_Default);
		return true;
	}

	return false;
}

bool ibApplicationData::Connect(const wxString& strUserName, const wxString& strUserPassword, const int flags)
{
	if (m_created_metadata)
		return false;
	if (!StartSession(strUserName, strUserPassword))
		return false;  //start session
	if (!metaDataCreate(m_runMode, flags))
		return false;
	m_created_metadata = true;
	m_run_metadata = activeMetaData->RunDatabase();
	return m_connected_to_db &&
		m_created_metadata &&
		m_run_metadata;
}

bool ibApplicationData::Disconnect()
{
	if (m_created_metadata) {

		if (!CloseSession())
			return false;

		if (m_run_metadata) {
			const bool isConfigOpen = activeMetaData->IsConfigOpen();
			if (isConfigOpen && !activeMetaData->CloseDatabase(forceCloseFlag))
				return false;
		}

		metaDataDestroy();
		m_created_metadata = false;
	}

	return true;
}

#pragma region config

#define BACKEND_CONF wxT("backend.conf")

void ibApplicationData::ReadEngineConfig()
{
	const wxString& workingDir = wxGetCwd(); wxString strConfigFile;
	if (wxFileName::FileExists(workingDir + wxFILE_SEP_PATH + BACKEND_CONF)) {
		strConfigFile = workingDir +
			wxFILE_SEP_PATH + BACKEND_CONF;
	}
	else {
		wxFileName fn(wxStandardPaths::Get().GetExecutablePath());
		wxString exeDir = fn.GetPath();
		if (wxFileName::FileExists(exeDir + wxFILE_SEP_PATH + BACKEND_CONF)) {
			strConfigFile = exeDir + wxFILE_SEP_PATH + BACKEND_CONF;
		}
#if defined(__WXOSX__) || defined(__APPLE__)
		// On macOS, exe is inside .app/Contents/MacOS/ — check 3 levels up
		else {
			wxFileName bundlePath(exeDir);
			bundlePath.RemoveLastDir(); // MacOS
			bundlePath.RemoveLastDir(); // Contents
			bundlePath.RemoveLastDir(); // .app
			wxString bundleDir = bundlePath.GetPath();
			if (wxFileName::FileExists(bundleDir + wxFILE_SEP_PATH + BACKEND_CONF)) {
				strConfigFile = bundleDir + wxFILE_SEP_PATH + BACKEND_CONF;
			}
		}
#endif
	}

	wxFileConfig fc(wxT(""), wxT(""), wxT(""), strConfigFile);
	fc.Read(wxT("Locale"), &m_configInfo.m_strLocale);
}

#pragma endregion 

///////////////////////////////////////////////////////////////////////////////
#include "backend/debugger/debugClient.h"

#pragma region execute 
long ibApplicationData::RunApplication(const wxString& strAppName, bool searchDebug) const
{
	return RunApplication(strAppName,
		m_userInfo.m_strUserName,
		m_userInfo.m_strUserPassword,
		searchDebug
	);
}

long ibApplicationData::RunApplication(const wxString& strAppName, const wxString& strUserName, const wxString& strUserPassword, bool searchDebug) const
{
	wxString executeCmd = strAppName + wxT(' ');

	if (m_strFile.IsEmpty()) {

		if (!m_strServer.IsEmpty())
			executeCmd += wxString::Format(wxT(" /srv %s"), m_strServer);
		if (!m_strPort.IsEmpty())
			executeCmd += wxString::Format(wxT(" /p %s"), m_strPort);
		if (!m_strDatabase.IsEmpty())
			executeCmd += wxString::Format(wxT(" /db %s"), m_strDatabase);
		if (!m_strUser.IsEmpty())
			executeCmd += wxString::Format(wxT(" /usr %s"), m_strUser);
		if (!m_strPassword.IsEmpty())
			executeCmd += wxString::Format(wxT(" /pwd %s"), m_strPassword);
	}
	else {
		executeCmd += wxString::Format(wxT(" /file %s"), m_strFile);
	}

	if (searchDebug)
		executeCmd += wxString::Format(wxT(" /debug"));

	if (!strUserName.IsEmpty())
		executeCmd += wxString::Format(wxT(" /ib_usr %s"), strUserName);

	if (!strUserPassword.IsEmpty())
		executeCmd += wxString::Format(wxT(" /ib_pwd %s"), strUserPassword);

	executeCmd += wxString::Format(wxT(" /lc %s"), m_locale.GetName());

	const long execute = wxExecute(executeCmd);

	if (searchDebug) {

		unsigned short num_attempts = 0;

		debugClient->SearchServer(true);
		while (debugClient != nullptr) {

			if (debugClient->GetConnectionSuccess())
				break;

			if (num_attempts > 300)
				break;

			num_attempts++;
			wxMilliSleep(5);
		}
	}

	return execute;
}

#pragma endregion

///////////////////////////////////////////////////////////////////////////////

bool ibApplicationData::AuthenticationAndSetUser(const wxString& strUserName, const wxString& strUserPassword)
{
	if (!strUserName.IsEmpty() || HasAllowedUser()) {
		m_userInfo = ReadUserData(strUserName);
		return m_userInfo.IsOk() &&
			m_userInfo.m_strUserPassword == ibApplicationData::ComputeMd5(strUserPassword);
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////

#include <wx/zipstrm.h>
#include <wx/wfstream.h>
#include <wx/mstream.h>
#include <wx/filename.h>

bool ibApplicationData::LoadDatabase(const wxString& strFullPath)
{
	wxFileInputStream fis(strFullPath);
	if (!fis.IsOk()) {
		wxLogError("Couldn't open the file '%s'.", strFullPath);
		return false;
	}

	if (!ClearDatabase() && !ClearTableUser())
		return false;

	wxZipInputStream zis(fis);
	std::unique_ptr<wxZipEntry> entry;

	// Iterate through all entries in the zip file
	while (entry.reset(zis.GetNextEntry()), entry) {

		if (!entry->IsDir() && entry->GetName() == wxT("config")) {

			// Open the entry for reading and write its data to a new file
			if (zis.OpenEntry(*entry)) {
				wxMemoryOutputStream fos;
				if (fos.IsOk()) {
					zis.Read(fos); // Read from zip stream, write to file stream
				}
				else {
					return false;
				}

				//load configuration 
				wxMemoryBuffer buffer;
				fos.CopyTo(buffer.GetAppendBuf(fos.GetSize()), fos.GetSize());
				buffer.SetDataLen(fos.GetSize());

				if (!activeMetaData->LoadConfigFromBuffer(buffer))
					return false;

				activeMetaData->RunDatabase();
				activeMetaData->SaveDatabase(saveConfigFlag);
			}
		}
		else if (!entry->IsDir() && entry->GetName() == wxT("user")) {

			// Open the entry for reading and write its data to a new file
			if (zis.OpenEntry(*entry)) {
				wxMemoryOutputStream fos;
				if (fos.IsOk()) {
					zis.Read(fos); // Read from zip stream, write to file stream
				}
				else {
					return false;
				}

				//load user 
				wxMemoryBuffer buffer;
				fos.CopyTo(buffer.GetAppendBuf(fos.GetSize()), fos.GetSize());
				buffer.SetDataLen(fos.GetSize());

				if (!LoadUserInfoFromBuffer(buffer))
					return false;
			}
		}
		else if (!entry->IsDir() && entry->GetName() == wxT("data")) {

			// Open the entry for reading and write its data to a new file
			if (zis.OpenEntry(*entry)) {
				wxMemoryOutputStream fos;
				if (fos.IsOk()) {
					zis.Read(fos); // Read from zip stream, write to file stream
				}
				else {
					return false;
				}

				//load data 
				wxMemoryBuffer buffer;
				fos.CopyTo(buffer.GetAppendBuf(fos.GetSize()), fos.GetSize());
				buffer.SetDataLen(fos.GetSize());

				if (!activeMetaData->LoadDataFromBuffer(buffer))
					return false;
			}
		}
	}

	return true;
}

bool ibApplicationData::SaveDatabase(const wxString& strFullPath)
{
	// 1. Create the physical file output stream
	wxFFileOutputStream out(strFullPath);
	if (!out.IsOk())
	{
		wxLogError("Cannot create output file %s", strFullPath);
		return false;
	}

	// 2. Wrap it in a wxZipOutputStream for compression
	wxZipOutputStream zip(out);

	// PutNextEntry sets up the new entry in the archive
	if (zip.PutNextEntry(wxT("config"))) {
		//save configuration 
		wxMemoryBuffer bufferConfig;
		if (activeMetaData->SaveConfigToBuffer(bufferConfig)) {
			// Wrap the content in an input stream to write it easily
			wxMemoryInputStream contentStream(bufferConfig.GetData(), bufferConfig.GetDataLen());
			zip.Write(contentStream); // Write the data from the content stream
		}
	}

	if (zip.PutNextEntry(wxT("user"))) {
		//save users 
		wxMemoryBuffer bufferUser;
		if (!SaveUserInfoToBuffer(bufferUser))
			return false;
		// Wrap the content in an input stream to write it easily
		wxMemoryInputStream contentStream(bufferUser.GetData(), bufferUser.GetDataLen());
		zip.Write(contentStream); // Write the data from the content stream
	}

	if (zip.PutNextEntry(wxT("data"))) {
		//save data
		wxMemoryBuffer bufferData;
		if (!activeMetaData->SaveDataToBuffer(bufferData))
			return false;
		// Wrap the content in an input stream to write it easily
		wxMemoryInputStream contentStream(bufferData.GetData(), bufferData.GetDataLen());
		zip.Write(contentStream); // Write the data from the content stream
	}

	// 3. Close the zip stream (this is essential to finalize the archive)
	// The underlying file stream 'out' will be closed automatically when 'zip' goes out of scope,
	// or when explicitly calling zip.Close().
	return zip.Close();
}

bool ibApplicationData::ClearDatabase()
{
	if (!m_created_metadata)
		return false;

	if (!activeMetaData->ReCreateDatabase())
		return false;

	return true;
}

wxString ibApplicationData::GetDatabaseDescription()
{
	if (m_dbMode == ibDatabaseMode::eFILE)
		return m_strFile;

	if (m_dbMode == ibDatabaseMode::eSERVER)
		return m_strServer + wxT(":") + m_strPort + wxT("/") + m_strDatabase;

	return wxT("");
}

///////////////////////////////////////////////////////////////////////////////

void ibApplicationData::ReadUserData_Password(const wxMemoryBuffer& buffer, ibApplicationDataUserInfo& userInfo) const
{
	ibReaderMemory reader(buffer);

	userInfo.m_strUserGuid = reader.r_stringZ();
	userInfo.m_strUserName = reader.r_stringZ();
	userInfo.m_strUserFullName = reader.r_stringZ();
	userInfo.m_strUserPassword = reader.r_stringZ();
}

void ibApplicationData::ReadUserData_Role(const wxMemoryBuffer& buffer, ibApplicationDataUserInfo& userInfo) const
{
	ibReaderMemory reader(buffer);

	unsigned int count = reader.r_u32();
	userInfo.m_roleArray.reserve(count);
	for (unsigned int idx = 0; idx < count; idx++) {
		ibApplicationDataUserInfo::ibApplicationDataUserRole entry;
		entry.m_strRoleGuid = reader.r_stringZ();
		entry.m_miRoleId = reader.r_s32();
		userInfo.m_roleArray.emplace_back(std::move(entry));
	}
}

void ibApplicationData::ReadUserData_Language(const wxMemoryBuffer& buffer, ibApplicationDataUserInfo& userInfo) const
{
	ibReaderMemory reader(buffer);
	userInfo.m_strLanguageGuid = reader.r_stringZ();
	userInfo.m_strLanguageCode = reader.r_stringZ();
}

///////////////////////////////////////////////////////////////////////////////

wxMemoryBuffer ibApplicationData::SaveUserData_Password(const ibApplicationDataUserInfo& userInfo) const
{
	ibWriterMemory writer;
	writer.w_stringZ(userInfo.m_strUserGuid);
	writer.w_stringZ(userInfo.m_strUserName);
	writer.w_stringZ(userInfo.m_strUserFullName);
	writer.w_stringZ(userInfo.m_strUserPassword);
	return writer.buffer();
}

wxMemoryBuffer ibApplicationData::SaveUserData_Role(const ibApplicationDataUserInfo& userInfo) const
{
	ibWriterMemory writer;
	writer.w_u32(userInfo.m_roleArray.size());
	for (const auto& role : userInfo.m_roleArray) {
		writer.w_stringZ(role.m_strRoleGuid);
		writer.w_s32(role.m_miRoleId);
	}
	return writer.buffer();
}

wxMemoryBuffer ibApplicationData::SaveUserData_Language(const ibApplicationDataUserInfo& userInfo) const
{
	ibWriterMemory writer;
	writer.w_stringZ(userInfo.m_strLanguageGuid);
	writer.w_stringZ(userInfo.m_strLanguageCode);
	return writer.buffer();
}

///////////////////////////////////////////////////////////////////////////////

#include "backend/utils/md5.hpp"

wxString ibApplicationData::ComputeMd5(const wxString& userPassword) const
{
	if (userPassword.Length() > 0)
		return ibMD5::ComputeMd5(userPassword);

	return wxEmptyString;
}

///////////////////////////////////////////////////////////////////////////////