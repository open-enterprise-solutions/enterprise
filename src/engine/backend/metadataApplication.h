#ifndef _APPLICATION_METADATA_H__
#define _APPLICATION_METADATA_H__

#include <vector>

#include <memory>

#include "backend/metaData.h"
#include "backend/appData.h"
#include "backend/appEnv.h"   // appEnv::ActiveMetaData accessor
#include "backend/query/provider/database/structure/structureBuilder.h" // ibStructureBuilder — the application-save structure subsystem (member)

class ibDebuggerServer;
class ibDebuggerClient;
class ibDataNode;

//////////////////////////////////////////////////////////////////////////////////////////////////////
// activeMetaData — the application the calling thread's session works in: its own reference
// (ibSession::GetMetaData), else its base's active one (ibApplicationInstance::m_activeMetaData, held for
// the sessions to come); reached through the thin appEnv accessor. nullptr in modes that don't host
// metadata (launcher, coderunner). See backend/appEnv.h for the rationale on the namespace-fasad over
// appData's static getters.
#define activeMetaData			(appEnv::ActiveMetaData())
//////////////////////////////////////////////////////////////////////////////////////////////////////
// Lifecycle — ibApplicationInstance::CreateActiveMetaData makes the application's (ibMetaDataApplication) as the
// base opens; teardown happens in the base's Close (ibApplicationInstance::Close), which closes it, calls OnDestroy
// and then lets the base's reference go — the polymorphic dtor chain runs with the last holder. A studio's
// session's own (ibMetaDataApplicationStorage) is made by the session itself (ibSession::AcquireMetaData) and goes
// with it.
//////////////////////////////////////////////////////////////////////////////////////////////////////

enum ibApplicationType {
	ibApplicationType_File,
	ibApplicationType_Load,
	ibApplicationType_Load_And_Save,
};

class BACKEND_API ibMetaDataApplicationBase : public ibMetaData {
public:

#pragma region access
	virtual bool AccessRight_Administration() const { return true; }
	virtual bool AccessRight_DataAdministration() const { return true; }
	virtual bool AccessRight_UpdateDatabase() const { return true; }
	virtual bool AccessRight_ActiveUsers() const { return true; }
	virtual bool AccessRight_SingleUserMode() const { return true; }
	virtual bool AccessRight_ModeAllFunction() const { return true; }
	virtual bool AccessRight_Assistant() const { return true; }
	virtual bool AccessRight_DataAssistant() const { return true; }
#pragma endregion

protected:
	// Construction restricted to ibApplicationInstance::CreateActiveMetaData
	// via the ib::AppDataCtorToken gate. Concrete subclasses take the
	// token as the first ctor argument; the base ctor stays protected
	// + arg-less so the chain compiles without re-passing the token at
	// every level.
	ibMetaDataApplicationBase() : ibMetaData() {}

	// ⭐ THE BASE THIS APPLICATION WAS MADE FOR — set by the concrete class from its constructor, the way a
	// registry stamps its sessions: a process may hold several bases, and what an application owns (its
	// debugger) asks its base through here rather than through "the current one".
	ibApplicationInstance* m_applicationInstance = nullptr;

public:

	virtual ibApplicationInstance* GetApplicationInstance() const override { return m_applicationInstance; }

	virtual ibString GetApplicationMD5() const = 0;
	virtual ibString GetApplicationName() const = 0;

	virtual ibGuid GetApplicationGuid() const = 0;

	// if storage save in db
	// (IsOpen lives on ibMetaData now — backed by the open-image flag.)
	virtual bool IsApplicationSave() const { return true; }

	// ⭐⭐ THE OTHER HALF OF THE MODIFIED-MARK RULE, and it lives here because it asks a question
	// only an application can answer.
	//
	// A save reports TWICE — `Modify(false)` and then `Modify(true)` (see the plain-save branch in
	// metadataApplicationQuery.cpp): the application stops being what the database has and
	// immediately says so again. The `false` is where the mark re-arms, so the `true` that follows
	// is heard the way the LOAD's report is heard — as a statement of what now stands, not as
	// somebody editing.
	//
	// 🛑 WITHOUT THIS, PRESSING SAVE LEFT THE ASTERISK ON. The base does `m_metaSetModify = true`
	// unconditionally, which is right for every other occasion and wrong for exactly this one
	// (Max, 2026-09-05, watching the button do nothing).
	//
	// ⚠ AND IT IS `IsApplicationSave()`, NOT `!IsModified()`, though at this instant they look alike.
	// `Modify(false)` has just cleared the flag, so `!IsModified()` is true whatever happened;
	// IsApplicationSave compares against the database and is FALSE after a plain save, which is the
	// whole point — the re-arm has to distinguish "saved, database still behind" from "applied".
	// ⚠ ONLY WHEN IT WAS ALREADY ARMED — the first report of all is the load, and the base arms on
	// it; re-arming has to be the ELSE of that, or the load itself would be second-guessed.
	virtual void Modify(bool modify = true) override {

		const bool wasArmed = m_metaSetModify;

		ibMetaData::Modify(modify);

		if (wasArmed && !modify)
			m_metaSetModify = IsApplicationSave();
	}

	// Restructure ledger — the record of REAL structural changes (CREATE/ALTER/DROP table, add/change/
	// remove column + value row) from the application-save differ, plus metadata-validation warnings/errors.
	// Lives on the APPLICATION: only an application restructures, an external data-tool / report never
	// does. STATIC accessor that pulls the ACTIVE application's ledger, so this-less sites (static scaffold
	// methods, the apply-change dialog) reach it without a metadata handle. (replaces s_restructureInfo.)
	static ibRestructureInfo& GetRestructureInfo();
private:
	ibRestructureInfo m_restructureInfo;
public:

	virtual bool LoadDatabase(int flags = defaultFlag) { return true; }
	virtual bool SaveDatabase(int flags = defaultFlag) { return true; }

	//rollback to application db
	virtual bool RollbackDatabase() { return true; }

	//load/save application form buffer
	virtual bool LoadApplicationFromBuffer(const ibMemoryBuffer& buffer) { return true; }
	virtual bool SaveApplicationToBuffer(ibMemoryBuffer& buffer) { return true; }

	//load/save application form buffer
	virtual bool RestoreDataFromBuffer(const ibMemoryBuffer& buffer) { return true; }
	virtual bool DumpDataToBuffer(ibMemoryBuffer& buffer) { return true; }

	//get common metadata
	virtual const ibValueMetaObjectApplication* GetRootMetaObject() const = 0;
	virtual ibValueMetaObjectApplication* GetRootMetaObject() = 0;

	// get application metadata in storage
	virtual ibMetaDataApplicationBase* GetApplication() const { return nullptr; }

	//get application type 
	virtual ibApplicationType GetApplicationType() const = 0;

	// ==========================================================================================
	//  THE THREE APPLICATION VERBS - an application saves, applies and rolls back ITSELF
	// ==========================================================================================
	//
	// STAR2 Max, 2026-09-01: *"the one who calls it has the exclusive right to interrupt it."* They
	// used to sit on ibMetaDataNotifier, and ibMetaData looped over the watchers until one said yes -
	// which is the engine READING AN ANSWER OUT OF A VIEWER, the one shape this whole day removed.
	// It survived because it was written down as "a question" rather than as what it was: a pull.
	//
	// STAR AND THE EXCLUSIVE RIGHT WAS ALREADY IN THE SIGNATURE. `decide` arrives WITH the call, so
	// only the caller can decline - by construction, with no rule about whose answer wins. Everyone
	// else is merely TOLD, through the stages the phases already broadcast: Closed then Loaded then
	// Run for a rollback, Saved for a save. A watcher answers nothing.
	//
	// STOP AND THERE WAS NOTHING OF A TREE IN THEM. The comment that kept them on the notifier said
	// they were "wrapped in the two things only a tree-and-documents host can do - flush the open
	// editors first, reload the navigator afterwards". The code did neither: three bodies of
	// `m_metaData->...`, a read-only guard, and an exception turned into words. The rollback's one
	// exception - an explicit `Load()` - is what the Loaded stage now does by itself.
	virtual bool SaveApplication(ibString& refusal);
	virtual bool ApplyApplication(ibString& refusal,
		const std::function<bool(const class ibRestructureInfo&)>& decide = {});
	virtual bool RollbackApplication(ibString& refusal);

	//special delete and create 
	virtual bool ReCreateDatabase() { return false; }

	//special save
	virtual bool OnBeforeSaveDatabase(int flags) { return false; }
	virtual bool OnSaveDatabase(int flags) { return false; }
	virtual bool OnAfterSaveDatabase(bool roolback, int flags) { return false; }

	// Can this application be applied to a LIVE database — with people connected, without single-user
	// mode? See the Storage override. The base answers "no": a container that cannot say for sure must
	// not invite a dynamic update.
	virtual bool IsDynamicUpdateAvailable() const { return false; }

	//load/save form file
	bool LoadApplicationFromFile(const ibString& strFileName);
	bool SaveApplicationToFile(const ibString& strFileName);

	// …into a directory of files, one a part (ibFileProvider) — and as the set of those files itself, for a directory on
	// another machine: a studio's, the set travelling as one node.
	bool LoadApplicationFromFiles(const ibString& directory);
	bool LoadApplicationFromFiles(const ibDataNode& files);
	bool SaveApplicationToFiles(const ibString& directory);
	bool SaveApplicationToFiles(ibDataNode& files);

public:

	// Called by the appData fabric right after construction (OnInitialize)
	// and right before destruction (OnDestroy). Subclasses override to
	// wire run-mode-specific state. Singleton Get()/Initialize()/Destroy()
	// retired — held by ibApplicationInstance::m_activeMetaData and by the
	// sessions let in while it was active (ibSession::GetMetaData); the
	// fabric is ibApplicationInstance::CreateActiveMetaData.
	//
	// Public so the appData fabric / ~ibApplicationInstance can call them
	// through a base-class pointer without a friend declaration.
	// Construction itself stays gated on ib::AppDataCtorToken.
	virtual bool OnInitialize(const int flag) { return true; }
	virtual bool OnDestroy() { return true; }

	// …and the base's application REPLACED (ibApplicationInstance::ReplaceActiveMetaData, under the lock that guards
	// its holder): `metaData` put in `active`, the one before handed back. Its sessions keep it and work on; it is
	// told here that the base no longer runs it — what it compiles from now it does not save (ibByteCodeStorage::Detach).
	static std::shared_ptr<ibMetaDataApplicationBase> Replace(
		std::shared_ptr<ibMetaDataApplicationBase>& active, std::shared_ptr<ibMetaDataApplicationBase> metaData);
};

class BACKEND_API ibMetaDataApplicationFile : public ibMetaDataApplicationBase {
public:

#pragma region access
	virtual bool AccessRight_Administration() const { return m_rootObject->AccessRight_Administration(); }
	virtual bool AccessRight_DataAdministration() const { return m_rootObject->AccessRight_DataAdministration(); }
	virtual bool AccessRight_UpdateDatabase() const { return m_rootObject->AccessRight_UpdateDatabase(); }
	virtual bool AccessRight_ActiveUsers() const { return m_rootObject->AccessRight_ActiveUsers(); }
	virtual bool AccessRight_SingleUserMode() const { return m_rootObject->AccessRight_SingleUserMode(); }
	virtual bool AccessRight_ModeAllFunction() const { return m_rootObject->AccessRight_ModeAllFunction(); }
	virtual bool AccessRight_Assistant() const { return m_rootObject->AccessRight_Assistant(); }
	virtual bool AccessRight_DataAssistant() const { return m_rootObject->AccessRight_DataAssistant(); }
#pragma endregion

	// Public ctor — `ibMetaDataApplicationFile` is NOT the appData-
	// owned active metadata (those are the leaf subclasses
	// `ibMetaDataApplication` and `ibMetaDataApplicationStorage`,
	// which have private ctor + friend ibApplicationInstance). The File
	// base is instantiated directly by studio document views that
	// load a stand-alone .oap / XML / JSON for inspection — that is
	// per-document scratch state, not a coordinator singleton.
	ibMetaDataApplicationFile();
	virtual ~ibMetaDataApplicationFile();

	virtual ibString GetApplicationMD5() const { return m_md5Hash; }
	virtual ibString GetApplicationName() const { return m_rootObject->GetName(); }

	virtual ibGuid GetApplicationGuid() const { return m_rootObject->GetDocPath(); }

	virtual void SetVersion(const ibVersionID& version) { m_rootObject->SetVersion(version); }
	virtual ibVersionID GetVersion() const { return m_rootObject->GetVersion(); }

	//compare metaData
	virtual bool CompareMetadata(ibMetaDataApplicationFile* dst) const {
		return m_md5Hash == dst->m_md5Hash;
	}

	//get language code 
	virtual ibString GetLangCode() const;

	//Check is full access 
	virtual bool IsFullAccess() const;

	//run/close
	virtual bool RunDatabase(const ibRuntimeRequest& request = ibRuntimeRequest());
	virtual bool CloseDatabase(const ibRuntimeRequest& request = ibRuntimeRequest());

	virtual bool ClearDatabase();

	//load/save application from buffer — application metadata seam shared by the file
	// path (LoadApplicationFromFile/SaveApplicationToFile) and the binary path
	// (appData::LoadDatabase/SaveDatabase). Both operate on m_rootObject,
	// so they live at the File level; the data-buffer counterparts
	// (Load/SaveDataFromBuffer) stay on Storage since they go through the
	// saved baseline.
	virtual bool LoadApplicationFromBuffer(const ibMemoryBuffer& buffer);
	virtual bool SaveApplicationToBuffer(ibMemoryBuffer& buffer);

	// Out-of-line: m_rootObject is an ibValuePtr; its operator T* downcast needs
	// the full ibValueMetaObjectApplication, kept out of this header.
	virtual const ibValueMetaObjectApplication* GetRootMetaObject() const override;
	virtual ibValueMetaObjectApplication* GetRootMetaObject() override;

	//get application type
	virtual ibApplicationType GetApplicationType() const { return ibApplicationType::ibApplicationType_File; };

protected:

	//loader/saver/deleter: (header sign/guid read+written inside LoadRootTree/SaveRootTree)
	bool LoadRootTree(const ibClassID& clsid, ibReaderMemory& readerData);
	bool SaveRootTree(const ibClassID& clsid, ibWriterMemory& writerData, int flags = defaultFlag);

	// Build a detached, initialized application root for the detached-root
	// load swap (LoadRootTree). Returned at refcount 0 — the caller's ibValuePtr
	// adopts it. nullptr on failure.
	ibValueMetaObjectApplication* BuildFreshRoot();

protected:

	// ⭐ THE APPLICATION'S DIGEST, and it is the AOT cache's KEY rather than a mark of what has
	// been applied — recomputed on every load and every save, so a save retires every cached row of
	// the previous application by putting them out of reach (byteCodeCache.h).
	ibString m_md5Hash;

	//root metaobject — owning handle (ibValuePtr): bind = IncrRef, rebind / dtor = DecrRef
	ibValuePtr<ibValueMetaObjectApplication> m_rootObject;
};

class BACKEND_API ibMetaDataApplication : public ibMetaDataApplicationFile {
public:
	// Single load seam for both entry points: file import funnels here
	// (base LoadApplicationFromFile -> LoadApplicationFromBuffer) and direct binary
	// load reaches it straight (appData::LoadDatabase -> LoadApplicationFromBuffer).
	// The File base does close + replace only; here we additionally RUN the
	// freshly loaded tree so its per-type ctors register before any later DDL
	// apply (otherwise GetTypeCtor misses reference/enum types during
	// restructure -> bogus ALTER TABLE -> Firebird -607). Standalone file
	// documents stay on the load-only base impl (ibMetaDataApplicationFile)
	// and run explicitly with onlyLoadFlag where needed.
	virtual bool LoadApplicationFromBuffer(const ibMemoryBuffer& buffer) override {
		if (ibMetaDataApplicationFile::LoadApplicationFromBuffer(buffer)) {
			Modify(true); //set modify for check metaData
			return RunDatabase();
		}
		return false;
	}

	// Out-of-line — m_debugServer holds a forward-declared ibDebuggerServer
	// (heavy header), default_delete needs the full type so the dtor lives
	// in metadataApplication.cpp where debugServer.h is included.
	virtual ~ibMetaDataApplication();

	virtual ibString GetApplicationName() const { return m_rootObject->GetName(); }
	virtual ibGuid GetApplicationGuid() const { return m_metaGuid; }

	//metaData 
	virtual bool LoadDatabase(int flags = defaultFlag);

	//get application type
	virtual ibApplicationType GetApplicationType() const { return ibApplicationType::ibApplicationType_Load; };

	// ⭐ THE COMPILE CACHE IS A STUDIO'S — on an application made for a studio's session: the one it edits and its
	// copy of the database's. Asked of the token it was made with, not of the thread that builds it: an apply builds
	// on the studio's thread, and the base's own application, the one the runtime works in, carries none — given
	// one, it handed the runtime the studio's one instance of a form ("duplicate identifier Controls", 2026-10-28).
	virtual std::unique_ptr<ibCompileValueCache> CreateStudioCache() override;

protected:

	virtual bool OnInitialize(const int flag);
	virtual bool OnDestroy();

public:
	// Construction restricted to ibApplicationInstance::CreateActiveMetaData
	// (and to ibMetaDataApplicationStorage, which composes an inner
	// ibMetaDataApplication as the "saved" reference baseline against
	// which studio edits are compared). Both gate on the
	// ib::AppDataCtorToken — appData mints once, Storage forwards the
	// token it received when constructing its inner baseline. The token names
	// the base it is made for (GetApplicationInstance) and the session it is made for (GetSessionKind).
	explicit ibMetaDataApplication(ib::AppDataCtorToken owner);

protected:

	// What it was made with — its base, and the session it was made for; a new copy is made with it.
	const ib::AppDataCtorToken m_owner;

	ibGuid m_metaGuid;
	bool m_applicationNew;

	// Owned debugger server — one per process, lifecycle bound to the
	// active metadata. The static `ibDebuggerServer::ms_debugServer`
	// cache is published in ctor and retired in dtor; the `debugServer`
	// macro reads that slot.
	std::unique_ptr<ibDebuggerServer> m_debugServer;
};

class BACKEND_API ibMetaDataApplicationStorage : public ibMetaDataApplication {

	struct CSequenceData {
		int m_interval;
		ibString m_strGuid;
		ibString m_strPrefix;
		int m_number;
	};

public:

	virtual ~ibMetaDataApplicationStorage();

	// Construction restricted to a studio's session (ibSession::AcquireMetaData)
	// via the ib::AppDataCtorToken gate. The inner baseline reference
	// (m_applicationMetadata) is constructed by forwarding the same token — and with it the same base.
	explicit ibMetaDataApplicationStorage(ib::AppDataCtorToken owner);


	//is application save
	virtual bool IsApplicationSave() const {
		return CompareMetadata(GetApplication());
	}

	//metadata
	virtual bool LoadDatabase(int flags = defaultFlag);
	virtual bool SaveDatabase(int flags = defaultFlag);

	//run/close
	// The database's copy runs as the mirror it is (loadApplicationFlag), once: after an apply it is the base's active
	// application as well, running already.
	virtual bool RunDatabase(const ibRuntimeRequest& request = ibRuntimeRequest()) {

		if (!ibMetaDataApplication::RunDatabase(request))
			return false;

		ibRuntimeRequest mirrorRequest = request;
		mirrorRequest.m_flags |= loadApplicationFlag;
		return m_applicationMetadata->IsOpen() || m_applicationMetadata->RunDatabase(mirrorRequest);
	}

	// …and closes with this one only while nobody else holds it; held by the base and its sessions, it goes with
	// the last of them.
	virtual bool CloseDatabase(const ibRuntimeRequest& request = ibRuntimeRequest()) {
		if (!ibMetaDataApplication::CloseDatabase(request))
			return false;
		if (m_applicationMetadata.use_count() > 1 || !m_applicationMetadata->IsOpen())
			return true;
		return m_applicationMetadata->CloseDatabase(request);
	}

	//Check is full access 
	virtual bool IsFullAccess() const { return true; }

	//rollback to application db
	virtual bool RollbackDatabase();


	//load/save data form buffer (table rows + sequence — goes through the
	// saved baseline, so it stays on Storage; application metadata seam lives on
	// ibMetaDataApplicationFile)
	virtual bool RestoreDataFromBuffer(const ibMemoryBuffer& buffer);
	virtual bool DumpDataToBuffer(ibMemoryBuffer& buffer);

	// get application metaData
	virtual ibMetaDataApplication* GetApplication() const { return m_applicationMetadata.get(); }

	//get application type 
	virtual ibApplicationType GetApplicationType() const { return ibApplicationType::ibApplicationType_Load_And_Save; };

	////////////////////////////////////////////////////////////////

	static bool TableAlreadyCreated();

	static void CreateApplicationTable();
	static void CreateApplicationSaveTable();
	
	static void CreateApplicationSequence();

	static void ResetSequence();

	////////////////////////////////////////////////////////////////

	//special save
	virtual bool OnBeforeSaveDatabase(int flags);
	virtual bool OnSaveDatabase(int flags);
	virtual bool OnAfterSaveDatabase(bool roolback, int flags);

	// TRUE when the change lives entirely in modules, forms and properties: nobody's tables move under
	// the people who are working, and they meet the new application when they next log in. FALSE means
	// the apply will write DDL, and DDL means single-user mode.
	//
	// Answered by comparing the two schema snapshots OnSaveDatabase would diff (the saved baseline + the
	// edited application) — see SameStructure. Asked BEFORE the apply starts, so the studio can offer the
	// choice instead of refusing; the apply keeps its own gate either way (ibStructureBatch::Flush).
	virtual bool IsDynamicUpdateAvailable() const override;

	//special clear 
	virtual bool ReCreateDatabase();

protected:

	virtual bool OnInitialize(const int flag);
	virtual bool OnDestroy();

	//deleter: deleted-node purge (SaveRootTree moved down to File level)
	bool DeleteRootTree(const ibClassID& clsid);

private:

	//load/save sequence form buffer
	bool LoadSequenceFromBuffer(const ibReaderMemory& reader);
	bool SaveSequenceToBuffer(ibWriterMemory& writer);

	// THE DATABASE'S COPY — what IsApplicationSave compares against and what an apply diffs from. The studio's, made with
	// its token: an apply reads the published application into a NEW one, and the base reads its own
	// (ibApplicationInstance::ReplaceActiveMetaData) — the studio's copy is never handed to the runtime.
	std::shared_ptr<ibMetaDataApplication> m_applicationMetadata;

	// The application-save STRUCTURE + SEED engine — METADATA-AGNOSTIC: it works only on snapshots this Storage
	// hands it (built via ContributeTables on the edited application + the saved baseline). On save the Storage
	// builds both snapshots and drives the builder's before/during/after events. (query/provider/database/structure/structureBuilder.h)
	ibStructureBuilder m_structureBuilder;

	// Studio-side debugger client. Same ownership pattern as
	// m_debugServer on the base; cached pointer through
	// `ibDebuggerClient::ms_debugClient`.
	std::unique_ptr<ibDebuggerClient> m_debugClient;
};

#define sign_metadata 0x1236F362122FE

#endif