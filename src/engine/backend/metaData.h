#ifndef __METADATA_H__
#define __METADATA_H__

#include <atomic>   // std::atomic — MSVC supplied this transitively
#include <map>
#include <memory>
#include <optional>
#include <functional>
#include <vector>

#include "backend/moduleManager/moduleManager.h"
#include "backend/value_ptr.h"
#include "backend/ctorRegistry.h"
#include "backend/restructureInfo.h"   // ibRestructureInfo — per-metadata restructure ledger (member below)

///////////////////////////////////////////////////////////////////////////////
class BACKEND_API ibBackendMetadataTree;
class BACKEND_API ibValueMetaObjectCommonModule;
class BACKEND_API ibValueMetaObjectModuleBase;
class BACKEND_API ibValueMetaObjectGenericData;
class BACKEND_API ibValueMetaObjectFormBase;
///////////////////////////////////////////////////////////////////////////////
class BACKEND_API ibCtorMetaValueType;
class BACKEND_API ibMetaData;
///////////////////////////////////////////////////////////////////////////////

// Module-storage skeleton — list of common-module descriptors that
// belong to the metadata. Designer-mutation API (Add/Rename/Remove) +
// read-only iteration for runtime mm. One instance per metadata; held
// by value on ibMetaData so the accessor never returns nullptr (empty
// storage is the default for metadata kinds without init modules).
class BACKEND_API ibModuleStorage {
public:
	bool AddCommonModule(ibValueMetaObjectCommonModule* commonModule);
	bool RenameCommonModule(ibValueMetaObjectCommonModule* commonModule, const wxString& newName);
	bool RemoveCommonModule(ibValueMetaObjectCommonModule* commonModule);

	// Drop every registration — used on the RunDatabase bail-out path, where
	// some common modules already registered (in OnBeforeRunMetaObject) before the
	// failure, CloseDatabase won't run (m_configOpened stays false), and the next
	// load rebuilds the metaobject tree out from under these raw pointers.
	void Clear() { m_initModules.clear(); }

	const std::vector<ibValueMetaObjectCommonModule*>& GetCompileModules() const { return m_initModules; }

private:
	std::vector<ibValueMetaObjectCommonModule*> m_initModules;
};

///////////////////////////////////////////////////////////////////////////////

// ibDeferredForm (the lazy form-construction marker stored in the compile-value cache) now lives in
// metaFormObject.h — it is only used there, and its constructor reads IsPasteMode off the complete form type.

///////////////////////////////////////////////////////////////////////////////

// Compile-value cache — stores compiled ibValue pointers for designer's
// intellisense / metadata-property previews. Created only on metadata
// configurations that support designer editing (ibMetaDataConfigurationStorage);
// runtime configurations leave m_compileCache nullptr → callsites use
// `if (auto* cc = metaData->GetCompileCache())` instead of DesignerMode().
//
// Two AddCompileModule overloads share storage:
//   - (meta, ibValue*)        — caller already has the compiled value
//                               (catalog/document/register/manager modules).
//                               Stored as immediate; Find returns directly.
//   - (meta, ibDeferredForm)  — caller can't build eagerly (forms whose
//                               compile module needs the session's root
//                               mm ready). Stored with a rebuilder
//                               descriptor; Find lazily Constructs on
//                               first lookup and caches the result while
//                               keeping the rebuilder alive for later
//                               invalidations.
//
// FindCompileModuleRef is const but mutates the cache via mutable storage —
// lazy materialization replaces deferred entries with built ibValue without
// changing the API contract for callers.
class BACKEND_API ibCompileValueCache {
public:
	// Designer-own module manager (the pre-session-split config-level mm the
	// Designer lost when runtime moved into per-session ibSession::m_root). Held
	// here so the editor reads common-module units + named context from a manager
	// that tracks the CURRENT designer state, decoupled from the runtime session's
	// CreateMainModule timing. Owned by this cache. See project_common_module_designer_cache.
	explicit ibCompileValueCache(ibValueModuleManagerDesigner* moduleManager = nullptr);

	// Out-of-line (metadata.cpp): the ibValuePtr<ibValueModuleManagerDesigner>
	// assign/convert needs the COMPLETE designer type for its ref-count static_cast.
	ibValueModuleManagerDesigner* GetModuleManager() const;
	// Rebind the designer manager — driven by RunDatabase (fresh metaobject) /
	// CloseDatabase (nullptr). The manager binds to the current common metaobject;
	// holding one across a config reload would dangle (the old metaobject is freed).
	void SetModuleManager(ibValueModuleManagerDesigner* moduleManager);

	bool AddCompileModule(const ibValueMetaObject* moduleObject, ibValue* object);
	// Generalized deferred: the builder is Constructed lazily on first lookup.
	// Forms wrap ibDeferredForm (needs the session root mm compiled). Common
	// modules no longer use this path — they register into the designer-mgr
	// directly via RuntimeRegisterCommonModule (see project_common_module_designer_cache).
	// insert_or_assign semantics (a re-run replaces the prior builder + drops
	// the built value).
	bool AddCompileModule(const ibValueMetaObject* moduleObject, std::function<ibValue*()> builder);
	bool RemoveCompileModule(const ibValueMetaObject* moduleObject);

	// Mark a deferred entry as dirty — Designer calls this on form-edit
	// commits so the next Find rebuilds the form value via the stored
	// rebuilder. No-op for entries that were registered as immediate
	// values (no rebuilder), and no-op for unknown descriptors.
	bool InvalidateCompileModule(const ibValueMetaObject* moduleObject);

	ibValue* FindCompileModuleRef(const ibValueMetaObject* moduleObject) const;
	ibValue* FindParentCompileModuleRef(const ibValueMetaObject* moduleObject) const;

	template <class T>
	bool FindCompileModule(const ibValueMetaObject* moduleObject, T*& objValue) const {
		objValue = dynamic_cast<T*>(FindCompileModuleRef(moduleObject));
		return objValue != nullptr;
	}

	template <class T>
	bool FindParentCompileModule(const ibValueMetaObject* moduleObject, T*& objValue) const {
		objValue = dynamic_cast<T*>(FindParentCompileModuleRef(moduleObject));
		return objValue != nullptr;
	}

private:
	struct ibCompileEntry {
		ibValuePtr<ibValue>           m_value;     // built value; null if pending or invalidated
		std::function<ibValue*()>     m_deferred;  // rebuilder; set for lazy entries (forms / common modules)
		bool                          m_constructing = false; // recursion guard for FindCompileModuleRef
	};
	mutable std::map<const ibValueMetaObject*, ibCompileEntry> m_cache;

	// Designer-own module manager — owning handle (ibValuePtr: IncrRef on bind,
	// DecrRef on cache destruction). Null until ibMetaDataConfigurationStorage
	// wires one in. Designer-only type: holds its OWN common-module registry with
	// compiled lightweight units, independent of the runtime managers.
	ibValuePtr<ibValueModuleManagerDesigner> m_moduleManager;
};

///////////////////////////////////////////////////////////////////////////////

// Runtime-image — aggregates everything that "opening" a metadata fills and
// "closing" discards: the metadata-defined type-ctor factory, the common-module
// skeleton, and the (designer-only) compile cache. Held by shared_ptr on ibMetaData
// (m_image): its PRESENCE is the open state — a run builds it (LoadGuard) and keeps
// it on success or drops it on failure ("the load never happened"); close tears the
// registered objects down per-node, then drops it. The registry OWNS its ctors via
// shared_ptr, so dropping the image frees them — no manual cleanup. Non-copyable /
// non-movable: lifetime is managed only through the shared_ptr.
//
// m_factoryCtorCountChanges (the monotonic invalidation counter) deliberately stays
// OUTSIDE the image, on ibMetaData — dropping the image must not reset it.
class ibQueryableFactory;         // per-config source factory lives on the snapshot (forward-decl: unique_ptr member)
class ibMetaQueryableFactory;     // the concrete per-config factory (own registry → global fallback), allocated out of line

class BACKEND_API ibMetaImage {
public:
	// The image builds its OWN designer infrastructure on construction (out of line —
	// see metadataFactory.cpp): it asks the owner metadata for its designer cache via
	// CreateDesignerCache() (the compile-value cache WITH its module-manager attached;
	// nullptr for runtime / non-designer kinds). So each metadata kind decides its own
	// designer setup; the caller (LoadGuard) never pokes the image's internals.
	// owner==nullptr ⇒ a bare image (the ctor-factory + module skeleton fill during the
	// run cascade, not here).
	explicit ibMetaImage(ibMetaData* owner = nullptr);
	// Dtor is OUT OF LINE (metadataFactory.cpp): m_sourceFactory is a unique_ptr to a forward-declared
	// ibQueryableFactory, so its deleter needs the complete type there, not in this header. m_factoryCtors
	// (ibCtorRegistry) frees its ctors via type-erased shared_ptr deleters; module storage is non-owning; the
	// compile cache + source factory are unique_ptrs (free themselves).
	~ibMetaImage();
	// Managed only through shared_ptr (install / swap) — never copied or moved.
	ibMetaImage(const ibMetaImage&) = delete;
	ibMetaImage& operator=(const ibMetaImage&) = delete;
	ibMetaImage(ibMetaImage&&) = delete;
	ibMetaImage& operator=(ibMetaImage&&) = delete;

	// --- ctor-factory facade ---
	// The operations on m_factoryCtors, exposed as image methods so ibMetaData (and its
	// DataProcessor / Report subclasses) call `m_image->X` (guarding m_image for the
	// closed state) instead of reaching into m_factoryCtors directly. Register/Unregister
	// Ctor pair the ctor's lifecycle event with the registry mutation; the registry owns
	// the ctor via shared_ptr (UnregisterCtor frees it). RegisterCtor / UnregisterCtor /
	// FindCtor(metaValue) deref ibCtorMetaValueType, so they're defined out of line in
	// metadataFactory.cpp (complete type via objCtor.h). The clsid/name lookups and
	// ForEachCtor only move pointers, so they stay inline here.
	void RegisterCtor(ibCtorMetaValueType* typeCtor);     // CallEvent(Register)   + register (takes ownership)
	void UnregisterCtor(ibCtorMetaValueType* typeCtor);   // CallEvent(UnRegister) + unregister (frees it)
	ibCtorMetaValueType* FindCtor(const ibValueMetaObject* metaValue, ibCtorObjectMetaType refType) const;

	// One template per single-key lookup — the registry resolves the key overload
	// (clsid O(1) / type_info O(1) / name linear), so clsid / wxString / std::type_info
	// all go through one method instead of an overload per key. (Distinct arity from the
	// two-arg (metaValue, refType) overload above — no ambiguity.)
	template <typename Key> ibCtorMetaValueType* FindCtor(const Key& key) const { return m_factoryCtors.Find(key); }
	template <typename Key> bool                 HasCtor(const Key& key)  const { return m_factoryCtors.Find(key) != nullptr; }
	template <typename Fn>   void                ForEachCtor(Fn&& fn)     const { m_factoryCtors.ForEach(std::forward<Fn>(fn)); }

	// Module-storage + compile-cache accessors. ibMetaData exposes these as a facade
	// (GetModuleStorage / GetCompileCache), null-checking the image for the closed
	// state; on a live image module storage is always valid, the compile cache nullptr
	// unless the metadata's CreateCompileCache() made one (designer kinds).
	ibModuleStorage*       ModuleStorage()       { return &m_moduleStorage; }
	const ibModuleStorage* ModuleStorage() const { return &m_moduleStorage; }
	ibCompileValueCache*   CompileCache()  const { return m_compileCache.get(); }

	// This config's OWN source factory (metadata-backed queryables register here; resolve descends to the global
	// factory on a miss). Allocated in the image ctor. The base ptr is enough for callers (they use the virtual
	// Resolve*). See ibMetaData::GetSourceFactory facade.
	ibQueryableFactory*    SourceFactory() const { return m_sourceFactory.get(); }

private:

	// clsid O(1); name / (metaValue,refType) linear (see ctorRegistry.h). type_info
	// index stays empty (ibCtorMetaValueType has no concrete typeid — metaobjects
	// self-id via their own GetClassType() override, not typeid).
	ibCtorRegistry<ibCtorMetaValueType>  m_factoryCtors;
	// Common-module skeleton — populated by descriptor's OnBeforeRunMetaObject
	// during RunDatabase. Empty for metadata kinds without init modules.
	ibModuleStorage                      m_moduleStorage;
	// Compile-value cache — designer-only. Allocated by ibMetaDataConfigurationStorage's
	// ctor (and the external DP / report ctors); nullptr on runtime configurations.
	std::unique_ptr<ibCompileValueCache> m_compileCache;
	// Per-config source factory (ibMetaQueryableFactory) — this snapshot's OWN metadata-backed queryable descriptors;
	// resolve descends to the global factory on a miss. Allocated in the image ctor (out of line).
	std::unique_ptr<ibQueryableFactory> m_sourceFactory;
};

///////////////////////////////////////////////////////////////////////////////

class BACKEND_API ibMetaData {
	void DoGenerateNewID(ibMetaID& id, const ibValueMetaObject* top) const;
public:

	ibMetaData() :
		m_metaModify(false),
		m_metaTree(nullptr) {
	}

	virtual ~ibMetaData() {}

	// Module-storage skeleton — list of common-module descriptors that
	// runtime mm reads in CreateMainModule to spawn its own instances.
	// Always non-null (empty for metadata kinds without init modules,
	// e.g. external data processor / report).
	// Facade over the image; null when closed (no image) — callers touch module
	// storage only while open (run / close cascades, runtime mm bring-up).
	ibModuleStorage* GetModuleStorage() { return m_image ? m_image->ModuleStorage() : nullptr; }
	const ibModuleStorage* GetModuleStorage() const { return m_image ? m_image->ModuleStorage() : nullptr; }

	// Compile-value cache — designer-only storage of compiled ibValue
	// pointers used by intellisense / metadata-property previews. Created
	// only on metadata configurations that support designer editing
	// (ibMetaDataConfigurationStorage's ctor allocates it). Runtime-only
	// configurations leave it nullptr — callers gate by null-check
	// (`if (auto* cc = metaData->GetCompileCache())`) instead of querying
	// appData->DesignerMode().
	ibCompileValueCache* GetCompileCache() const { return m_image ? m_image->CompileCache() : nullptr; }

	// This config's OWN source factory (on the snapshot; null when closed). ONLY metadata-backed sources register here
	// now — the global (application-data) factory is empty and is a FUTURE plugin seam (the per-config factory descends
	// to it on a resolve miss). Every entry goes THROUGH metadata: registration via RegisterSource below, resolve via
	// this factory. Callers get the factory from the metadata they run on behalf of — never appData / active metadata.
	class ibQueryableFactory* GetSourceFactory() const { return m_image ? m_image->SourceFactory() : nullptr; }

	// Register / unregister a metadata-backed source descriptor into THIS config's OWN factory. The metaobject calls it
	// on run / close (GetMetaData()->RegisterSource(&m_queryable)) — each config keeps its own set of queryables. Out
	// of line (metaData.cpp): keeps ibQueryableFactory out of the metaobject TUs. const — the factory, not the metadata,
	// is mutated (the metadata is a stable identity; its snapshot holds the mutable registry).
	void RegisterSource(class ibQueryableSourceDescriptor* descriptor) const;
	void UnregisterSource(class ibQueryableSourceDescriptor* descriptor) const;

	// Factory for this metadata kind's designer infrastructure — called by the image
	// ctor, which takes ownership (no bool flag on the metadata). Base returns none
	// (runtime / non-designer kinds); the designer kinds override to build the
	// compile-value cache WITH its module-manager already attached (Storage always;
	// external DP / report in designer mode — manager bound to the common metaobject /
	// object module). One method ⇒ each kind's whole designer setup lives in one place.
	virtual std::unique_ptr<ibCompileValueCache> CreateDesignerCache() { return nullptr; }

	// Open state — single source of truth, lives in the open-image (see ibMetaImage).
	// Replaces the per-subclass m_configOpened on File / DataProcessor / Report; a
	// metadata that never opens just keeps the default false. Virtual so existing
	// `activeMetaData->IsConfigOpen()` call sites resolve here unchanged.
	virtual bool IsConfigOpen() const { return m_image != nullptr; }

	// (The restructure ledger lives on ibMetaDataConfigurationBase, not here — only a CONFIGURATION
	//  restructures; an external data-processor / report metadata never does. Reach it through the static
	//  ibMetaDataConfigurationBase::GetRestructureInfo(), which pulls the ACTIVE config's ledger.)

	virtual bool IsModified() const { return m_metaModify; }
	virtual void Modify(bool modify = true) {
		if (m_metaTree != nullptr)
			m_metaTree->Modify(modify);
		m_metaModify = modify;
	}

	virtual void SetVersion(const ibVersionID& version) = 0;
	virtual ibVersionID GetVersion() const = 0;

	virtual wxString GetFileName() const { return wxEmptyString; }
	virtual const ibValueMetaObject* GetCommonMetaObject() const { return nullptr; }
	virtual ibValueMetaObject* GetCommonMetaObject() { return nullptr; }

	// THE WHOLE STRUCTURE THIS CONFIGURATION DECLARES — every table of every
	// metaobject in it, in one snapshot.
	//
	// One door, because there is one answer. Five callers used to reach for the
	// common object and ask it themselves (the DDL differ, the full rebuild, the
	// config dump and its restore, the totals fold), which is four chances to ask
	// a slightly different question — and the fold, which walks what it is given,
	// would simply have folded less. Nothing open (no configuration) yields an
	// empty snapshot, which every caller already treats as "nothing to do".
	//
	// Out-of-line (metaData.cpp) — ibSchemaSnapshot is only forward-declared here.
	class ibSchemaSnapshot BuildSchemaSnapshot() const;

	//runtime support:
	inline ibValue CreateObject(const ibClassID& clsid, ibValue** paParams = nullptr, const long lSizeArray = 0) const {
		return CreateObjectRef(clsid, paParams, lSizeArray);
	}
	inline ibValue CreateObject(const wxString& className, ibValue** paParams = nullptr, const long lSizeArray = 0) const {
		return CreateObjectRef(className, paParams, lSizeArray);
	}

	template<typename T, typename... Args>
	inline ibValue CreateObjectValue(Args&&... args) const {
		return CreateObjectValueRef<T>(std::forward<Args>(args)...);
	}

	virtual ibValue* CreateObjectRef(const ibClassID& clsid, ibValue** paParams = nullptr, const long lSizeArray = 0) const;
	virtual ibValue* CreateObjectRef(const wxString& className, ibValue** paParams = nullptr, const long lSizeArray = 0) const {
		const ibClassID& clsid = GetIDObjectFromString(className);
		return CreateObjectRef(clsid, paParams, lSizeArray);
	}
	template<typename T, typename... Args>
	inline ibValue* CreateObjectValueRef(Args&&... args) const {
		return CreateAndConvertObjectValueRef<T>(std::forward<Args>(args)...);
	}

	template<class T = ibValue>
	inline T* CreateAndConvertObjectRef(const ibClassID& clsid, ibValue** paParams = nullptr, const long lSizeArray = 0) const {
		return CastValue<T>(CreateObjectRef(clsid, paParams, lSizeArray));
	}
	template<class T = ibValue>
	inline T* CreateAndConvertObjectRef(const wxString& className, ibValue** paParams = nullptr, const long lSizeArray = 0) const {
		return CastValue<T>(CreateObjectRef(className, paParams, lSizeArray));
	}
	template<typename T, typename... Args>
	inline T* CreateAndConvertObjectValueRef(Args&&... args) const {
		T* created_value = new T(args...);
		if (!IsRegisterCtor(created_value->GetClassType())) {
			wxDELETE(created_value);
			wxASSERT_MSG(false, "CreateAndConvertObjectValueRef ret null!");
			return nullptr;
		}
		return created_value;
	}

	void RegisterCtor(ibCtorMetaValueType* typeCtor);
	void UnRegisterCtor(ibCtorMetaValueType*& typeCtor);

	void UnRegisterCtor(const wxString& className);

	virtual bool IsRegisterCtor(const wxString& className) const;
	virtual bool IsRegisterCtor(const wxString& className, ibCtorObjectType objectType) const;
	virtual bool IsRegisterCtor(const wxString& className, ibCtorObjectType objectType, enum ibCtorObjectMetaType metaType) const;

	virtual bool IsRegisterCtor(const ibClassID& clsid) const;

	virtual ibClassID GetIDObjectFromString(const wxString& className) const;
	virtual wxString GetNameObjectFromID(const ibClassID& clsid, bool upper = false) const;

	inline ibMetaID GetVTByID(const ibClassID& clsid) const;
	inline ibClassID GetIDByVT(const ibMetaID& valueType, enum ibCtorObjectMetaType refType) const;

	virtual ibCtorMetaValueType* GetTypeCtor(const wxString& className) const;
	virtual ibCtorMetaValueType* GetTypeCtor(const ibClassID& clsid) const;
	virtual ibCtorMetaValueType* GetTypeCtor(const ibValueMetaObject* metaValue, enum ibCtorObjectMetaType refType) const;

	virtual ibCtorAbstractType* GetAvailableCtor(const wxString& className) const;
	virtual ibCtorAbstractType* GetAvailableCtor(const ibClassID& clsid) const;

	virtual std::vector<ibCtorMetaValueType*> GetListCtorsByType() const;
	virtual std::vector<ibCtorMetaValueType*> GetListCtorsByType(const ibClassID& clsid, enum ibCtorObjectMetaType refType) const;
	virtual std::vector<ibCtorMetaValueType*> GetListCtorsByType(enum ibCtorObjectMetaType refType) const;

	//get parent metadata 
	virtual bool GetOwner(ibMetaData*& metaData) const { return false; }

	//factory version 
	virtual unsigned int GetFactoryCountChanges() const {
		return m_factoryCtorCountChanges + ibValue::GetFactoryCountChanges();
	}

	//get language code 
	virtual wxString GetLangCode() const = 0;

	//Check is full access 
	virtual bool IsFullAccess() const { return true; }

	//associate this metaData with the designer's metadata tree (null outside the designer)
	virtual ibBackendMetadataTree* GetMetaTree() const { return m_metaTree; }
	virtual void SetMetaTree(ibBackendMetadataTree* metaTree) { m_metaTree = metaTree; }

	//run/close 
	virtual bool RunDatabase(int flags = defaultFlag) = 0;
	virtual bool CloseDatabase(int flags = defaultFlag) = 0;

	//metaobject
	ibValueMetaObject* CreateMetaObject(const ibClassID& clsid,
		ibValueMetaObject* parentMetaObj, bool runObject = true);

	bool RenameMetaObject(ibValueMetaObject* object, const wxString& newName);
	void RemoveMetaObject(ibValueMetaObject* object, ibValueMetaObject* objParent = nullptr);

#pragma region __array_h__

	//any
	template <typename _T1 = ibValueMetaObject>
	std::vector<_T1*> GetAnyArrayObject(const bool use_child_filter = false) const {
		std::vector<_T1*> array;
		FillArrayObjectByFilter<_T1, ibValueMetaObject>(array, {}, use_child_filter);
		return array;
	}

	//any
	template <typename _T1 = ibValueMetaObject>
	std::vector<_T1*> GetAnyArrayObject(const ibClassID& clsid, const bool use_child_filter = false) const {
		std::vector<_T1*> array;
		FillArrayObjectByFilter<_T1, ibValueMetaObject>(array, { clsid });
		return array;
	}

	//any
	template <typename _T1 = ibValueMetaObject>
	std::vector<_T1*> GetAnyArrayObject(const std::initializer_list<ibClassID> filter, const bool use_child_filter = false) const {
		std::vector<_T1*> array;
		FillArrayObjectByFilter<_T1, ibValueMetaObject>(array, filter, use_child_filter);
		return array;
	}

#pragma endregion
#pragma region __filter_h__

	// Overload pair (Effective C++ Item 3): const this → const result, so the
	// runtime (which reaches ibMetaData through a const handle) gets const
	// meta-objects — they protect metadata from mutation exactly like the const
	// ibMetaData handle does. non-const this → mutable, for designer /
	// meta-internal edits. The protected FindObjectByFilter helper stays const
	// and is shared by both; the const path just narrows its result.
	template <typename _T1 = ibValueMetaObject, typename _T2>
	const _T1* FindAnyObjectByFilter(const _T2& id, const bool use_child_filter = false) const {
		return FindObjectByFilter<_T2, ibValueMetaObject, _T1>(id, {}, use_child_filter);
	}
	template <typename _T1 = ibValueMetaObject, typename _T2>
	_T1* FindAnyObjectByFilter(const _T2& id, const bool use_child_filter = false) {
		return FindObjectByFilter<_T2, ibValueMetaObject, _T1>(id, {}, use_child_filter);
	}

	//any
	template <typename _T1 = ibValueMetaObject, typename _T2>
	const _T1* FindAnyObjectByFilter(const _T2& id, const ibClassID& clsid, const bool use_child_filter = false) const {
		return FindObjectByFilter<_T2, ibValueMetaObject, _T1>(id, { clsid }, use_child_filter);
	}
	template <typename _T1 = ibValueMetaObject, typename _T2>
	_T1* FindAnyObjectByFilter(const _T2& id, const ibClassID& clsid, const bool use_child_filter = false) {
		return FindObjectByFilter<_T2, ibValueMetaObject, _T1>(id, { clsid }, use_child_filter);
	}

	//any
	template <typename _T1 = ibValueMetaObject, typename _T2>
	const _T1* FindAnyObjectByFilter(const _T2& id,
		const std::initializer_list<ibClassID> filter, const bool use_child_filter = false) const {
		return FindObjectByFilter<_T2, ibValueMetaObject, _T1>(id, filter, use_child_filter);
	}
	template <typename _T1 = ibValueMetaObject, typename _T2>
	_T1* FindAnyObjectByFilter(const _T2& id,
		const std::initializer_list<ibClassID> filter, const bool use_child_filter = false) {
		return FindObjectByFilter<_T2, ibValueMetaObject, _T1>(id, filter, use_child_filter);
	}

#pragma endregion

	// Copy-aware identity resolve — a metaId is config-local (shifts on copy / reorder), the
	// metaobject's GUID is stable. Serialised references (source paths, meta-description lists,
	// reference types) store the GUID and resolve back through these. One home for both the
	// source- and meta-description memory serialisers (was a file-local duplicate). GetCommonGuid
	// yields the COPY-guid mid-copy, so a binding inside a copied object points at the copy.
	ibGuid  GuidByMetaId(const ibMetaID& id)  const;   // metaId -> stable guid (null if absent / not allowed)
	ibMetaID MetaIdByGuid(const ibGuid& guid) const;   // guid -> live metaId (wxNOT_FOUND if unresolved)

	//ID's
	ibMetaID GenerateNewID() const;

	//generate new name
	wxString GetNewName(const ibClassID& clsid,
		ibValueMetaObject* parent, const wxString& strPrefix = wxEmptyString, bool forConstructor = false);

#pragma region serialization

	wxString Serialize(const ibValue& cValue);
	ibValue Deserialize(const wxString& strValue);

#pragma endregion

protected:

#pragma region __array_h__

	template <typename _T1 = ibValueMetaObject, typename _T2 = ibValueMetaObject>
	bool FillArrayObjectByFilter(
		std::vector<_T1*>& array,
		const std::initializer_list<ibClassID> filter) const
	{
		const auto commonObject = GetCommonMetaObject();
		if (commonObject != nullptr)
			return commonObject->FillArrayObjectByFilter(array, filter, false);
		return false;
	}

	template <typename _T1 = ibValueMetaObject, typename _T2 = ibValueMetaObject>
	bool FillArrayObjectByFilter(
		std::vector<_T1*>& array,
		const std::initializer_list<ibClassID> filter,
		const bool use_child_filter) const
	{
		const auto commonObject = GetCommonMetaObject();
		if (commonObject != nullptr)
			return commonObject->FillArrayObjectByFilter(array, filter, use_child_filter);
		return false;
	}

#pragma endregion
#pragma region __filter_h__

	template<typename _T1, typename _T2 = ibValueMetaObject, typename _T3 = ibValueMetaObject>
	_T3* FindObjectByFilter(
		const _T1& id,
		const std::initializer_list<ibClassID> filter) const {
		const auto commonObject = GetCommonMetaObject();
		if (commonObject != nullptr)
			return commonObject->FindObjectByFilter<_T3>(id, filter, false);
		return nullptr;
	}

	template<typename _T1, typename _T2 = ibValueMetaObject, typename _T3 = ibValueMetaObject>
	_T3* FindObjectByFilter(
		const _T1& id,
		const std::initializer_list<ibClassID> filter,
		const bool use_child_filter) const {
		const auto commonObject = GetCommonMetaObject();
		if (commonObject != nullptr)
			return commonObject->FindObjectByFilter<_T3>(id, filter, use_child_filter);
		return nullptr;
	}

#pragma endregion

public:

	// Serialization chunk IDs for the metadata blob tree. The containers write
	// eHeaderBlock and frame the root; the per-node {eDataBlock,eChildBlock} layout
	// is emitted by ibBinaryProvider (serialize/dataBuilder.cpp), which mirrors these
	// ids for byte-compatibility (kept in sync there).
	enum
	{
		eHeaderBlock = 0x2320,
		eDataBlock = 0x2350,
		eChildBlock = 0x2370
	};

protected:

	bool m_metaModify;

	// --- the runtime-image IS the open state ---
	// No image (m_image == nullptr) == CLOSED; a live image == OPEN. A config run
	// builds the image and either keeps it (success) or drops it (failure ⇒ the load
	// "never happened"); close tears the registered objects down per-node and then
	// drops the image. So the presence of the image replaces a separate "opened" flag,
	// and the registry inside it owns its ctors — dropping the image frees them.
	//
	// LoadGuard is the RAII transaction. Its ctor CREATES the image (assert: was
	// closed — else "run without close") + the compile cache if this metadata wants
	// one; the dtor DROPS it, rolling the load back, UNLESS Commit() ran.
	// "exception == rollback": a raised ibBackendException (or any early return)
	// unwinds through the dtor, the image is dropped, and the state is exactly the
	// closed state it started from. Dropping the image also releases the designer
	// module-manager (it lives in the cache, freed by RAII → DestroyMainModule), so
	// no separate manager teardown is needed.
	class LoadGuard {
		ibMetaData* m_meta;
		bool        m_committed = false;
	public:
		explicit LoadGuard(ibMetaData* meta) : m_meta(meta) {
			wxASSERT(m_meta->m_image == nullptr);   // must be closed first — else a run-without-close
			// The image ctor builds its own designer infrastructure via the owner's
			// CreateDesignerCache() (cache + manager; nullptr for non-designer kinds).
			m_meta->m_image = std::make_shared<ibMetaImage>(m_meta);
		}
		~LoadGuard() {
			if (!m_committed)
				m_meta->m_image.reset();   // drop → load rolled back (frees ctors + cache + manager)
		}
		void Commit() { m_committed = true; }   // keep the image — it is now the live config
		LoadGuard(const LoadGuard&) = delete;
		LoadGuard& operator=(const LoadGuard&) = delete;
	};

	// The runtime image: nullptr == closed, live == open. Built by LoadGuard on a run,
	// dropped on close / failed run. The registry inside owns its ctors. The factory
	// methods guard on it directly (closed ⇒ not-found); mutation happens only while open.
	std::shared_ptr<ibMetaImage> m_image;

	// Factory version — monotonic invalidation counter for compiler caches. Bumped on
	// each register / unregister; never reset when the image drops, so observers only
	// ever see it advance.
	std::atomic<unsigned int> m_factoryCtorCountChanges = 0;

private:
	ibBackendMetadataTree* m_metaTree;
};

#endif 
