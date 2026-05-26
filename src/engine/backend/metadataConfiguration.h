#ifndef _CONFIG_METADATA_H__
#define _CONFIG_METADATA_H__

#include <vector>

#include <memory>

#include "backend/metadata.h"
#include "backend/appData.h"
#include "backend/appEnv.h"   // appEnv::ActiveMetaData accessor

class ibDebuggerServer;
class ibDebuggerClient;

//////////////////////////////////////////////////////////////////////////////////////////////////////
// activeMetaData — process-wide configuration metadata. Owned by
// ibApplicationData (m_activeMetaData); reached through the thin
// appEnv accessor. nullptr in modes that don't host metadata
// (launcher, codeRunner). See backend/appEnv.h for the rationale on
// the namespace-fasad over appData's static getters.
#define activeMetaData			(appEnv::ActiveMetaData())
//////////////////////////////////////////////////////////////////////////////////////////////////////
// Lifecycle — fabric on ibApplicationData picks the concrete subclass
// by runMode and stashes the unique_ptr in m_activeMetaData. Returns
// true on success (or true with no-op for modes that don't allocate
// metadata, like launcher).
//
// `metaDataDestroy()` macro was retired — nobody called it; teardown
// happens through `~ibApplicationData`, which resets m_activeMetaData
// (firing the polymorphic dtor chain). Outside-caller-driven destroy
// would go through `ibApplicationData::DestroyActiveMetaData()` directly.
#define metaDataCreate(mode, f)	(ibApplicationData::CreateActiveMetaData(mode, f))
//////////////////////////////////////////////////////////////////////////////////////////////////////

enum ibConfigType {
	ibConfigType_File,
	ibConfigType_Load,
	ibConfigType_Load_And_Save,
};

class BACKEND_API ibMetaDataConfigurationBase : public ibMetaData {
public:

#pragma region access
	virtual bool AccessRight_Administration() const { return true; }
	virtual bool AccessRight_DataAdministration() const { return true; }
	virtual bool AccessRight_UpdateDatabaseConfiguration() const { return true; }
	virtual bool AccessRight_ActiveUsers() const { return true; }
	virtual bool AccessRight_ExclusiveMode() const { return true; }
	virtual bool AccessRight_ModeAllFunction() const { return true; }
#pragma endregion

protected:
	// Construction restricted to ibApplicationData::CreateActiveMetaData
	// via the ib::AppDataCtorToken gate. Concrete subclasses take the
	// token as the first ctor argument; the base ctor stays protected
	// + arg-less so the chain compiles without re-passing the token at
	// every level.
	ibMetaDataConfigurationBase() : ibMetaData() {}

public:

	virtual wxString GetConfigMD5() const = 0;
	virtual wxString GetConfigName() const = 0;

	virtual ibGuid GetConfigGuid() const = 0;

	// if storage save in db 
	virtual bool IsConfigOpen() const { return false; }
	virtual bool IsConfigSave() const { return true; }

	virtual bool LoadDatabase(int flags = defaultFlag) { return true; }
	virtual bool SaveDatabase(int flags = defaultFlag) { return true; }

	//rollback to config db
	virtual bool RollbackDatabase() { return true; }

	//load/save config form buffer
	virtual bool LoadConfigFromBuffer(const wxMemoryBuffer& buffer) { return true; }
	virtual bool SaveConfigToBuffer(wxMemoryBuffer& buffer) { return true; }

	//load/save config form buffer
	virtual bool LoadDataFromBuffer(const wxMemoryBuffer& buffer) { return true; }
	virtual bool SaveDataToBuffer(wxMemoryBuffer& buffer) { return true; }

	//get common metadata
	virtual ibValueMetaObjectConfiguration* GetCommonMetaObject() const = 0;

	// get config metadata in storage
	virtual ibMetaDataConfigurationBase* GetConfiguration() const { return nullptr; }

	//get config type 
	virtual ibConfigType GetConfigType() const = 0;

	//special delete and create 
	virtual bool ReCreateDatabase() { return false; }

	//special save 
	virtual bool OnBeforeSaveDatabase(int flags) { return false; }
	virtual bool OnSaveDatabase(int flags) { return false; }
	virtual bool OnAfterSaveDatabase(bool roolback, int flags) { return false; }

	//load/save form file
	bool LoadConfigFromFile(const wxString& strFileName);
	bool SaveConfigToFile(const wxString& strFileName);

public:

	// Called by the appData fabric right after construction (OnInitialize)
	// and right before destruction (OnDestroy). Subclasses override to
	// wire run-mode-specific state. Singleton Get()/Initialize()/Destroy()
	// retired — ownership is on ibApplicationData::m_activeMetaData; the
	// fabric is ibApplicationData::CreateActiveMetaData.
	//
	// Public so the appData fabric / ~ibApplicationData can call them
	// through a base-class pointer without a friend declaration.
	// Construction itself stays gated on ib::AppDataCtorToken.
	virtual bool OnInitialize(const int flag) { return true; }
	virtual bool OnDestroy() { return true; }
};

class BACKEND_API ibMetaDataConfigurationFile : public ibMetaDataConfigurationBase {
public:

#pragma region access
	virtual bool AccessRight_Administration() const { return m_commonObject->AccessRight_Administration(); }
	virtual bool AccessRight_DataAdministration() const { return m_commonObject->AccessRight_DataAdministration(); }
	virtual bool AccessRight_UpdateDatabaseConfiguration() const { return m_commonObject->AccessRight_UpdateDatabaseConfiguration(); }
	virtual bool AccessRight_ActiveUsers() const { return m_commonObject->AccessRight_ActiveUsers(); }
	virtual bool AccessRight_ExclusiveMode() const { return m_commonObject->AccessRight_ExclusiveMode(); }
	virtual bool AccessRight_ModeAllFunction() const { return m_commonObject->AccessRight_ModeAllFunction(); }
#pragma endregion

	virtual bool IsConfigOpen() const { return m_configOpened; }

	// Public ctor — `ibMetaDataConfigurationFile` is NOT the appData-
	// owned active metadata (those are the leaf subclasses
	// `ibMetaDataConfiguration` and `ibMetaDataConfigurationStorage`,
	// which have private ctor + friend ibApplicationData). The File
	// base is instantiated directly by designer document views that
	// load a stand-alone .obk / XML / JSON for inspection — that is
	// per-document scratch state, not a coordinator singleton.
	ibMetaDataConfigurationFile();
	virtual ~ibMetaDataConfigurationFile();

	virtual wxString GetConfigMD5() const { return m_md5Hash; }
	virtual wxString GetConfigName() const { return m_commonObject->GetName(); }

	virtual ibGuid GetConfigGuid() const { return m_commonObject->GetDocPath(); }

	virtual void SetVersion(const ibVersionID& version) { m_commonObject->SetVersion(version); }
	virtual ibVersionID GetVersion() const { return m_commonObject->GetVersion(); }

	//compare metaData
	virtual bool CompareMetadata(ibMetaDataConfigurationFile* dst) const {
		return m_md5Hash == dst->m_md5Hash;
	}

	//get language code 
	virtual wxString GetLangCode() const;

	//Check is full access 
	virtual bool IsFullAccess() const;

	//run/close 
	virtual bool RunDatabase(int flags = defaultFlag);
	virtual bool CloseDatabase(int flags = defaultFlag);

	virtual bool ClearDatabase();

	//load/save form file
	virtual bool LoadConfigFromBuffer(const wxMemoryBuffer& buffer);

	virtual ibValueMetaObjectConfiguration* GetCommonMetaObject() const { return m_commonObject; }

	//get config type
	virtual ibConfigType GetConfigType() const { return ibConfigType::ibConfigType_File; };

protected:

	//header loader/saver 
	bool LoadHeader(ibReaderMemory& readerData);

	//loader/saver/deleter: 
	bool LoadCommonMetadata(const ibClassID& clsid, ibReaderMemory& readerData);
	bool LoadDatabase(const ibClassID& clsid, ibReaderMemory& readerData, ibValueMetaObject* object);
	bool LoadChildMetadata(const ibClassID& clsid, ibReaderMemory& readerData, ibValueMetaObject* object);

	//run/close recursively:
	bool RunChildMetadata(ibValueMetaObject* object, int flags, bool before);
	bool CloseChildMetadata(ibValueMetaObject* object, int flags, bool before);

	//clear recursively:
	bool ClearChildMetadata(ibValueMetaObject* object);

protected:

	bool m_configOpened;
	wxString m_md5Hash;
	//common meta object
	ibValueMetaObjectConfiguration* m_commonObject;
};

class BACKEND_API ibMetaDataConfiguration : public ibMetaDataConfigurationFile {
public:
	virtual bool LoadConfigFromFile(const wxString& strFileName) {
		if (ibMetaDataConfigurationFile::LoadConfigFromFile(strFileName)) {
			Modify(true); //set modify for check metaData
			return RunDatabase();
		}
		return false;
	}

	// Out-of-line — m_debugServer holds a forward-declared ibDebuggerServer
	// (heavy header), default_delete needs the full type so the dtor lives
	// in metadataConfiguration.cpp where debugServer.h is included.
	virtual ~ibMetaDataConfiguration();

	virtual wxString GetConfigName() const { return m_commonObject->GetName(); }
	virtual ibGuid GetConfigGuid() const { return m_metaGuid; }

	//metaData 
	virtual bool LoadDatabase(int flags = defaultFlag);

	//get config type 
	virtual ibConfigType GetConfigType() const { return ibConfigType::ibConfigType_Load; };

protected:

	virtual bool OnInitialize(const int flag);
	virtual bool OnDestroy();

public:
	// Construction restricted to ibApplicationData::CreateActiveMetaData
	// (and to ibMetaDataConfigurationStorage, which composes an inner
	// ibMetaDataConfiguration as the "saved" reference baseline against
	// which designer edits are compared). Both gate on the
	// ib::AppDataCtorToken — appData mints once, Storage forwards the
	// token it received when constructing its inner baseline.
	explicit ibMetaDataConfiguration(ib::AppDataCtorToken);

protected:

	ibGuid m_metaGuid;
	bool m_configNew;

	// Owned debugger server — one per process, lifecycle bound to the
	// active metadata. The static `ibDebuggerServer::ms_debugServer`
	// cache is published in ctor and retired in dtor; the `debugServer`
	// macro reads that slot.
	std::unique_ptr<ibDebuggerServer> m_debugServer;
};

class BACKEND_API ibMetaDataConfigurationStorage : public ibMetaDataConfiguration {

	struct CSequenceData {
		int m_interval;
		wxString m_strGuid;
		wxString m_strPrefix;
		int m_number;
	};

public:

	virtual ~ibMetaDataConfigurationStorage();

	// Construction restricted to ibApplicationData::CreateActiveMetaData
	// via the ib::AppDataCtorToken gate. The inner baseline reference
	// (m_configMetadata) is constructed by forwarding the same token.
	explicit ibMetaDataConfigurationStorage(ib::AppDataCtorToken);


	//is config save
	virtual bool IsConfigSave() const {
		return CompareMetadata(m_configMetadata);
	}

	//metadata  
	virtual bool LoadDatabase(int flags = defaultFlag);
	virtual bool SaveDatabase(int flags = defaultFlag);

	//run/close 
	virtual bool RunDatabase(int flags = defaultFlag) {
		
		if (!ibMetaDataConfiguration::RunDatabase(flags))
			return false;
		
		return m_configMetadata->RunDatabase(flags | loadConfigFlag);
	}

	virtual bool CloseDatabase(int flags = defaultFlag) {
		if (!ibMetaDataConfiguration::CloseDatabase(flags))
			return false;
		return m_configMetadata->CloseDatabase(flags);
	}

	//Check is full access 
	virtual bool IsFullAccess() const { return true; }

	//rollback to config db
	virtual bool RollbackDatabase();

	//save form file
	virtual bool SaveConfigToBuffer(wxMemoryBuffer& buffer);

	//load/save config form buffer
	virtual bool LoadDataFromBuffer(const wxMemoryBuffer& buffer);
	virtual bool SaveDataToBuffer(wxMemoryBuffer& buffer);

	// get config metaData 
	virtual ibMetaDataConfiguration* GetConfiguration() const { return m_configMetadata; }

	//get config type 
	virtual ibConfigType GetConfigType() const { return ibConfigType::ibConfigType_Load_And_Save; };

	////////////////////////////////////////////////////////////////

	static bool TableAlreadyCreated();

	static void CreateConfigTable();
	static void CreateConfigSaveTable();
	
	static void CreateConfigSequence();

	static void ResetSequence();

	////////////////////////////////////////////////////////////////

	//special save 
	virtual bool OnBeforeSaveDatabase(int flags);
	virtual bool OnSaveDatabase(int flags);
	virtual bool OnAfterSaveDatabase(bool roolback, int flags);

	//special clear 
	virtual bool ReCreateDatabase();

protected:

	virtual bool OnInitialize(const int flag);
	virtual bool OnDestroy();

	//header saver 
	bool SaveHeader(ibWriterMemory& writerData);

	//loader/saver/deleter: 
	bool SaveCommonMetadata(const ibClassID& clsid, ibWriterMemory& writerData, int flags = defaultFlag);
	bool SaveDatabase(const ibClassID& clsid, ibWriterMemory& writerData, int flags = defaultFlag);
	bool SaveChildMetadata(const ibClassID& clsid, ibWriterMemory& writerData, ibValueMetaObject* object, int flags = defaultFlag);
	bool DeleteCommonMetadata(const ibClassID& clsid);
	bool DeleteMetadata(const ibClassID& clsid);
	bool DeleteChildMetadata(const ibClassID& clsid, ibValueMetaObject* object);

private:

	//load/save sequence form buffer
	bool LoadSequenceFromBuffer(const ibReaderMemory& reader);
	bool SaveSequenceToBuffer(ibWriterMemory& writer);

	ibMetaDataConfiguration* m_configMetadata;

	// Designer-side debugger client. Same ownership pattern as
	// m_debugServer on the base; cached pointer through
	// `ibDebuggerClient::ms_debugClient`.
	std::unique_ptr<ibDebuggerClient> m_debugClient;
};

#define sign_metadata 0x1236F362122FE

#endif