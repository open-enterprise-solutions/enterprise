////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : external metaData - for dataProcessors, reports
////////////////////////////////////////////////////////////////////////////

#include "metadataDataProcessor.h"
#include "backend/appData.h"

ibMetaDataDataProcessor::ibMetaDataDataProcessor() : ibMetaData(),
m_commonObject(nullptr),
m_moduleManager(nullptr),
m_ownerMeta(nullptr),
m_configOpened(false),
m_version(version_oes_last)
{
	// Designer-only: cache backs code-editor / form-preview / metadata-property
	// lookups via m_metaData->GetCompileCache() (codeEditorInterpreter,
	// metaFormObject etc.). At runtime cache must stay null — StartMainModule's
	// "show default form" branch is gated by !cc and would skip ShowForm if the
	// cache held the deferred entry.
	if (appData->DesignerMode())
		m_compileCache = std::make_unique<ibCompileValueCache>();

	//create main metaObject
	m_commonObject = new ibValueMetaObjectExternalDataProcessor;
	m_commonObject->SetName(
		ibMetaData::GetNewName(g_metaExternalDataProcessorCLSID, nullptr, m_commonObject->GetClassName())
	);

	if (m_commonObject->OnCreateMetaObject(this, newObjectFlag)) {
		if (!m_commonObject->OnLoadMetaObject(this)) {
			wxASSERT_MSG(false, "m_commonObject->OnLoadMetaObject() == false");
		}
	}

	m_commonObject->InvalidateNames();
	// m_commonObject is an ibValuePtr — the assignment above already holds the ref.

	// Runtime module manager for this external DP, on the prepared root. Built in
	// the ctor so a freshly created (not-from-file) DP already has it — the
	// designer "New data processor" path calls RunDatabase() directly, never
	// through LoadFromFile (which would otherwise be the only place it's built).
	// LoadFromFile rebuilds it on the swapped-in root.
	m_moduleManager = new ibValueModuleRuntimeManagerExternalDataProcessor(this, m_commonObject);
	m_moduleManager->InvalidateNames();

	m_ownerMeta = this;
}

ibMetaDataDataProcessor::ibMetaDataDataProcessor(ibMetaData* metaData, ibValueMetaObjectDataProcessor* srcDataProcessor) : ibMetaData(),
m_commonObject(srcDataProcessor),
m_moduleManager(nullptr),
m_ownerMeta(nullptr),
m_configOpened(false),
m_version(version_oes_last)
{
	if (srcDataProcessor == nullptr) {
		ibValueMetaObject* commonMetaObject = metaData->GetCommonMetaObject();
		wxASSERT(commonMetaObject);
		//create main metaObject
		m_commonObject = new ibValueMetaObjectDataProcessor();
		m_commonObject->SetName(
			ibMetaData::GetNewName(g_metaDataProcessorCLSID, nullptr, m_commonObject->GetClassName())
		);
		if (commonMetaObject != nullptr) {
			m_commonObject->SetParent(commonMetaObject);
			commonMetaObject->AddChild(m_commonObject);
		}
	}

	m_commonObject->InvalidateNames();
	// m_commonObject (ibValuePtr) holds the member ref; for the inner case AddChild
	// added the parent's own ref separately.

	m_ownerMeta = metaData;
}

ibMetaDataDataProcessor::~ibMetaDataDataProcessor()
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

ibValueMetaObjectDataProcessor* ibMetaDataDataProcessor::GetDataProcessor() const
{
	return m_commonObject; // ibValuePtr operator T*
}

const ibValueMetaObject* ibMetaDataDataProcessor::GetCommonMetaObject() const
{
	return m_commonObject; // ibValuePtr operator T* -> upcast to base
}

ibValueMetaObject* ibMetaDataDataProcessor::GetCommonMetaObject()
{
	return m_commonObject;
}

bool ibMetaDataDataProcessor::LoadDatabase()
{
	return RunDatabase();
}

bool ibMetaDataDataProcessor::SaveDatabase()
{
	return true;
}

bool ibMetaDataDataProcessor::ClearDatabase()
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

wxString ibMetaDataDataProcessor::GetLangCode() const
{
	return activeMetaData->GetLangCode();
}

////////////////////////////////////////////////////////////////////

bool ibMetaDataDataProcessor::RunDatabase(int flags)
{
	// RunSubtree fires the root's own OnBeforeRun/OnAfterRun + every descendant.
	// CreateMainModule/StartMainModule stay interleaved between the two phases.
	if (m_commonObject->IsExternalCreate()) {

		// Lightweight DESIGNER module manager for this external DP's own compile
		// cache — same role as the configuration's, so the code editor inside the
		// DP's module sees the Manager singleton + ctor-context + common modules.
		// Created BEFORE RunSubtree so the DP's common modules register into it.
		if (auto* cc = GetCompileCache())
			cc->SetModuleManager(new ibValueModuleManagerDesigner(this, m_commonObject->GetObjectModule()));

		// Any failure below leaves CloseDatabase un-called (m_configOpened false), so
		// drop the designer manager on the bail-out path or its stale common-module
		// registrations would dangle into the next RunDatabase.
		auto bail = [this]() -> bool {
			if (auto* cc = GetCompileCache())
				cc->SetModuleManager(nullptr);
			return false;
		};

		if (!m_commonObject->RunSubtree(flags, true)) {
			return bail();
		}

		if (auto* cc = GetCompileCache()) {
			if (auto* mgr = cc->GetModuleManager())
				mgr->CreateMainModule();
		}

		if (m_moduleManager->CreateMainModule()) {
			if (!m_commonObject->RunSubtree(flags, false)) {
				return bail();
			}
			m_configOpened = true;
			if (!m_moduleManager->StartMainModule()) {
				return bail();
			}
			return true;
		}
		return bail();
	}
	else if (!m_commonObject->IsExternalCreate()) {

		if (!m_commonObject->RunSubtree(flags, true)) {
			return false;
		}
		if (!m_commonObject->RunSubtree(flags, false)) {
			return false;
		}

		return true;
	}

	return false;
}

bool ibMetaDataDataProcessor::CloseDatabase(int flags)
{
	wxASSERT(m_configOpened);

	if (!ExitMainModule((flags & forceCloseFlag) != 0))
		return false;

	// CloseSubtree closes every descendant then the root's own hook (bottom-up);
	// it self-skips a deleted node.
	if (!m_commonObject->CloseSubtree(true))
		return false;

	// Symmetric to RunDatabase — tear down + release the designer manager (its
	// object module is about to be reset/freed; dropping it avoids a dangle).
	if (auto* cc = GetCompileCache()) {
		if (auto* mgr = cc->GetModuleManager())
			mgr->DestroyMainModule();
		cc->SetModuleManager(nullptr);
	}

	if (!m_commonObject->CloseSubtree(false))
		return false;
	m_configOpened = false;
	return true;
}

#include <fstream>
#include <filesystem>

#include "backend/backend_exception.h"   // catch ibBackendException at the LoadCommonTree boundary

ibValueMetaObjectDataProcessor* ibMetaDataDataProcessor::BuildFreshRoot()
{
	// Mirror the ctor's external-root setup, minus the module manager — the
	// manager is a runtime concern (CreateObjectExtValue / CreateMainModule), not
	// touched by LoadSubtree, so it is re-created after the swap. Returned at
	// refcount 0 — the caller's ibValuePtr adopts it.
	auto* root = new ibValueMetaObjectExternalDataProcessor;
	root->SetName(ibMetaData::GetNewName(g_metaExternalDataProcessorCLSID, nullptr, root->GetClassName()));
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

bool ibMetaDataDataProcessor::LoadFromFile(const wxString& strFileName)
{
	// Read the whole file up front — no tree is touched until the bytes are in
	// hand, so a read failure leaves the current data processor intact.
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

	// Inner (read object + copy into a config): the data processor is a child of
	// the configuration tree — load into the existing root, no swap. resetId=true
	// regenerates every loaded node's metaId from the config counter (m_ownerMeta,
	// stamped via OnCreateMetaObject) so the file's ids can't collide with existing
	// config objects; ResetAll then gives the root a fresh guid too (its ctor clsid
	// keys on guid). Without this the import re-registers a live clsid → "Object is
	// exist" → throw out of RunDatabase.
	if (!m_commonObject->IsExternalCreate()) {
		if (!m_commonObject->OnCreateMetaObject(m_ownerMeta, newObjectFlag))
			return false;
		if (!LoadCommonTree(m_commonObject, g_metaExternalDataProcessorCLSID, readerData, /*resetId*/ true))
			return false;
		m_commonObject->ResetAll();
		m_commonObject->BuildNewName();
		return LoadDatabase();
	}

	// External (start from file): standalone data processor — detached-root swap.
	// Build a fresh root and load into it; on failure the live processor is
	// untouched (all-or-nothing).
	ibValuePtr<ibValueMetaObjectDataProcessor> fresh(BuildFreshRoot()); // adopt (refcount 0 -> 1)
	if (!fresh)
		return false;
	if (!LoadCommonTree(fresh, g_metaExternalDataProcessorCLSID, readerData))
		return false; // fresh (ibValuePtr) discards the root automatically

	// Commit. Close the old tree's run-state first (unregisters its ctors). Then tear
	// down the old module manager BEFORE the swap — the ibValuePtr assignment releases
	// the old root, and the manager references it via m_objectValue (use-after-free if
	// the manager outlives the root). Then swap (the assignment releases old + adopts
	// fresh) and rebuild the manager on the fresh root (mirrors ctor / dtor ordering).
	if (m_configOpened && !CloseDatabase(forceCloseFlag))
		return false; // fresh discarded automatically

	// Release the old manager first (its dtor runs DestroyMainModule via RAII) while
	// the old root is still alive — the manager references it via m_objectValue. Then
	// swap the root, then build the new manager on the fresh root.
	m_moduleManager = nullptr;

	m_commonObject = fresh; // ibValuePtr: release old root (DecrRef -> cascade), adopt fresh

	m_moduleManager = new ibValueModuleRuntimeManagerExternalDataProcessor(this, m_commonObject);
	m_moduleManager->InvalidateNames();

	return LoadDatabase();
}

bool ibMetaDataDataProcessor::SaveToFile(const wxString& strFileName)
{
	//common data
	ibWriterMemory writerData;

	m_fullPath = strFileName;

	//Save common object (header is written inside SaveCommonTree)
	if (!SaveCommonTree(g_metaExternalDataProcessorCLSID, writerData, saveConfigFlag))
		return false;

	//Delete common object
	if (!DeleteCommonTree(g_metaExternalDataProcessorCLSID))
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

bool ibMetaDataDataProcessor::LoadCommonTree(ibValueMetaObjectDataProcessor* root, const ibClassID& clsid, ibReaderMemory& readerData, bool resetId)
{
	// Header (sign + version + guid) leads the tree blob in the same stream — read
	// and validate it here so the common-tree blob stays self-describing (was the
	// separate LoadHeader).
	{
		ibReaderMemory* headerReader = readerData.open_chunk(eHeaderBlock);
		if (!headerReader)
			return false;
		if (headerReader->r_u64() != sign_dataProcessor)
			return false;
		m_version = headerReader->r_u32();
		wxString metaGuid;
		headerReader->r_stringZ(metaGuid);
		headerReader->close();
	}

	std::shared_ptr<ibReaderMemory> readerMemory(readerData.open_chunk(clsid));

	if (!readerMemory)
		return false;

	u64 meta_id = 0;
	std::shared_ptr<ibReaderMemory> readerMetaMemory(readerMemory->open_chunk_iterator(meta_id));

	if (!readerMetaMemory)
		return true;

	// Load into the caller-provided root (the live tree for the inner case, a
	// detached fresh root for the external start-from-file swap). LoadSubtree
	// throws ibBackendException on a malformed chunk / factory miss — catch at this
	// container boundary and report false (the caller discards the fresh root).
	try {
		root->LoadSubtree(*readerMetaMemory, resetId);
		return true;
	}
	catch (const ibBackendException&) {
		return false;
	}
}

bool ibMetaDataDataProcessor::SaveCommonTree(const ibClassID& clsid, ibWriterMemory& writerData, int flags)
{
	// Header (sign + version + guid) leads the tree blob (was the separate SaveHeader).
	{
		ibWriterMemory headerWriter;
		headerWriter.w_u64(sign_dataProcessor); //sign
		headerWriter.w_u32(m_version); // version 1 - DEFAULT
		headerWriter.w_stringZ(m_commonObject->GetDocPath()); //guid conf
		writerData.w_chunk(eHeaderBlock, headerWriter.pointer(), headerWriter.size());
	}

	// Tree serialization is owned by the node (ibValueMetaObject::SaveSubtree).
	return m_commonObject->SaveSubtree(clsid, writerData, flags);
}

bool ibMetaDataDataProcessor::DeleteCommonTree(const ibClassID& clsid)
{
	// Deleted-node purge (runs during save/apply to drop IsDeleted nodes from
	// tree + DB) is owned by the node (ibValueMetaObject::DeleteSubtree).
	return m_commonObject->DeleteSubtree();
}