////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : external metaData - for dataTools, reports
////////////////////////////////////////////////////////////////////////////

#include "metadataDataTool.h"
#include "backend/appData.h"

ibMetaDataDataTool::ibMetaDataDataTool() : ibMetaData(),
m_ownerMeta(nullptr),
m_moduleManager(nullptr),
m_rootObject(nullptr),
m_version(version_oes_last)
{
	// Compile cache (studio mode only) is built by the image ctor via
	// CreateCompileCache() below — backs code-editor / form-preview lookups via
	// GetCompileCache(). At runtime it stays absent, so StartMainModule's "show default
	// form" branch (gated by !cc) runs.

	//create main metaObject
	m_rootObject = new ibValueMetaObjectExternalDataTool;
	ibNameRequest request;
	request.m_clsid = g_metaExternalDataToolCLSID;
	request.m_prefix = m_rootObject->GetClassName();
	m_rootObject->SetName(ibMetaData::GetNewName(nullptr, request));

	if (m_rootObject->OnCreateMetaObject(this, newObjectFlag)) {
		if (!m_rootObject->OnLoadMetaObject(this)) {
			ibASSERT_MSG(false, "m_rootObject->OnLoadMetaObject() == false");
		}
	}

	m_rootObject->InvalidateNames();
	// m_rootObject is an ibValuePtr — the assignment above already holds the ref.

	// ⚠ NO MODULE MANAGER YET — it is built where the data tool first runs (RunDatabase), on whichever road it came: the
	// loader holds this container by then (ibMetaData::MakeShared), and the object the manager makes takes its own
	// reference to it. Built here, it held nothing — nothing holds a container while it is being constructed — and a
	// data tool made in memory closed under its own open form (the external report's twin, metadataReport.cpp).

	m_ownerMeta = this;
}

ibMetaDataDataTool::ibMetaDataDataTool(ibMetaData* metaData, ibValueMetaObjectDataTool* srcDataTool) : ibMetaData(),
m_ownerMeta(nullptr),
m_moduleManager(nullptr),
m_rootObject(srcDataTool),
m_version(version_oes_last)
{
	m_rootObject->InvalidateNames();
	// m_rootObject (ibValuePtr) holds the member ref beside the application's own.

	m_ownerMeta = metaData;
}

ibMetaDataDataTool::~ibMetaDataDataTool()
{
	if (m_rootObject->IsExternalCreate()) {

		// Release the manager first, while the root is still alive — the manager
		// references the root via m_objectValue, and its dtor runs DestroyMainModule
		// (RAII). The ibValuePtr assignment to nullptr is the release.
		m_moduleManager = nullptr;

		//clear data
		if (!ClearDatabase()) {
			ibASSERT_MSG(false, "ClearDatabase() == false");
		}

		// m_rootObject (ibValuePtr) releases the root after this body.
	}
}

ibValueMetaObjectDataTool* ibMetaDataDataTool::GetDataTool() const
{
	return m_rootObject; // ibValuePtr operator T*
}

const ibValueMetaObject* ibMetaDataDataTool::GetRootMetaObject() const
{
	return m_rootObject; // ibValuePtr operator T* -> upcast to base
}

ibValueMetaObject* ibMetaDataDataTool::GetRootMetaObject()
{
	return m_rootObject;
}

bool ibMetaDataDataTool::LoadDatabase()
{
	return RunDatabase();
}

bool ibMetaDataDataTool::SaveDatabase()
{
	return true;
}

bool ibMetaDataDataTool::ClearDatabase()
{
	// Full force-replace unload: just drop the tree — owning handles cascade the
	// destruction. No OnDeleteMetaObject cascade (metadata is wholly replaced;
	// reconciliation happens on the next load). ClearSubtree is available if a
	// caller ever wants to fire the delete events explicitly before a load.
	// keepPinned: predefined object/manager modules are bound to the root for life.
	m_rootObject->RemoveAllChildren(true);
	return true;
}

////////////////////////////////////////////////////////////////////

ibString ibMetaDataDataTool::GetLangCode() const
{
	return activeMetaData->GetLangCode();
}

////////////////////////////////////////////////////////////////////

std::unique_ptr<ibCompileValueCache> ibMetaDataDataTool::CreateStudioCache()
{
	// Studio mode only — the cache backs the code editor / form preview. Only an
	// external (.otl) data tool additionally owns a module-manager (bound to its object
	// module); an embedded data tool runs against the application's. Called by the image ctor.
	if (!appData->StudioMode())
		return nullptr;
	auto cache = std::make_unique<ibCompileValueCache>();
	if (m_rootObject->IsExternalCreate())
		cache->SetModuleManager(new ibValueModuleManagerStudio(this, m_rootObject->GetObjectModule()));
	return cache;
}

bool ibMetaDataDataTool::RunDatabase(const ibRuntimeRequest& request)
{
	// RunSubtree fires the root's own OnBeforeRun/OnAfterRun + every descendant.
	// CreateMainModule/StartMainModule stay interleaved between the two phases.
	// Transactional like ibMetaDataApplicationFile::RunDatabase via LoadGuard: it
	// CREATES the runtime image and drops it on ANY exit that isn't Commit() — a failed
	// return OR a raised exception (exception == rollback) — so a partial run leaves the
	// metadata closed (the load "never happened"). StartMainModule runs during the build
	// (it reads the compile cache through GetCompileCache() = the live image); Commit()
	// keeps the image only once it has succeeded, so a failed start rolls back cleanly.
	LoadGuard load(this);

	if (m_rootObject->IsExternalCreate()) {

		// The runtime module manager, on the root it runs — born here, on the first run of this root: the loader
		// holds the container by now, so the object can take its own reference (see the ctor).
		if (m_moduleManager == nullptr) {
			m_moduleManager = new ibValueModuleRuntimeManagerExternalDataTool(weak_from_this().lock(), m_rootObject);
			m_moduleManager->InvalidateNames();
		}

		// The studio module-manager (for the data tool's own compile cache) was already
		// built by the image ctor via CreateStudioModuleManager() — shared modules
		// register into it during RunSubtree(true) below.
		if (!m_rootObject->RunSubtree(request.m_flags, ibValueMetaObject::ibRunPhase::Before))
			return false;

		if (auto* cc = GetCompileCache()) {
			if (auto* mgr = cc->GetModuleManager())
				mgr->CreateMainModule();
		}

		if (m_moduleManager->CreateMainModule()) {
			if (!m_rootObject->RunSubtree(request.m_flags, ibValueMetaObject::ibRunPhase::After))
				return false;
			if (!m_moduleManager->StartMainModule(request))
				return false;
			load.Commit();   // keep the image → data tool is now open
			return true;
		}
		return false;
	}
	else if (!m_rootObject->IsExternalCreate()) {

		if (!m_rootObject->RunSubtree(request.m_flags, ibValueMetaObject::ibRunPhase::Before))
			return false;
		if (!m_rootObject->RunSubtree(request.m_flags, ibValueMetaObject::ibRunPhase::After))
			return false;

		load.Commit();
		// ⭐ ALIVE — said after Commit, because a run that did not survive its own phases never
		// happened. The mirror of Closed below.
		MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Run, GetRootMetaObject());
		return true;
	}

	return false;
}

bool ibMetaDataDataTool::CloseDatabase(const ibRuntimeRequest& request)
{
	// The assert warns (Debug), the close happens anyway — see the report's twin
	// (metadataReport.cpp): a refused open is rolled back through this very road, and a bad file
	// must not leave an application that cannot be shut down.
	ibASSERT(IsOpen());
	if (!IsOpen())
		return true;

	// ⭐⭐ THE WHOLE CONTAINER IS GOING — said BEFORE the teardown, for the same reason `Removed`
	// is: a watcher's business with this is to shut what it is showing OF the tree, and after
	// CloseSubtree there is nothing left to find. This one signal replaces the per-NODE close
	// that used to run inside every object's OnAfterCloseMetaObject.
	MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Closed, GetRootMetaObject());

	if (!ExitMainModule(request))
		return false;

	// CloseSubtree closes every descendant then the root's own hook (bottom-up);
	// it self-skips a deleted node.
	if (!m_rootObject->CloseSubtree(ibValueMetaObject::ibRunPhase::Before))   // un-resolve
		return false;

	// Symmetric to RunDatabase — tear down + release the studio manager (its
	// object module is about to be reset/freed; dropping it avoids a dangle).
	if (auto* cc = GetCompileCache()) {
		if (auto* mgr = cc->GetModuleManager())
			mgr->DestroyMainModule();
		cc->SetModuleManager(nullptr);
	}

	if (!m_rootObject->CloseSubtree(ibValueMetaObject::ibRunPhase::After))    // un-register
		return false;

	m_image.reset();   // drop the runtime image ⇒ closed (frees ctors + modules + cache)
	return true;
}

#include <fstream>
#include <filesystem>

#include "core/fileSystem.h"             // ibPathOf
#include "backend/backend_exception.h"   // catch ibBackendException at the LoadRootTree boundary
#include "core/serialize/dataBuilder.h"  // ibDataBuilder / ibBinaryProvider — top-level structure builder
#include "backend/temp/tempStorage.h"       // ibTempFile — LoadFromTempFile

ibValueMetaObjectDataTool* ibMetaDataDataTool::BuildFreshRoot()
{
	// Mirror the ctor's external-root setup — the module manager is a runtime concern
	// (CreateObjectExtValue / CreateMainModule), not touched by LoadSubtree, built by
	// the run after the swap. Returned at refcount 0 — the caller's ibValuePtr adopts it.
	auto* root = new ibValueMetaObjectExternalDataTool;
	ibNameRequest request;
	request.m_clsid = g_metaExternalDataToolCLSID;
	request.m_prefix = root->GetClassName();
	root->SetName(ibMetaData::GetNewName(nullptr, request));
	if (!root->OnCreateMetaObject(this, newObjectFlag)) {
		// Released by the RUNTIME, not by `delete` — see the twin of this in metadataReport.cpp
		// and ibBackendRuntimeOwned (compiler/value.h) for why that is now a compile-time matter.
		ibValuePtr<ibValueMetaObjectExternalDataTool> discard(root);
		return nullptr;
	}
	if (!root->OnLoadMetaObject(this)) {
		ibASSERT_MSG(false, "BuildFreshRoot: OnLoadMetaObject() == false");
	}
	root->InvalidateNames();
	return root;
}

bool ibMetaDataDataTool::LoadFromFile(const ibString& strFileName)
{
	// Read the whole file up front — no tree is touched until the bytes are in
	// hand, so a read failure leaves the current data tool intact.
	std::ifstream in(strFileName.ToStdString(), std::ios::in | std::ios::binary);
	if (!in.is_open())
		return false;
	in.seekg(0, in.end);
	std::streamsize fsize = in.tellg();
	in.seekg(0, in.beg);
	ibMemoryBuffer tempBuffer(fsize);
	in.read((char*)tempBuffer.GetWriteBuf(fsize), fsize);
	tempBuffer.UngetWriteBuf(fsize);
	in.close();

	return LoadFromBuffer(tempBuffer, strFileName);
}

bool ibMetaDataDataTool::LoadFromTempFile(const ibString& id)
{
	// The parts read in order, whole — as the disk's file is read up front.
	const ibTempFile file(id);
	if (!file.IsOpened())
		return false;

	ibMemoryBuffer tempBuffer;
	ibMemoryBuffer part;
	for (int index = 0; file.ReadPart(index, part); ++index)
		tempBuffer.AppendData(part.GetData(), part.GetDataLen());

	return LoadFromBuffer(tempBuffer, file.GetName());
}

bool ibMetaDataDataTool::LoadFromBuffer(const ibMemoryBuffer& buffer, const ibString& fullPath)
{
	ibReaderMemory readerData(buffer.GetData(), buffer.GetDataLen());
	if (readerData.eof())
		return false;

	m_fullPath = fullPath;

	// (A file read INTO an application is read here on its own, with its own ids, and then pasted into it — the paste
	// gives its objects their ids and re-homes what holds them: ReadExternal, schema/studio/metaData.cpp, #578.)

	// External (start from file): standalone data tool — detached-root swap.
	// Build a fresh root and load into it; on failure the live data tool is
	// untouched (all-or-nothing).
	ibValuePtr<ibValueMetaObjectDataTool> fresh(BuildFreshRoot()); // adopt (refcount 0 -> 1)
	if (!fresh)
		return false;
	if (!LoadRootTree(fresh, g_metaExternalDataToolCLSID, readerData))
		return false; // fresh (ibValuePtr) discards the root automatically

	// Commit. Close the old tree's run-state first (unregisters its ctors). Then tear
	// down the old module manager BEFORE the swap — the ibValuePtr assignment releases
	// the old root, and the manager references it via m_objectValue (use-after-free if
	// the manager outlives the root). Then swap (the assignment releases old + adopts
	// fresh); the run builds the manager on the fresh root (RunDatabase).
	if (IsOpen() && !CloseDatabase(ibRuntimeRequest(forceCloseFlag)))
		return false; // fresh discarded automatically

	// Release the old manager first (its dtor runs DestroyMainModule via RAII) while
	// the old root is still alive — the manager references it via m_objectValue. Then
	// swap the root; the new manager is built on the fresh root by its run (RunDatabase).
	m_moduleManager = nullptr;

	m_rootObject = fresh; // ibValuePtr: release old root (DecrRef -> cascade), adopt fresh

	return LoadDatabase();
}

bool ibMetaDataDataTool::SaveToFile(const ibString& strFileName)
{
	//common data
	ibWriterMemory writerData;
	if (!SaveToBuffer(writerData, strFileName))
		return false;

	// Atomic export: write to a sibling temp file, then rename over the target
	// (single commit point — a partial/failed write never replaces a good file).
	// std::filesystem, no wx.
	namespace fs = std::filesystem;
	const fs::path dstPath = ibPathOf(strFileName);
	fs::path tmpPath = dstPath;
	tmpPath += L".tmp";

	{
		std::ofstream datafile(tmpPath, std::ios::binary | std::ios::trunc);
		if (!datafile.is_open())
			return false;

		datafile.write(reinterpret_cast<char*>(writerData.pointer()), writerData.size());
		datafile.flush();

		const bool ok = datafile.good();
		datafile.close();
		if (!ok) {
			std::error_code ec;
			fs::remove(tmpPath, ec);
			return false;
		}
	}

	std::error_code ec;
	fs::rename(tmpPath, dstPath, ec);
	if (ec) {
		std::error_code rmEc;
		fs::remove(tmpPath, rmEc);
		return false;
	}

	return true;
}

bool ibMetaDataDataTool::SaveToTempFile(const ibString& id)
{
	// The file, made anew of the name it was given — written as the disk's file is, whole.
	ibTempFile file;
	if (!file.Create(id, true))
		return false;

	ibWriterMemory writerData;
	if (!SaveToBuffer(writerData, file.GetName()))
		return false;

	return file.Write(writerData.pointer(), writerData.size());
}

bool ibMetaDataDataTool::SaveToBuffer(ibWriterMemory& writerData, const ibString& fullPath)
{
	m_fullPath = fullPath;

	//Save common object (header is written inside SaveRootTree)
	if (!SaveRootTree(g_metaExternalDataToolCLSID, writerData, saveApplicationFlag))
		return false;

	//Delete common object
	if (!DeleteRootTree(g_metaExternalDataToolCLSID))
		return false;

	return true;
}

bool ibMetaDataDataTool::LoadRootTree(ibValueMetaObjectDataTool* root, const ibClassID& clsid, ibReaderMemory& readerData)
{
	// Header (sign + version + guid) leads the tree blob in the same stream — read
	// and validate it here so the common-tree blob stays self-describing (was the
	// separate LoadHeader).
	//
	// 🛑 BYTES THIS BUILD CANNOT READ ARE REFUSED IN WORDS — the external report's reader says why (metadataReport.cpp).
	const auto unreadable = [this]() {
		ibBackendCoreException::Error(_("'%s' is stored in a format this build cannot read: it was written by an older or "
			"a different build. Nothing was loaded."), ibTextOf(ibPathOf(m_fullPath).filename()));
	};
	{
		std::shared_ptr<ibReaderMemory> headerReader(readerData.open_chunk(eHeaderBlock));
		if (!headerReader || headerReader->r_u64() != sign_dataTool)
			unreadable();
		m_version = headerReader->r_u32();
		ibString metaGuid;
		headerReader->r_stringZ(metaGuid);
	}

	// The tree's data block is keyed by the ROOT object's GetClassType() at save time — the EXTERNAL container
	// clsid (MD_EDTL; SaveRootTree frames it under builder.Root().GetClsid(), metaObjectSerialize.cpp
	// BuildDataNode). open_chunk returns an OWNED reader (shared_ptr-safe); open_chunk_iterator does NOT own
	// cleanly, so avoid it here.
	(void)clsid;
	std::shared_ptr<ibReaderMemory> readerMemory(readerData.open_chunk(g_metaExternalDataToolCLSID));

	if (!readerMemory)
		unreadable();

	std::uint64_t meta_id = 0;
	std::shared_ptr<ibReaderMemory> readerMetaMemory(readerMemory->open_chunk_iterator(meta_id));

	if (!readerMetaMemory)
		return true;

	// Parse the inner content into the universal structure tree, then apply into
	// the caller-provided root (a detached fresh root for the start-from-file
	// swap). ApplyDataNode throws
	// ibBackendException on a factory miss / bad data — catch at this container
	// boundary and report false (the caller discards the fresh root).
	ibDataNode rootNode(root->GetClassType(), (ibMetaID)meta_id);
	ibBinaryProvider provider;
	provider.Read(*readerMetaMemory, rootNode);
	try {
		root->ApplyDataNode(rootNode);

		// ⭐ AND EVERYONE WATCHING IS TOLD IT IS READ IN — the stage a tree answers by drawing the
		// whole thing. Said HERE rather than by each caller of the load, because there are several
		// (a file, the database, a fresh root) and a stage nobody sends is a stage that does not exist.
		MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Loaded, GetRootMetaObject());
		return true;
	}
	catch (const ibCoreException& err) {
		// ⭐ THE ENGINE'S WORDS REACH THE USER — the twin of the report container's catch, and it
		// has to say the same thing: `ApplyDataNode` refuses for reasons the user can act on, and
		// answering `false` in silence makes a file that will not open indistinguishable from one
		// that opened and failed later. A refusal is data, never a format string.
		ibJournalError(ibT("metadata.dp"),ibT("%s"), err.GetErrorDescription());
		return false;
	}
}

bool ibMetaDataDataTool::SaveRootTree(const ibClassID& clsid, ibWriterMemory& writerData, int flags)
{
	// Header (sign + version + guid) leads the tree blob (was the separate SaveHeader).
	{
		ibWriterMemory headerWriter;
		headerWriter.w_u64(sign_dataTool); //sign
		headerWriter.w_u32(m_version); // version 1 - DEFAULT
		headerWriter.w_stringZ(m_rootObject->GetDocPath()); //guid conf
		writerData.w_chunk(eHeaderBlock, headerWriter.pointer(), headerWriter.size());
	}

	// Top-level structure builder — BuildDataNode fills the root's clsid/metaId from the object.
	ibDataBuilder builder;
	if (!m_rootObject->BuildDataNode(builder.Root(), flags))
		return false;

	// Provider writes the root's INNER; frame it with the root identity — chunk(clsid){
	// chunk(metaId){ inner } } — exactly what LoadRootTree peels above. Frame the OUTER block
	// under the passed `clsid` — the EXTERNAL container class (MD_EDTL), fixed at the SaveToFile
	// call site — NOT the object's own GetClassType() (its BASE metadata kind, MD_DTL). This makes
	// the on-disk root class deterministic: every external file frames its tree under the external
	// clsid, so the loader knows exactly which clsid to peel and never has to guess base-vs-external.
	ibBinaryProvider provider;
	ibWriterMemory innerWriter;
	if (!builder.Save(provider, innerWriter))
		return false;

	ibWriterMemory metaWriter;
	metaWriter.w_chunk((std::uint64_t)builder.Root().GetMetaId(), innerWriter.pointer(), innerWriter.size());
	writerData.w_chunk((std::uint64_t)clsid, metaWriter.pointer(), metaWriter.size());

	// ⭐ …and that it has been written out. A watcher shows this as "no longer modified"; nothing
	// in the tree changed, which is why this is a stage of its own and not MetaDataChanged.
	MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Saved, GetRootMetaObject());
	return true;
}

bool ibMetaDataDataTool::DeleteRootTree(const ibClassID& clsid)
{
	// Deleted-node purge (runs during save/apply to drop IsDeleted nodes from
	// tree + DB) is owned by the node (ibValueMetaObject::DeleteSubtree).
	return m_rootObject->DeleteSubtree();
}