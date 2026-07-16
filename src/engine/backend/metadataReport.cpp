////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : external metaData - for dataProcessors, reports
////////////////////////////////////////////////////////////////////////////

#include "metadataReport.h"
#include "backend/appData.h"

ibMetaDataReport::ibMetaDataReport() : ibMetaData(),
m_commonObject(nullptr),
m_moduleManager(nullptr),
m_ownerMeta(nullptr),
m_version(version_oes_last)
{
	// Compile cache (designer mode only) is built by the image ctor via
	// CreateCompileCache() below — see ibMetaDataDataProcessor.

	//create main metaObject
	m_commonObject = new ibValueMetaObjectExternalReport();
	m_commonObject->SetName(
		ibMetaData::GetNewName(g_metaExternalReportCLSID, nullptr, m_commonObject->GetClassName())
	);

	if (m_commonObject->OnCreateMetaObject(this, newObjectFlag)) {
		if (!m_commonObject->OnLoadMetaObject(this)) {
			wxASSERT_MSG(false, "m_commonObject->OnLoadMetaObject() == false");
		}
	}

	m_commonObject->InvalidateNames();
	// m_commonObject is an ibValuePtr — the assignment above already holds the ref.

	// Runtime module manager for this external report, on the prepared root. Built
	// in the ctor so a freshly created (not-from-file) report already has it — the
	// designer "New report" path calls RunDatabase() directly, never through
	// LoadFromFile. LoadFromFile rebuilds it on the swapped-in root.
	m_moduleManager = new ibValueModuleRuntimeManagerExternalReport(this, m_commonObject);
	m_moduleManager->InvalidateNames();

	m_ownerMeta = this;
}

ibMetaDataReport::ibMetaDataReport(ibMetaData* metaData, ibValueMetaObjectReport* srcReport) : ibMetaData(),
m_commonObject(srcReport),
m_ownerMeta(nullptr),
m_moduleManager(nullptr),
m_version(version_oes_last)
{
	if (srcReport == nullptr) {
		ibValueMetaObject* commonMetaObject = metaData->GetCommonMetaObject();
		wxASSERT(commonMetaObject);
		//create main metaObject
		m_commonObject = new ibValueMetaObjectReport();
		m_commonObject->SetName(
			ibMetaData::GetNewName(g_metaReportCLSID, nullptr, m_commonObject->GetClassName())
		);
		if (commonMetaObject != nullptr) {
			m_commonObject->SetParent(commonMetaObject);
			commonMetaObject->AddChild(m_commonObject);
		}
	}

	// m_commonObject (ibValuePtr) holds the member ref; for the inner case AddChild
	// added the parent's own ref separately.
	m_commonObject->InvalidateNames();

	m_ownerMeta = metaData;
}

ibMetaDataReport::~ibMetaDataReport()
{
	if (m_commonObject->IsExternalCreate()) {

		// Release the manager first, while the root is still alive — the manager
		// references the root via m_objectValue, and its dtor runs DestroyMainModule
		// (RAII). The ibValuePtr assignment to nullptr is the release.
		m_moduleManager = nullptr;

		//clear data
		if (!ClearDatabase()) {
			wxASSERT_MSG(false, "ClearDatabase() == false");
		}

		// m_commonObject (ibValuePtr) releases the root after this body.
	}
}

ibValueMetaObjectReport* ibMetaDataReport::GetReport() const
{
	return m_commonObject; // ibValuePtr operator T*
}

const ibValueMetaObject* ibMetaDataReport::GetCommonMetaObject() const
{
	return m_commonObject; // ibValuePtr operator T* -> upcast to base
}

ibValueMetaObject* ibMetaDataReport::GetCommonMetaObject()
{
	return m_commonObject;
}

bool ibMetaDataReport::LoadDatabase()
{
	return RunDatabase();
}

bool ibMetaDataReport::SaveDatabase()
{
	return true;
}

bool ibMetaDataReport::ClearDatabase()
{
	// Full force-replace unload: just drop the tree — owning handles cascade the
	// destruction. No OnDeleteMetaObject cascade (metadata is wholly replaced;
	// reconciliation happens on the next load). ClearSubtree is available if a
	// caller ever wants to fire the delete events explicitly before a load.
	// keepPinned: predefined object/manager modules are bound to the root for life.
	m_commonObject->RemoveAllChildren(true);
	return true;
}

////////////////////////////////////////////////////////////////////

wxString ibMetaDataReport::GetLangCode() const
{
	return activeMetaData->GetLangCode();
}

////////////////////////////////////////////////////////////////////

std::unique_ptr<ibCompileValueCache> ibMetaDataReport::CreateDesignerCache()
{
	// Designer mode only; an external report additionally owns a module-manager (bound
	// to its object module). Called by the image ctor.
	if (!appData->DesignerMode())
		return nullptr;
	auto cache = std::make_unique<ibCompileValueCache>();
	if (m_commonObject->IsExternalCreate())
		cache->SetModuleManager(new ibValueModuleManagerDesigner(this, m_commonObject->GetObjectModule()));
	return cache;
}

bool ibMetaDataReport::RunDatabase(int flags)
{
	// RunSubtree fires the root's own OnBeforeRun/OnAfterRun + every descendant.
	// CreateMainModule/StartMainModule stay interleaved between the two phases.
	// Transactional like ibMetaDataConfigurationFile::RunDatabase via LoadGuard: it
	// CREATES the runtime image and drops it on ANY exit that isn't Commit() — a failed
	// return OR a raised exception (exception == rollback) — so a partial run leaves the
	// metadata closed (the load "never happened"). StartMainModule runs during the build
	// (it reads the compile cache through GetCompileCache() = the live image); Commit()
	// keeps the image only once it has succeeded, so a failed start rolls back cleanly.
	LoadGuard load(this);

	if (m_commonObject->IsExternalCreate()) {

		// The designer module-manager (for the report's compile cache) was already
		// built by the image ctor via CreateDesignerModuleManager() — common modules
		// register into it during RunSubtree(true) below.
		if (!m_commonObject->RunSubtree(flags, ibValueMetaObject::ibRunPhase::Before))
			return false;

		if (auto* cc = GetCompileCache()) {
			if (auto* mgr = cc->GetModuleManager())
				mgr->CreateMainModule();
		}

		if (m_moduleManager->CreateMainModule()) {
			if (!m_commonObject->RunSubtree(flags, ibValueMetaObject::ibRunPhase::After))
				return false;
			if (!m_moduleManager->StartMainModule())
				return false;
			load.Commit();   // keep the image → report is now open
			return true;
		}
		return false;
	}
	else if (!m_commonObject->IsExternalCreate()) {

		if (!m_commonObject->RunSubtree(flags, ibValueMetaObject::ibRunPhase::Before))
			return false;

		if (!m_commonObject->RunSubtree(flags, ibValueMetaObject::ibRunPhase::After))
			return false;

		load.Commit();
		return true;
	}

	return false;
}

bool ibMetaDataReport::CloseDatabase(int flags)
{
	wxASSERT(IsConfigOpen());

	if (!ExitMainModule((flags & forceCloseFlag) != 0))
		return false;

	// CloseSubtree closes every descendant then the root's own hook (bottom-up);
	// it self-skips a deleted node.
	if (!m_commonObject->CloseSubtree(ibValueMetaObject::ibRunPhase::Before))   // un-resolve
		return false;

	// Symmetric to RunDatabase — tear down + release the designer manager.
	if (auto* cc = GetCompileCache()) {
		if (auto* mgr = cc->GetModuleManager())
			mgr->DestroyMainModule();
		cc->SetModuleManager(nullptr);
	}

	if (!m_commonObject->CloseSubtree(ibValueMetaObject::ibRunPhase::After))    // un-register
		return false;

	m_image.reset();   // drop the runtime image ⇒ closed (frees ctors + modules + cache)
	return true;
}

#include <fstream>
#include <filesystem>

#include "backend/backend_exception.h"   // catch ibBackendException at the LoadCommonTree boundary
#include "backend/serialize/dataBuilder.h"  // ibDataBuilder / ibBinaryProvider — top-level structure builder

ibValueMetaObjectReport* ibMetaDataReport::BuildFreshRoot()
{
	// Mirror the ctor's external-root setup, minus the module manager — the
	// manager is a runtime concern (CreateObjectExtValue / CreateMainModule), not
	// touched by LoadSubtree, so it is re-created after the swap. Returned at
	// refcount 0 — the caller's ibValuePtr adopts it.
	auto* root = new ibValueMetaObjectExternalReport();
	root->SetName(ibMetaData::GetNewName(g_metaExternalReportCLSID, nullptr, root->GetClassName()));
	if (!root->OnCreateMetaObject(this, newObjectFlag)) {
		delete root; // refcount 0 — never IncrRef'd
		return nullptr;
	}
	if (!root->OnLoadMetaObject(this)) {
		wxASSERT_MSG(false, "BuildFreshRoot: OnLoadMetaObject() == false");
	}
	root->InvalidateNames();
	return root;
}

bool ibMetaDataReport::LoadFromFile(const wxString& strFileName)
{
	// Read the whole file up front — no tree is touched until the bytes are in
	// hand, so a read failure leaves the current report intact.
	std::ifstream in(strFileName.ToStdString(), std::ios::in | std::ios::binary);
	if (!in.is_open())
		return false;
	in.seekg(0, in.end);
	std::streamsize fsize = in.tellg();
	in.seekg(0, in.beg);
	wxMemoryBuffer tempBuffer(fsize);
	in.read((char*)tempBuffer.GetWriteBuf(fsize), fsize);
	in.close();

	ibReaderMemory readerData(tempBuffer.GetData(), tempBuffer.GetBufSize());
	if (readerData.eof())
		return false;

	m_fullPath = strFileName;

	// Inner (read object + copy into a config): the report is a child of the
	// configuration tree — load into the existing root, no swap. resetId=true
	// regenerates every loaded node's metaId from the config counter (m_ownerMeta,
	// stamped via OnCreateMetaObject) so the file's ids can't collide with existing
	// config objects; ResetAll then gives the root a fresh guid too (its ctor clsid
	// keys on guid). Without this the import re-registers a live clsid → "Object is
	// exist" → throw out of RunDatabase.
	if (!m_commonObject->IsExternalCreate()) {
		if (!m_commonObject->OnCreateMetaObject(m_ownerMeta, newObjectFlag))
			return false;
		if (!LoadCommonTree(m_commonObject, g_metaExternalReportCLSID, readerData, /*resetId*/ true))
			return false;
		m_commonObject->ResetAll();
		m_commonObject->BuildNewName();
		return LoadDatabase();
	}

	// External (start from file): standalone report — detached-root swap. Build a
	// fresh root and load into it; on failure the live report is untouched.
	ibValuePtr<ibValueMetaObjectReport> fresh(BuildFreshRoot()); // adopt (refcount 0 -> 1)
	if (!fresh)
		return false;
	if (!LoadCommonTree(fresh, g_metaExternalReportCLSID, readerData))
		return false; // fresh (ibValuePtr) discards the root automatically

	// Commit. Close the old tree's run-state first (unregisters its ctors). Then tear
	// down the old module manager BEFORE the swap — the ibValuePtr assignment releases
	// the old root, and the manager references it via m_objectValue (use-after-free if
	// the manager outlives the root). Then swap (the assignment releases old + adopts
	// fresh) and rebuild the manager on the fresh root (mirrors ctor / dtor ordering).
	if (IsConfigOpen() && !CloseDatabase(forceCloseFlag))
		return false; // fresh discarded automatically

	// Release the old manager first (its dtor runs DestroyMainModule via RAII) while
	// the old root is still alive — the manager references it via m_objectValue. Then
	// swap the root, then build the new manager on the fresh root.
	m_moduleManager = nullptr;

	m_commonObject = fresh; // ibValuePtr: release old root (DecrRef -> cascade), adopt fresh

	m_moduleManager = new ibValueModuleRuntimeManagerExternalReport(this, m_commonObject);
	m_moduleManager->InvalidateNames();

	return LoadDatabase();
}

bool ibMetaDataReport::SaveToFile(const wxString& strFileName)
{
	//common data
	ibWriterMemory writerData;

	m_fullPath = strFileName;

	//Save common object (header is written inside SaveCommonTree)
	if (!SaveCommonTree(g_metaExternalReportCLSID, writerData, saveConfigFlag))
		return false;

	//Delete common object
	if (!DeleteCommonTree(g_metaExternalReportCLSID))
		return false;

	// Atomic export: write to a sibling temp file, then rename over the target
	// (single commit point — a partial/failed write never replaces a good file).
	// std::filesystem (C++17), no wx.
	namespace fs = std::filesystem;
	const fs::path dstPath(strFileName.ToStdWstring());
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

bool ibMetaDataReport::LoadCommonTree(ibValueMetaObjectReport* root, const ibClassID& clsid, ibReaderMemory& readerData, bool resetId)
{
	// Header (sign + version + guid) leads the tree blob in the same stream — read
	// and validate it here so the common-tree blob stays self-describing (was the
	// separate LoadHeader).
	{
		ibReaderMemory* headerReader = readerData.open_chunk(eHeaderBlock);
		if (!headerReader)
			return false;
		if (headerReader->r_u64() != sign_dataReport)
			return false;
		m_version = headerReader->r_u32();
		wxString metaGuid;
		headerReader->r_stringZ(metaGuid);
		headerReader->close();
	}

	// The tree's data block is keyed by the root's GetClassType() AT SAVE TIME, which may be the
	// EXTERNAL container clsid (MD_ERPT, files saved now) or the BASE metadata clsid (MD_RPT, files
	// saved before the external kind was split out). Try the external clsid first, then fall back to
	// the base — one is what Save wrote. open_chunk returns an OWNED reader (shared_ptr-safe). (Same
	// fix as the DataProcessor reader.)
	(void)clsid;
	std::shared_ptr<ibReaderMemory> readerMemory(readerData.open_chunk(g_metaExternalReportCLSID));
	if (!readerMemory)
		readerMemory.reset(readerData.open_chunk(g_metaReportCLSID));

	if (!readerMemory)
		return false;

	u64 meta_id = 0;
	std::shared_ptr<ibReaderMemory> readerMetaMemory(readerMemory->open_chunk_iterator(meta_id));

	if (!readerMetaMemory)
		return true;

	// Parse the inner content into the universal structure tree, then apply into
	// the caller-provided root (the live tree for the inner case, a detached fresh
	// root for the external start-from-file swap). ApplyDataNode throws
	// ibBackendException on a factory miss / bad data — catch at this container
	// boundary and report false (the caller discards the fresh root).
	ibDataNode rootNode(root->GetClassType(), (ibMetaID)meta_id);
	ibBinaryProvider provider;
	provider.Read(*readerMetaMemory, rootNode);
	try {
		root->ApplyDataNode(rootNode, resetId);
		return true;
	}
	catch (const ibBackendException&) {
		return false;
	}
}

bool ibMetaDataReport::SaveCommonTree(const ibClassID& clsid, ibWriterMemory& writerData, int flags)
{
	// Header (sign + version + guid) leads the tree blob (was the separate SaveHeader).
	{
		ibWriterMemory headerWriter;
		headerWriter.w_u64(sign_dataReport); //sign
		headerWriter.w_u32(m_version); // version 1 - DEFAULT
		headerWriter.w_stringZ(m_commonObject->GetDocPath()); //guid conf
		writerData.w_chunk(eHeaderBlock, headerWriter.pointer(), headerWriter.size());
	}

	// Top-level structure builder — BuildDataNode fills the root's clsid/metaId from the object.
	ibDataBuilder builder;
	if (!m_commonObject->BuildDataNode(builder.Root(), flags))
		return false;

	// Provider writes the root's INNER; frame it with the root identity — chunk(clsid){
	// chunk(metaId){ inner } } — exactly what LoadCommonTree peels above. Frame the OUTER block
	// under the passed `clsid` — the EXTERNAL container class (MD_ERPT), fixed at the SaveToFile call
	// site — NOT the object's own GetClassType() (its BASE metadata kind, MD_RPT). This makes the
	// on-disk root class deterministic: the loader always peels the tree under the external clsid.
	ibBinaryProvider provider;
	ibWriterMemory innerWriter;
	if (!builder.Save(provider, innerWriter))
		return false;

	ibWriterMemory metaWriter;
	metaWriter.w_chunk((u64)builder.Root().GetMetaId(), innerWriter.pointer(), innerWriter.size());
	writerData.w_chunk((u64)clsid, metaWriter.pointer(), metaWriter.size());
	return true;
}

bool ibMetaDataReport::DeleteCommonTree(const ibClassID& clsid)
{
	// Deleted-node purge (runs during save/apply to drop IsDeleted nodes from
	// tree + DB) is owned by the node (ibValueMetaObject::DeleteSubtree).
	return m_commonObject->DeleteSubtree();
}