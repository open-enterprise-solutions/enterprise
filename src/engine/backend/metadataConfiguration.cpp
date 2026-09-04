#include "metadataConfiguration.h"

#include "backend/debugger/debugClient.h"
#include "backend/debugger/debugServer.h"

#include "backend/backend_mainFrame.h"
#include "backend/appData.h"
#include "backend/job/jobManager.h"   // the job records are swept once the surviving jobs are known

// ms_instance / Get / Initialize / Destroy retired — ownership moved
// to ibApplicationData::m_activeMetaData (a unique_ptr). The fabric
// lives on ibApplicationData::CreateActiveMetaData / DestroyActiveMetaData;
// callers reach the active metadata through `appEnv::ActiveMetaData()`
// (which the legacy `activeMetaData` macro now redirects to).
//
// Subclass ctors are private + friend ibApplicationData, so `new
// ibMetaDataConfiguration()` outside that fabric is a compile error —
// matches the strict ownership rule used by the rest of the appEnv
// subsystems (sessionRegistry / lockManager / ...).

#include <fstream>
#include <filesystem>

#include "backend/backend_exception.h"   // catch ibBackendException at the LoadCommonTree boundary
#include "backend/query/schemaSnapshot.h"   // ibSchemaSnapshot / ibSchemaTable — ContributeTables drives data dump
#include "backend/query/dataMover.h"         // ibDataMover::Dump / Restore (L3-3 row mover)
#include "backend/serialize/dataBuilder.h"      // ibDataBuilder / ibBinaryProvider — top-level structure builder
#include "backend/objCtor.h"                       // ibCtorMetaValueType::GetClassName — clsid -> type name

bool ibMetaDataConfigurationBase::LoadConfigFromFile(const wxString& strFileName)
{
	std::ifstream in(strFileName.ToStdString(), std::ios::in | std::ios::binary);

	if (!in.is_open())
		return false;

	//go to end
	in.seekg(0, in.end);

	//get size of file
	std::streamsize fsize = in.tellg();

	//go to beginning
	in.seekg(0, in.beg);

	wxMemoryBuffer buffer(fsize);

	in.read((char*)buffer.GetWriteBuf(fsize), fsize);
	buffer.UngetWriteBuf(fsize);
	in.close();

	return LoadConfigFromBuffer(buffer);
}

bool ibMetaDataConfigurationBase::SaveConfigToFile(const wxString& strFileName)
{
	//common data
	wxMemoryBuffer buffer;

	if (!SaveConfigToBuffer(buffer))
		return false;

	// Atomic export (Step 5 single-commit model): build the full blob in a
	// buffer, write it to a sibling temp file, then rename over the target. The
	// rename is the one commit point — a failed or partial write never replaces
	// a good existing config file. Temp sits next to the target so the rename
	// stays on one filesystem (atomic). std::filesystem (C++17), no wx.
	namespace fs = std::filesystem;
	const fs::path dstPath(strFileName.ToStdWstring());
	fs::path tmpPath = dstPath;
	tmpPath += L".tmp";

	{
		std::ofstream datafile(tmpPath, std::ios::binary | std::ios::trunc);
		if (!datafile.is_open())
			return false;

		datafile.write(reinterpret_cast<char*>(buffer.GetData()), buffer.GetBufSize());
		datafile.flush();

		const bool ok = datafile.good();
		datafile.close();
		if (!ok) {
			std::error_code ec;
			fs::remove(tmpPath, ec);
			return false;
		}
	}

	// fs::rename is the atomic commit (replaces the target on the same volume).
	std::error_code ec;
	fs::rename(tmpPath, dstPath, ec);
	if (ec) {
		std::error_code rmEc;
		fs::remove(tmpPath, rmEc);
		return false;
	}

	return true;
}

#include "backend/metaCollection/metaLanguageObject.h"

//**************************************************************************************************
//*                                          ConfigMetadata										   *
//**************************************************************************************************

ibMetaDataConfigurationFile::ibMetaDataConfigurationFile() : ibMetaDataConfigurationBase(),
m_commonObject(nullptr)
{
	//create main metaObject
	m_commonObject = new ibValueMetaObjectConfiguration();
	//m_commonObject->SetReadOnly(!m_metaReadOnly);

	if (m_commonObject->OnCreateMetaObject(this, newObjectFlag)) {
		if (!m_commonObject->OnLoadMetaObject(this)) {
			wxASSERT_MSG(false, "m_commonObject->OnLoadMetaObject() == false");
		}
	}

	m_commonObject->InvalidateNames();
	// m_commonObject is an ibValuePtr — the assignment above already holds the ref.

	{
		ibValue* ppParams[] = { m_commonObject };
		ibValueMetaObjectLanguage* commonLanguage =
			ibValue::CreateAndConvertObjectRef<ibValueMetaObjectLanguage>(g_metaLanguageCLSID, ppParams, 1);

		if (commonLanguage->OnCreateMetaObject(this, newObjectFlag)) {

			if (!commonLanguage->OnLoadMetaObject(this)) {
				wxASSERT_MSG(false, "commonLanguage->OnLoadMetaObject() == false");
			}

			commonLanguage->SetName(wxT("English"));
		}

		commonLanguage->InvalidateNames();
		// owned by m_commonObject's child vector (AddChild inside Init) — no IncrRef

		m_commonObject->SetLanguage(commonLanguage->GetMetaID());
	}
}

ibMetaDataConfigurationFile::~ibMetaDataConfigurationFile()
{
	//clear data
	if (!ClearDatabase()) {
		wxASSERT_MSG(false, "ClearDatabase() == false");
	}

	// m_commonObject (ibValuePtr) releases the root automatically on destruction
	// (DecrRef → cascade). For an unshared root that destroys it; a still-shared
	// root (e.g. designer UI holding a ref) survives until the last holder drops.
}

////////////////////////////////////////////////////////////////////

const ibValueMetaObjectConfiguration* ibMetaDataConfigurationFile::GetCommonMetaObject() const
{
	return m_commonObject; // ibValuePtr operator T* -> raw root
}

ibValueMetaObjectConfiguration* ibMetaDataConfigurationFile::GetCommonMetaObject()
{
	return m_commonObject;
}

wxString ibMetaDataConfigurationFile::GetLangCode() const
{
	if (m_commonObject != nullptr)
		return m_commonObject->GetLangCode();

	return wxT("");
}

bool ibMetaDataConfigurationFile::IsFullAccess() const
{
	// A loop that only asked whether the collection is EMPTY, and said so through a flag: the
	// question spelled as itself. (It also read as unreachable code to MSVC.)
	if (GetAnyArrayObject(g_metaRoleCLSID).empty())
		return true;   // no roles declared at all — access is not governed by them

	if (m_commonObject != nullptr)
		return m_commonObject->AccessRight_Administration();

	return true;
}

////////////////////////////////////////////////////////////////////

bool ibMetaDataConfigurationFile::RunDatabase(int flags)
{

	wxASSERT(!IsConfigOpen());

	if ((flags & loadConfigFlag) == 0)
		ibCompileCode::SetCodeStyle(m_commonObject->GetCompileSyntax());

	// Transactional open as RAII (image presence == open): LoadGuard CREATES the
	// runtime image — and, for a designer-edit config, its compile cache + designer
	// module-manager (bound to the common metaobject, via CreateDesignerModuleManager),
	// so common modules can register into the manager during RunSubtree(true). The
	// guard's dtor ROLLS BACK on ANY exit that isn't Commit() — a failed RunSubtree
	// return OR a raised ibBackendException unwinding the stack — by dropping the image,
	// which frees the half-built ctors + cache + manager, leaving the metadata closed
	// (the load "never happened"). The run fills the SAME metaobject tree, so the
	// image's ctors point at live nodes.
	LoadGuard load(this);

	// RunSubtree fires OnBeforeRun/OnAfterRun on the root itself + every descendant
	// (top-down). Drive the two phases; any failure just returns false and the guard
	// rolls back. A metaobject may also RAISE during the cascade (e.g. a typed-parent
	// mismatch from ibValueMetaObject::GetParentAsType — a malformed configuration):
	// exception == rollback (the guard's dtor aborts as the stack unwinds). We still
	// catch it to log + return false so it doesn't escape onto the worker thread.
	try {
		// Phase 1 — REGISTER: every object announces its identity / type ctor.
		if (!m_commonObject->RunSubtree(flags, ibValueMetaObject::ibRunPhase::Before))
			return false;

		// Seed the editor's context (Manager + Catalogs/Documents/Enums + globals)
		// AFTER the register pass — the ctor-context factories register while the
		// subtree runs, so CreateMainModule must run once they're available. Common-
		// module registration already happened above (in OnBeforeRunMetaObject).
		if (auto* cc = GetCompileCache()) {
			if (auto* mgr = cc->GetModuleManager())
				mgr->CreateMainModule();
		}

		// Phase 2 — RESOLVE: cross-object references + source registration + forms /
		// object-module values (all identities now present).
		if (!m_commonObject->RunSubtree(flags, ibValueMetaObject::ibRunPhase::After))
			return false;
	}
	catch (const ibBackendException& err) {
		ibJournalError(wxT("metadata.config"),err.GetErrorDescription());
		return false;
	}

	// SWEEP THE JOB RECORDS — here, and nowhere earlier.
	//
	// sys_job holds a row per job: its schedule, its switch, when it last ran. A job that was
	// deleted in the Designer leaves one behind, and the moment to notice is exactly this one —
	// the resolve pass has just finished, so every job that still exists has announced itself,
	// and whatever is left in the table belongs to something that did not survive the
	// restructuring. Doing it at the Designer's delete instead would throw the record away while
	// the user could still walk away without saving.
	if (ibJobManager* const jobs = ibApplicationData::GetJobManager())
		jobs->PurgeSharedState();

	// Success — keep the image (LoadGuard.Commit): its presence IS the open state.
	load.Commit();
	// ⭐ ALIVE — said after Commit, because a run that did not survive its own phases never
	// happened. The mirror of Closed below.
	MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Run, GetCommonMetaObject());
	return true;
}

bool ibMetaDataConfigurationFile::CloseDatabase(int flags)
{

	// The assert warns (Debug), the close happens anyway — see the report's twin
	// (metadataReport.cpp). A configuration whose open failed is torn down through this same road,
	// and it must not leave a session that cannot be shut down over a file it already refused.
	wxASSERT(IsConfigOpen());
	if (!IsConfigOpen())
		return true;

	// ⭐⭐ THE WHOLE CONTAINER IS GOING — said BEFORE the teardown, for the same reason `Removed`
	// is: a watcher's business with this is to shut what it is showing OF the tree, and after
	// CloseSubtree there is nothing left to find. This one signal replaces the per-NODE close
	// that used to run inside every object's OnAfterCloseMetaObject.
	MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Closed, GetCommonMetaObject());

	//if (!ExitMainModule((flags & forceCloseFlag) != 0))
	//	return false;

	// CloseSubtree closes every descendant then the root's own hook (bottom-up),
	// unregistering ctors / queryables per node while the image is still live.
	// Close is the LIFO mirror of run: un-resolve → un-register.
	if (!m_commonObject->CloseSubtree(ibValueMetaObject::ibRunPhase::Before))   // un-resolve
		return false;

	// Tear down the designer manager AND release it BEFORE the un-register phase. The
	// metaobject it binds to is about to be reset; dropping the manager now prevents a
	// dangling reference (the next RunDatabase makes a fresh one). Mirrors CreateMainModule.
	if (auto* cc = GetCompileCache()) {
		if (auto* mgr = cc->GetModuleManager())
			mgr->DestroyMainModule();
		cc->SetModuleManager(nullptr);   // ibValuePtr: releases (DecrRef → delete)
	}

	if (!m_commonObject->CloseSubtree(ibValueMetaObject::ibRunPhase::After))    // un-register
		return false;

	// Per-node teardown done — drop the runtime image: frees whatever ctors remain +
	// the module skeleton + the compile cache. Image gone ⇒ closed.
	m_image.reset();
	return true;
}

bool ibMetaDataConfigurationFile::ClearDatabase()
{
	// Full force-replace unload: just drop the tree — the owning child handles
	// cascade the destruction down the subtree. No OnDeleteMetaObject cascade here:
	// the metadata is wholly replaced, and the incoming config reconciles against
	// the old one during the DDL update. (ibValueMetaObject::ClearSubtree can fire
	// the delete events explicitly before a load, if a caller ever wants that.)
	// keepPinned: the predefined configuration module is bound to the root for
	// life — drop only the loaded tree, keep it so the metadata finder (and the
	// debugger's EditModule) can still resolve it after reload.
	m_commonObject->RemoveAllChildren(true);
	return true;
}

bool ibMetaDataConfigurationFile::LoadConfigFromBuffer(const wxMemoryBuffer& buffer)
{
	// Close the old tree's run-state first (unregisters its ctors). For the
	// storage subclass this is virtual and also closes the saved baseline, which
	// the post-load RunDatabase re-runs — leaving that orchestration intact. We
	// do NOT pre-clear the tree: LoadCommonTree builds a fresh detached root and
	// only swaps it in on success, so a failed load leaves the old tree's data
	// in place (all-or-nothing) instead of wiping it up front.
	if (IsConfigOpen()) {
		if (!CloseDatabase(forceCloseFlag)) {
			wxASSERT_MSG(false, "CloseDatabase() == false");
			return false;
		}
	}

	ibReaderMemory readerData(buffer.GetData(), buffer.GetBufSize());

	if (readerData.eof())
		return false;

	// Detached-root: on failure the live tree is untouched; on success the old
	// root is swapped out and released inside LoadCommonTree.
	return LoadCommonTree(g_metaCommonMetadataCLSID, readerData);
}

ibValueMetaObjectConfiguration* ibMetaDataConfigurationFile::BuildFreshRoot()
{
	// Same root setup the ctor gives m_commonObject — OnCreateMetaObject wires the
	// configuration-module property + any predefined children — minus the default
	// Language: LoadSubtree re-creates the serialized Language (and every top-level
	// object) as it loads. Returned at refcount 0 — the caller's ibValuePtr adopts it.
	auto* root = new ibValueMetaObjectConfiguration();
	if (root->OnCreateMetaObject(this, newObjectFlag)) {
		if (!root->OnLoadMetaObject(this)) {
			wxASSERT_MSG(false, "BuildFreshRoot: OnLoadMetaObject() == false");
		}
	}
	root->InvalidateNames();
	return root;
}

bool ibMetaDataConfigurationFile::LoadCommonTree(const ibClassID& clsid, ibReaderMemory& readerData)
{
	// Header (sign + config guid) leads the tree blob in the same stream — read and
	// validate it here so the common-tree blob stays self-describing (was the
	// separate LoadHeader). Config keeps no version in the header (version lives in
	// the common object's data).
	{
		std::shared_ptr<ibReaderMemory> headerReader(readerData.open_chunk(eHeaderBlock));
		if (!headerReader)
			return false;
		if (headerReader->r_u64() != sign_metadata)
			return false;
		wxString metaGuid;
		headerReader->r_stringZ(metaGuid);
	}

	// Configuration is a single kind (MD_MTD == the passed clsid == GetClassType()), so the
	// data block IS keyed by the passed clsid — no external/base split like DataProcessor/Report.
	std::shared_ptr<ibReaderMemory> readerMemory(readerData.open_chunk(clsid));

	if (!readerMemory)
		return false;

	u64 meta_id = 0;
	std::shared_ptr <ibReaderMemory> readerMetaMemory(readerMemory->open_chunk_iterator(meta_id));

	if (!readerMetaMemory)
		return true; // empty config — keep the existing root

	// Parse the stream into the universal structure tree. readerMetaMemory is the
	// root's INNER content ({ eDataBlock, eChildBlock }) — clsid/metaId already
	// peeled above — so the binary provider reads exactly what BuildDataNode wrote.
	ibValuePtr<ibValueMetaObjectConfiguration> fresh(BuildFreshRoot()); // adopt (refcount 0 -> 1)
	if (!fresh)
		return false;
	ibDataNode rootNode(clsid, (ibMetaID)meta_id);
	ibBinaryProvider provider;
	provider.Read(*readerMetaMemory, rootNode);

	// Detached-root atomic swap: apply into the freshly-built root, not the live one.
	// ApplyDataNode throws ibBackendException on a factory miss or bad data — on a
	// throw the fresh root is discarded and m_commonObject is untouched
	// (all-or-nothing). On success, swap it in and release the old root. The caller
	// has already closed the old tree's run-state (or it was never run), so the
	// DecrRef below can't leave dangling entries in the active image's factory.
	try {
		fresh->ApplyDataNode(rootNode);
	}
	catch (const ibBackendException& err) {
		// ⭐ THE ENGINE'S WORDS REACH THE USER — the twin of the report / data-processor catches.
		// A configuration that refuses to load is the costliest of the three to face in silence.
		ibJournalError(wxT("metadata.config"),wxT("%s"), err.GetErrorDescription());
		return false; // fresh (ibValuePtr) discards the root automatically
	}

	// Swap: the ibValuePtr assignment releases the old root (DecrRef -> cascade) and
	// IncrRefs the fresh one; the local `fresh` drops its own ref at scope exit, so
	// m_commonObject ends as the sole owner. No manual refcount.
	m_commonObject = fresh;

	// ⭐ AND EVERYONE WATCHING IS TOLD IT IS READ IN — the stage a tree answers by drawing the
	// whole thing. Said HERE rather than by each caller of the load, because there are several
	// (a file, the database, a fresh root) and a stage nobody sends is a stage that does not exist.
	MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Loaded, GetCommonMetaObject());
	return true;
}

//**************************************************************************************************
//*                                          ConfigMetadata                                        *
//**************************************************************************************************

bool ibMetaDataConfiguration::OnInitialize(const int flags)
{
	m_metaGuid = ibGuid::newGuid();

	if (!ibMetaDataConfigurationStorage::TableAlreadyCreated())
		return false;

	// One debug server per process — for now bound to the active
	// metadata configuration. Per-session debug context
	// (ibSession::Debug) layers on top — handshake is process-level,
	// EnterLoop / step routing is per-session via sessionGuid.
	//
	// Conceptually the debugger lives *above* metadata (it would
	// remain shared if a process ever hosted multiple configurations
	// simultaneously). Today the runtime stays 1:1, so owning it here
	// keeps the lifecycle close to where flags arrive. Move up to
	// ibApplicationData::m_debugServer if multi-metadata lands.
	m_debugServer.reset(new ibDebuggerServer());

	if (!LoadDatabase())
		return false;

	// Localization: pin the process-wide default to the configuration's
	// main language code (metadata short-code form ru/en/uk). Pre-auth
	// callers without a session bound — launcher / login screen — read
	// through this default. Per-session active language is assigned
	// later by SetUserInfo on authentication and stays cached on the
	// session for its whole life.
	ibBackendLocalization::SetUserLanguage(GetLangCode());

	if ((flags & _app_start_create_debug_server_flag) != 0) {
		// wait=true blocks bootstrap until the designer's debugClient
		// connects (OnStart must not run before breakpoints arrive).
		// For wes that's a deadlock — wes needs to bind HTTP and start
		// serving immediately; the designer attaches later through the
		// per-process search. Per-tab WebClient sessions stop at their
		// own OnStart only after the listener exists.
		const bool waitForClient = !appData->WebEnterpriseMode();
		debugServer->CreateServer(defaultHost, defaultDebuggerPort, waitForClient);
	}

	return true;
}

bool ibMetaDataConfiguration::OnDestroy()
{
	// Dtor would do this anyway; explicit reset keeps the order
	// deterministic — debug server is shut down before m_commonObject
	// and friends start unwinding, mirroring the symmetric order of
	// OnInitialize.
	m_debugServer.reset();
	return true;
}

// Out-of-line — m_debugServer holds a unique_ptr<ibDebuggerServer>
// (forward-declared in metadataConfiguration.h). default_delete needs
// the full type here.
ibMetaDataConfiguration::~ibMetaDataConfiguration() = default;

/////////////////////////////////////////////////////////////////////////////////////////////////////

ibMetaDataConfiguration::ibMetaDataConfiguration(ib::AppDataCtorToken) :
	ibMetaDataConfigurationFile(), m_configNew(true)
{
}

//**************************************************************************************************
//*                                          ConfigSaveMetadata                                    *
//**************************************************************************************************

bool ibMetaDataConfigurationStorage::OnInitialize(const int flags)
{
	m_metaGuid = ibGuid::newGuid();

	if (!ibMetaDataConfigurationStorage::TableAlreadyCreated()) {
		ibMetaDataConfigurationStorage::CreateConfigTable();
		ibMetaDataConfigurationStorage::CreateConfigSaveTable();
		ibMetaDataConfigurationStorage::CreateConfigSequence();
	}

	// Designer-side debugger client. Same 1:1-with-metadata footing as
	// m_debugServer above — promoted to ibApplicationData if multi-metadata
	// hosting becomes a thing.
	m_debugClient.reset(new ibDebuggerClient());

	// Load database
	if (!LoadDatabase())
		return false;

#pragma region access
	if (!AccessRight_Administration()) {

		try {
			ibBackendCoreException::Error(_("Not enough access rights for this user!"));
		}
		catch (...) {
		}

		return false;
	}
#pragma endregion

	// Localization: pin the process-wide default to the configuration's
	// main language code — designer always works with the editorial
	// baseline of the configuration regardless of OS locale.
	ibBackendLocalization::SetUserLanguage(GetLangCode());

	return true;
}

bool ibMetaDataConfigurationStorage::OnDestroy()
{
	m_debugClient.reset();
	return true;
}

////////////////////////////////////////////////////////////////////////////////

ibMetaDataConfigurationStorage::ibMetaDataConfigurationStorage(ib::AppDataCtorToken token) :
	ibMetaDataConfiguration(token),
	m_configMetadata(new ibMetaDataConfiguration(token)) {
	// Designer-edit configuration carries a compile-value cache + its module-manager —
	// built with the runtime image (CreateDesignerCache below); callsites gate on
	// `if (auto* cc = metaData->GetCompileCache())` rather than appData->DesignerMode().
}

// Designer-edit config: compile cache + its module-manager (bound to the common
// metaobject, rebuilt fresh with the image each run). Built by the image ctor; dropping
// the image releases the manager (RAII → DestroyMainModule).
// Designer mode only (Enterprise runtime never opens the metadata tree — only objects execute). A config ALWAYS
// carries the cache in the designer: the active edit config AND one browsed read-only from the DB (this inner
// baseline, reached via activeMetaData->GetConfiguration()). A metaobject's form is built THROUGH the cache (its
// owner + the seated source object come from it), so without it an object form opens sourceless and crashes.
// Storage inherits this (its own identical override is gone). Regression from the per-config image (ibMetaImage):
// the cache had narrowed to Storage only. Mode-gated inside, by analogy with the external report / data processor.
std::unique_ptr<ibCompileValueCache> ibMetaDataConfiguration::CreateDesignerCache()
{
	if (!appData->DesignerMode())
		return nullptr;
	auto cache = std::make_unique<ibCompileValueCache>();
	cache->SetModuleManager(new ibValueModuleManagerDesigner(this, GetCommonMetaObject()));
	return cache;
}

ibMetaDataConfigurationStorage::~ibMetaDataConfigurationStorage() {
	wxDELETE(m_configMetadata);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibMetaDataConfigurationStorage::LoadDatabase(int flags)
{
	if (m_configMetadata->LoadDatabase(onlyLoadFlag)) {

		//close if opened
		if (ibMetaDataConfiguration::IsConfigOpen()
			&& !CloseDatabase(forceCloseFlag)) {
			return false;
		}

		if (ibMetaDataConfiguration::LoadDatabase()) {
			Modify(!CompareMetadata(m_configMetadata));
			if (m_configNew)
				SaveDatabase(saveConfigFlag);
			m_configNew = false;
			return true;
		}
	}

	return false;
}

bool ibMetaDataConfigurationStorage::RestoreDataFromBuffer(const wxMemoryBuffer& buffer)
{
	ibReaderMemory reader(buffer);

	//common data
	wxMemoryBuffer bufferData;

	if (reader.r_chunk(1, bufferData)) {

		// The SAME snapshot the dump used — read each table's rows back by its metaID and RESTORE through
		// the L3-3 mover (UPSERT / INSERT / external-UPDATE chosen off the structure).
		const ibSchemaSnapshot snapshot = m_configMetadata->BuildSchemaSnapshot();

		ibReaderMemory readerData(bufferData);
		for (const ibSchemaTable& table : snapshot.Tables()) {
			if (table.m_columns.empty())   // a pure scaffold / seed table (an enum) — no data to move
				continue;
			wxMemoryBuffer tableBuffer;
			if (readerData.r_chunk(table.m_id, tableBuffer)) {
				ibReaderMemory rows(tableBuffer);
				if (!ibDataMover::Restore(table, rows))
					return false;
			}
		}
	}

	//sequence
	wxMemoryBuffer bufferSequence;

	if (reader.r_chunk(2, bufferSequence)) {
		ibReaderMemory readerSequence(bufferSequence);
		LoadSequenceFromBuffer(readerSequence);
	}

	return true;
}

bool ibMetaDataConfigurationFile::SaveConfigToBuffer(wxMemoryBuffer& buffer)
{
	//common data
	ibWriterMemory writer;

	//Save common object (header is written inside SaveCommonTree)
	if (!SaveCommonTree(g_metaCommonMetadataCLSID, writer, saveToFileFlag))
		return false;

	buffer = writer.buffer();
	return true;
}

bool ibMetaDataConfigurationStorage::DumpDataToBuffer(wxMemoryBuffer& buffer)
{
	ibWriterMemory writer;

	//common data
	ibWriterMemory writerData;

	// ONE source of truth: the whole config's structure — the SAME snapshot the DDL differ consumes —
	// drives the data dump too. Every declared table SELECTs its rows through the L3-3 mover, framed by
	// the table's metaID (a pure scaffold / seed table, e.g. an enum, is skipped).
	const ibSchemaSnapshot snapshot = m_configMetadata->BuildSchemaSnapshot();
	for (const ibSchemaTable& table : snapshot.Tables()) {
		if (table.m_columns.empty())
			continue;
		ibWriterMemory tableWriter;
		if (!ibDataMover::Dump(table, tableWriter))
			return false;
		writerData.w_chunk(table.m_id, tableWriter.buffer());
	}

	writer.w_chunk(1, writerData.buffer());

	//sequence 
	ibWriterMemory writerSequence;
	if (SaveSequenceToBuffer(writerSequence))
		writer.w_chunk(2, writerSequence.buffer());

	buffer = writer.buffer();
	return true;
}

bool ibMetaDataConfigurationFile::SaveCommonTree(const ibClassID& clsid, ibWriterMemory& writerData, int flags)
{
	// Header (sign + config guid) leads the tree blob (was the separate SaveHeader).
	{
		ibWriterMemory headerWriter;
		headerWriter.w_u64(sign_metadata); //sign
		headerWriter.w_stringZ(m_commonObject->GetDocPath()); //guid conf
		writerData.w_chunk(eHeaderBlock, headerWriter.pointer(), headerWriter.size());
	}

	// Top-level structure builder: the tree is built into a universal ibDataNode
	// (BuildDataNode, which fills the root's clsid/metaId from the object itself) and
	// serialized through the binary provider.
	(void)clsid; // root identity now comes from the object, not this hint
	ibDataBuilder builder;
	if (!m_commonObject->BuildDataNode(builder.Root(), flags))
		return false;

	// The provider writes the root's INNER content; the container owns the identity frame.
	// Wrap it in chunk(clsid){ chunk(metaId){ inner } } — exactly what LoadCommonTree peels
	// (open_chunk(clsid) -> open_chunk_iterator(metaId) -> provider.Read). Identity comes
	// from the built root, mirroring how children are framed inside the tree.
	ibBinaryProvider provider;
	ibWriterMemory innerWriter;
	if (!builder.Save(provider, innerWriter))
		return false;

	ibWriterMemory metaWriter;
	metaWriter.w_chunk((u64)builder.Root().GetMetaId(), innerWriter.pointer(), innerWriter.size());
	writerData.w_chunk((u64)builder.Root().GetClsid(), metaWriter.pointer(), metaWriter.size());

	// ⭐ …and that it has been written out. A watcher shows this as "no longer modified"; nothing
	// in the tree changed, which is why this is a stage of its own and not MetaDataChanged.
	MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Saved, GetCommonMetaObject());
	return true;
}

bool ibMetaDataConfigurationStorage::DeleteCommonTree(const ibClassID& clsid)
{
	// Deleted-node purge is owned by the node (ibValueMetaObject::DeleteSubtree).
	return m_commonObject->DeleteSubtree();
}



//**************************************************************************************************
//*      the three configuration verbs — see the note in metadataConfiguration.h                   *
//**************************************************************************************************

bool ibMetaDataConfigurationBase::SaveConfiguration(wxString& refusal)
{
	if (!IsEditable()) {
		refusal = _("This configuration is open for reading only.");
		return false;
	}

	try {
		// THE DISKETTE IS ONE CALL. It persists the configuration so it survives a re-login and leaves
		// the LIVE one alone — that is what applying is for, and they are two buttons because they are
		// two intentions.
		//
		// ⭐ WITH saveConfigFlag, AND THAT IS NOT DECORATION. Each metaobject reads the flag in
		// OnSaveMetaObject, and a MODULE does two things when it sees it: hands the debugger its new
		// line count, and drops its row from the AOT cache — the row was compiled from the text that
		// has just been replaced. Called with defaultFlag (0x0000) the test `(flags & saveConfigFlag)`
		// is false, so the save persisted the new text and LEFT THE STALE BYTE CODE in place, to be
		// handed to the next session as current. That is the whole of the "worked yesterday" failure:
		// the byte code answers for names that no longer sit where it thinks (Max, 2026-09-04:
		// *"ты передаёшь нулевой и считаешь, что кэш валидный"*).
		if (!SaveDatabase(saveConfigFlag)) {
			refusal = _("Failed to save the configuration.");
			return false;
		}
	}
	catch (const ibBackendException& e) {
		refusal = e.GetErrorDescription();
		return false;
	}

	return true;
}

bool ibMetaDataConfigurationBase::ApplyConfiguration(wxString& refusal,
	const std::function<bool(const ibRestructureInfo&)>& decide)
{
	if (!IsEditable()) {
		refusal = _("This configuration is open for reading only.");
		return false;
	}

	try {
		// 1 — the configuration itself, with the same flag the save above passes: an apply persists
		// the edited configuration first, and every module in it must retire its cached byte code
		// at that moment, not at some later compile that never comes.
		if (!SaveDatabase(saveConfigFlag)) {
			refusal = _("Failed to save the configuration.");
			return false;
		}

		// 2 — open the apply, AND SAY SO. Whoever asked for this knows already; whoever did NOT is the
		// reason the signal exists — the schema is about to move under them.
		//
		// ⭐⭐ THE ANSWER COMES BACK EITHER WAY (Max, 2026-09-01: *"you still get the event that says
		// the configuration was updated, or was not updated, and you cannot interrupt it — it is simply
		// a notification that arrives"*). A watcher that stopped reading on `Applying` has to be let go
		// on BOTH branches, so the pair is closed by a guard rather than by remembering to say it at
		// each of the exits below — two of which are throws.
		//
		// ⚠ WHAT CHANGED GOES TO THE CALLER ONLY, through `decide`. These two carry the configuration
		// root and nothing else: a watcher that did not start this has no business reading a ledger it
		// cannot act on, and re-reading is the only thing it can do about the news.
		struct ibApplyOutcome {
			ibMetaDataConfigurationBase* const m_config;
			bool m_applied = false;
			~ibApplyOutcome() {
				m_config->MetaObjectStage(m_applied
					? ibMetaDataNotifier::ibMetaStage::Applied
					: ibMetaDataNotifier::ibMetaStage::Reverted, m_config->GetCommonMetaObject());
			}
		} outcome{ this };

		MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Applying, GetCommonMetaObject());

		if (!OnBeforeSaveDatabase(saveConfigFlag)) {
			refusal = _("The update could not be started.");
			return false;
		}

		// 3 — do it; the restructure ledger fills here
		const bool saved = OnSaveDatabase(saveConfigFlag);

		// 4 — ⭐ THE ONE MOMENT "NO" IS FREE, and it belongs to WHOEVER CALLED. The ledger is
		// complete and the transaction is still open, which is why the designer can show it and be
		// answered — and why an assistant answers from an argument instead. A caller that declines gets
		// everything rolled back and keeps the ledger it declined: a real pass, not a rehearsal
		// (schemaSnapshot.h explains why the differ has no rehearsal mode).
		//
		// ⚠ THE DATABASE IS HELD WHILE THIS RUNS. It must answer at once — a dialog is the outer
		// limit, and anything that asks back over a socket holds the base open while it waits.
		const bool declined = saved && decide
			&& !decide(ibMetaDataConfigurationBase::GetRestructureInfo());

		// 5 — commit or roll back. Reached on BOTH branches: this is what closes the transaction, so a
		// path that skipped it would leave the database open.
		if (!OnAfterSaveDatabase(declined || !saved, saveConfigFlag) || !saved) {
			refusal = _("Failed to update the database.");
			return false;
		}

		// A DECLINE IS NOT A FAILURE — it did exactly what was asked. Reported apart from one, or a
		// caller cannot tell whether to fix something or simply to say yes.
		if (declined) {
			refusal = _("Rolled back - nothing was applied.");
			return false;
		}

		// …and it is done. Said by the guard on the way out, so `Applied` cannot arrive for an apply
		// that did not happen — which is the whole value of it to a watcher that did not start it.
		outcome.m_applied = true;
	}
	catch (const ibBackendException& e) {
		// OnSaveDatabase self-rolls-back and releases exclusive on a thrown DDL error (its own
		// try/catch), so there is no transaction left open here — only a message to carry out.
		refusal = e.GetErrorDescription();
		return false;
	}
	catch (const std::exception& e) {
		refusal = wxString::FromUTF8(e.what());
		return false;
	}

	return true;
}

bool ibMetaDataConfigurationBase::RollbackConfiguration(wxString& refusal)
{
	if (!IsEditable()) {
		refusal = _("This configuration is open for reading only.");
		return false;
	}

	if (!RollbackDatabase()) {
		refusal = _("The configuration could not be taken back from the database.");
		return false;
	}

	// ⭐⭐ AND NOW EVERYONE IS TOLD IT IS READ IN AGAIN.
	//
	// 🛑 THIS USED TO SAY NOTHING, on the reasoning that RollbackDatabase closes, clears, loads and
	// runs — so each of those four announces its own stage and a navigator rebuilds because it
	// HEARD. Every step of that is true and the result was still an empty tree: the rows went with
	// the clear and nothing put them back (Max, 2026-09-01: *"the tree clears, the event is there,
	// it just does not refill — press the × in the search box and the tree comes back"*, which is
	// the proof that the metadata was fine and only the signal was missing).
	//
	// A ROLLBACK IS ONE ACT, and this is the verb that knows it finished. Relying on the stages of
	// its parts means relying on every one of those paths to have kept its announce — and one of
	// them has not.
	//
	// ⚠ Harmless if a part did announce: the stage a tree answers by re-reading is idempotent, and
	// a second full read costs one draw.
	//
	// ⭐ AND IT IS ITS OWN STAGE. `Reverted` says what actually happened — this configuration is the
	// database's again — where `Run` would only say that something is running, which was already
	// true a moment before. A watcher that wants to tell a rollback from an ordinary load can;
	// one that does not care answers them the same way, which the navigator does.
	MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Reverted, GetCommonMetaObject());
	return true;
}
