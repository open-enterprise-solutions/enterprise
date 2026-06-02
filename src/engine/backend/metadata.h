#ifndef __METADATA_H__
#define __METADATA_H__

#include <map>
#include <memory>
#include <optional>
#include <functional>
#include <vector>

#include "backend/moduleManager/moduleManager.h"
#include "backend/value_ptr.h"
#include "backend/ctorRegistry.h"

///////////////////////////////////////////////////////////////////////////////
class BACKEND_API ibBackendMetadataTree;
class BACKEND_API ibValueMetaObjectCommonModule;
class BACKEND_API ibValueMetaObjectModuleBase;
class BACKEND_API ibValueMetaObjectGenericData;
class BACKEND_API ibValueMetaObjectFormBase;
///////////////////////////////////////////////////////////////////////////////
class BACKEND_API ibCtorMetaValueType;
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

// Lazy form-construction marker stored in the compile-value cache.
// Eager form-build at OnAfterRunMetaObject time would assert on a null
// mm (form's compile module needs to be parented to the session root,
// which isn't compiled yet at that point). Instead the cache registers
// this deferred descriptor; cache materializes the form value on first
// FindCompileModule lookup, when mm is fully ready.
class BACKEND_API ibDeferredForm {
public:
	ibDeferredForm(ibValueMetaObjectGenericData* parent,
	               ibValueMetaObjectFormBase* form) noexcept
		: m_parent(parent), m_form(form) {}

	// Builds the form value. Calls parent->CreateObjectForm(form) and
	// wraps via formWrapper::inl::cast_value into a ibValue*. Returns
	// nullptr if either pointer isn't set.
	class ibValue* Construct() const;

	ibValueMetaObjectGenericData* Parent() const { return m_parent; }
	ibValueMetaObjectFormBase*    Form()   const { return m_form; }

private:
	ibValueMetaObjectGenericData* m_parent;
	ibValueMetaObjectFormBase*    m_form;
};

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

class BACKEND_API ibMetaData {
	void DoGenerateNewID(ibMetaID& id, ibValueMetaObject* top) const;
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
	ibModuleStorage* GetModuleStorage() { return &m_moduleStorage; }
	const ibModuleStorage* GetModuleStorage() const { return &m_moduleStorage; }

	// Compile-value cache — designer-only storage of compiled ibValue
	// pointers used by intellisense / metadata-property previews. Created
	// only on metadata configurations that support designer editing
	// (ibMetaDataConfigurationStorage's ctor allocates it). Runtime-only
	// configurations leave it nullptr — callers gate by null-check
	// (`if (auto* cc = metaData->GetCompileCache())`) instead of querying
	// appData->DesignerMode().
	ibCompileValueCache* GetCompileCache() const { return m_compileCache.get(); }

	virtual bool IsModified() const { return m_metaModify; }
	virtual void Modify(bool modify = true) {
		if (m_metaTree != nullptr)
			m_metaTree->Modify(modify);
		m_metaModify = modify;
	}

	virtual void SetVersion(const ibVersionID& version) = 0;
	virtual ibVersionID GetVersion() const = 0;

	virtual wxString GetFileName() const { return wxEmptyString; }
	virtual ibValueMetaObject* GetCommonMetaObject() const { return nullptr; }

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
		T* created_value = ibValue::CreateAndPrepareValueRef<T>(args...);
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

	//associate this metaData with 
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

	//any
	template <typename _T1 = ibValueMetaObject, typename _T2>
	_T1* FindAnyObjectByFilter(const _T2& id, const bool use_child_filter = false) const {
		return FindObjectByFilter<_T2, ibValueMetaObject, _T1>(id, {}, use_child_filter);
	}

	//any
	template <typename _T1 = ibValueMetaObject, typename _T2>
	_T1* FindAnyObjectByFilter(const _T2& id, const ibClassID& clsid, const bool use_child_filter = false) const {
		return FindObjectByFilter<_T2, ibValueMetaObject, _T1>(id, { clsid }, use_child_filter);
	}

	//any
	template <typename _T1 = ibValueMetaObject, typename _T2>
	_T1* FindAnyObjectByFilter(const _T2& id,
		const std::initializer_list<ibClassID> filter, const bool use_child_filter = false) const {
		return FindObjectByFilter<_T2, ibValueMetaObject, ibValueMetaObject, _T1>(id, filter, use_child_filter);
	}

#pragma endregion 

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

	// Serialization chunk IDs for the metadata blob tree. Public so the
	// node-owned walk on ibValueMetaObject (SaveSubtree/LoadChildren) can emit
	// the same layout the ibMetaData containers expect.
	enum
	{
		eHeaderBlock = 0x2320,
		eDataBlock = 0x2350,
		eChildBlock = 0x2370
	};

protected:

	bool m_metaModify;

	//custom types — clsid O(1); name / (metaValue,refType) linear (see ctorRegistry.h).
	// type_info index stays empty here (ibCtorMetaValueType has no concrete typeid —
	// metaobjects self-id via their own GetClassType() override, not typeid).
	ibCtorRegistry<ibCtorMetaValueType> m_factoryCtors;
	std::atomic<unsigned int> m_factoryCtorCountChanges = 0;

	// Common-module skeleton — populated by descriptor's OnBeforeRunMetaObject
	// during RunDatabase. Empty for metadata kinds without init modules.
	ibModuleStorage m_moduleStorage;

	// Compile-value cache — designer-only. Allocated by
	// ibMetaDataConfigurationStorage's ctor; remains nullptr on runtime
	// configurations. Lifetime ends with the metadata.
	std::unique_ptr<ibCompileValueCache> m_compileCache;

private:
	ibBackendMetadataTree* m_metaTree;
};

#endif 