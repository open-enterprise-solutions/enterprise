#ifndef __MODULE_MANAGER_H__
#define __MODULE_MANAGER_H__

#include "backend/moduleInfo.h"

#include <mutex>

#include "backend/metaCollection/metaObjectMetadata.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metaCollection/partial/commonObject.h"

class ibSession;

//*********************************************************************************************************
//*   ibValueModuleManager — LIGHTWEIGHT base                                                           *
//*                                                                                                     *
//*   Holds only what the Designer / code editor need to resolve names: the per-module unit type,       *
//*   the metadata unit, the "Manager" singleton, the global-constant map and the named context. NO     *
//*   runtime concerns (ProcUnit spin-up, common-module ProcUnit registry, Create/Start/Exit, session  *
//*   Attach). Those live in ibValueModuleRuntimeManager. Splitting them keeps the fragile runtime      *
//*   unit lifetime out of the editor's read path (was the source of the UAF while typing).             *
//*********************************************************************************************************

class BACKEND_API ibValueModuleManager :
	public ibValueDynamicMembers, public ibRuntimeModuleDataObject {
	public:
protected:
	enum helperAlias {
		eProcUnit = g_aliasExport   // module exports go through the descriptor autobind
	};
public:

	class BACKEND_API ibValueModuleUnit :
		public ibValueDynamicMembers, public ibRuntimeModuleDataObject {
	public:
	protected:
		enum helperAlias {
			eProcUnit = g_aliasExport   // module exports go through the descriptor autobind
		};
	public:

		// No default ctor — the ibRuntimeModuleDataObject base requires the owning
		// value's helper + a compile module (no default descriptor ctor exists).
		ibValueModuleUnit(ibValueMetaObjectModuleBase* moduleObject, bool managerModule = false);
		virtual ~ibValueModuleUnit();

		//get common module
		ibValueMetaObjectModuleBase* GetObjectModule() const {
			return m_moduleObject;
		}

		//Is global module?
		wxString GetModuleFullName() const {
			return m_moduleObject ? m_moduleObject->GetFullName() : wxString(wxEmptyString);
		}

		wxString GetModuleDocPath() const {
			return m_moduleObject ? m_moduleObject->GetDocPath() : wxString(wxEmptyString);
		}

		wxString GetModuleName() const {
			return m_moduleObject ? m_moduleObject->GetName() : wxString(wxEmptyString);
		}

		wxString GetModuleText() const {
			return m_moduleObject ? m_moduleObject->GetModuleText() : wxString(wxEmptyString);
		}

		bool IsGlobalModule() const {
			return m_moduleObject ? m_moduleObject->IsGlobalModule() : false;
		}

		//WORK AS AN AGGREGATE OBJECT

		// Name surface = the module's exports, autobound as the helper's tail by the
		// ibRuntimeModuleDataObject ctor (DoGetPMethods + by-value m_members come
		// from ibValueDynamicMembers). No FillMembers — exports are the whole surface.

		//method call
		virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
		virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

		virtual wxString GetString() const override {
			return m_moduleObject->GetName();
		}

		//check is empty
		virtual bool IsEmpty() const override {
			return false;
		}

		//operator '=='
		virtual bool CompareValueEQ(const ibValue& cParam) const override
		{
			ibValueModuleUnit* compareModule = dynamic_cast<ibValueModuleUnit*>(cParam.GetRef());
			if (compareModule) {
				return m_moduleObject == compareModule->GetObjectModule();
			}

			return false;
		}

		//operator '!='
		virtual bool CompareValueNE(const ibValue& cParam) const override
		{
			ibValueModuleUnit* compareModule = dynamic_cast<ibValueModuleUnit*>(cParam.GetRef());
			if (compareModule) {
				return m_moduleObject != compareModule->GetObjectModule();
			}

			return false;
		}

	protected:
		ibValueMetaObjectModuleBase* m_moduleObject;
	};

	class BACKEND_API ibValueMetadataUnit :
		public ibValueDynamicMembers {
	public:

		ibValueMetadataUnit() {}
		ibValueMetadataUnit(ibMetaData* metaData);
		virtual ~ibValueMetadataUnit();

		//get common module
		const ibMetaData* GetMetaData() const { return m_metaData; }
		ibMetaData* GetMetaData() { return m_metaData; }

		//check is empty
		virtual bool IsEmpty() const override { return false; }

		//operator '=='
		virtual bool CompareValueEQ(const ibValue& cParam) const override
		{
			ibValueMetadataUnit* compareMetadata = dynamic_cast<ibValueMetadataUnit*>(cParam.GetRef());
			if (compareMetadata) {
				return m_metaData == compareMetadata->GetMetaData();
			}

			return false;
		}

		//operator '!='
		virtual bool CompareValueNE(const ibValue& cParam) const override {
			ibValueMetadataUnit* compareMetadata = dynamic_cast<ibValueMetadataUnit*>(cParam.GetRef());
			if (compareMetadata) {
				return m_metaData != compareMetadata->GetMetaData();
			}

			return false;
		}

		// DoGetPMethods (protected) + by-value m_members come from ibValueDynamicMembers.
		void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

		//****************************************************************************
		//*                              Override attribute                          *
		//****************************************************************************

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;        //setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;                   //attribute value

	private:
		ibMetaData* m_metaData;
	};

	// "Data" global — the QUERYABLE-source mirror of "Metadata" (L4-2). The same
	// member shape (kind namespaces -> a Name-keyed structure), but the leaves are
	// ibValueQueryable values (the source the text query language reads through) —
	// the QUERYABLE kinds only (records with a data-reference, registers,
	// constants; no modules / forms / reports). Lazy by contract: vending a
	// Queryable reads NOTHING. (moduleManagerDataUnit.cpp;
	// docs/query-language-arc.md §23.5)
	class BACKEND_API ibValueDataUnit :
		public ibValueDynamicMembers {
	public:

		ibValueDataUnit() {}
		ibValueDataUnit(ibMetaData* metaData);
		virtual ~ibValueDataUnit();

		const ibMetaData* GetMetaData() const { return m_metaData; }
		ibMetaData* GetMetaData() { return m_metaData; }

		//check is empty
		virtual bool IsEmpty() const override { return false; }

		//operator '=='
		virtual bool CompareValueEQ(const ibValue& cParam) const override
		{
			ibValueDataUnit* compareData = dynamic_cast<ibValueDataUnit*>(cParam.GetRef());
			if (compareData) {
				return m_metaData == compareData->GetMetaData();
			}
			return false;
		}

		//operator '!='
		virtual bool CompareValueNE(const ibValue& cParam) const override {
			ibValueDataUnit* compareData = dynamic_cast<ibValueDataUnit*>(cParam.GetRef());
			if (compareData) {
				return m_metaData != compareData->GetMetaData();
			}
			return false;
		}

		void FillMembers(ibMemberTable& helper) const;   // bound in ctor

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;

		// Data.From(valueTable) — wrap an in-memory value table as a Queryable
		// source (LINQ over RAM, joinable with DB sources through the composer).
		virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue,
		                        ibValue** paParams, const long lSizeArray) override;

	private:
		ibMetaData* m_metaData;
	};

protected:

	//metaData and external variant
	ibValueModuleManager(ibMetaData* metaData, const ibValueMetaObjectModule* metaObject);

public:

	virtual ~ibValueModuleManager();

	// Name surface = the module's exports, autobound as the helper's tail by the
	// ibRuntimeModuleDataObject ctor (DoGetPMethods + by-value m_members come
	// from ibValueDynamicMembers). No FillMembers — exports are the whole surface.

	//method call
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value
	virtual long FindProp(const wxString& strName) const;

	//check is empty
	virtual bool IsEmpty() const { return false; }

	//system object:
	ibValue* GetObjectManager() const { return m_objectManager; }
	ibValueMetadataUnit* GetMetaManager() const { return m_metaManager; }
	ibValueDataUnit* GetDataManager() const { return m_dataManager; }

	// Resolve a registered common module's compiled unit. Pure virtual — the
	// runtime manager reads its per-session runtime registry, the designer holder
	// reads its own compiled-unit registry. Callers (e.g. catalog/document manager
	// objects via GetEditModuleManager) work through the base, designer-or-runtime
	// agnostic.
	virtual ibValueModuleUnit* FindCommonModule(const ibValueMetaObjectCommonModule* commonModule) const = 0;

	//associated map — globals are the compile module's extern variables (single
	// source; m_listGlConstValue registry removed). Values are owned by
	// m_metaManager / m_listCommonModuleManager, the map only references them.
	virtual std::map<wxString, ibValue*>& GetGlobalVariables() { return m_compileModule->m_listExternValue; }
	virtual std::map<wxString, ibContextVar>& GetContextVariables() { return m_compileModule->m_listContextValue; }

	//return external module
	virtual ibValue* GetObjectValue() const { return nullptr; }

	// Module lifecycle — same names across the hierarchy. ibValueModuleManagerDesigner
	// overrides these to seed the "Manager" singleton + ctor-context (Catalogs /
	// Documents / Enums) + global consts for the code editor (NO runtime, NO common-
	// module registry); ibValueModuleManagerRuntimeConfiguration overrides them with
	// the full compile + runtime bring-up. Base default no-op.
	virtual bool CreateMainModule() { return true; }
	virtual bool DestroyMainModule() { return true; }

protected:

	//global manager
	ibValuePtr<ibValue> m_objectManager;

	// global metamanager
	ibValuePtr<ibValueMetadataUnit> m_metaManager;

	// global data manager — the "Data" queryable-source root (L4-2)
	ibValuePtr<ibValueDataUnit> m_dataManager;

	friend class ibMetaDataConfiguration;
	friend class ibMetaDataDataProcessor;

	friend class ibValueModuleUnit;
};

//*********************************************************************************************************
//*   ibValueModuleRuntimeManager — HEAVY runtime part                                                  *
//*                                                                                                     *
//*   Adds the runtime common-module registry (ibValueRuntimeModuleUnit + ProcUnit spin-up), the        *
//*   per-session Attach/Detach, and the Create/Destroy/Start/Exit lifecycle. Per-session roots and     *
//*   the external data-processor / report managers derive from this, NOT from the lightweight base.    *
//*********************************************************************************************************

class BACKEND_API ibValueModuleRuntimeManager :
	public ibValueModuleManager {
	public:

	// Runtime variant — adds the owning module manager + per-runtime create/destroy.
	// The lightweight base unit above is what the designer reads for autocomplete (no
	// manager); this is what the runtime manager spawns for actual execution.
	class BACKEND_API ibValueRuntimeModuleUnit :
		public ibValueModuleManager::ibValueModuleUnit {
	public:
		ibValueRuntimeModuleUnit(ibValueModuleRuntimeManager* moduleManager, ibValueMetaObjectModuleBase* moduleObject, bool managerModule = false);

		//initalize common module
		bool CreateCommonModule();
		bool DestroyCommonModule();

	protected:
		ibValueModuleRuntimeManager* m_moduleManager;
	};

protected:

	//metaData and external variant
	ibValueModuleRuntimeManager(ibMetaData* metaData, const ibValueMetaObjectModule* metaObject);

public:

	virtual ~ibValueModuleRuntimeManager();

	//Create common module — overridden with full compile + runtime bring-up
	virtual bool CreateMainModule() override = 0;

	//destroy common module
	virtual bool DestroyMainModule() override = 0;

	//start common module
	virtual bool StartMainModule(bool force = false) = 0;

	//exit common module
	virtual bool ExitMainModule(bool force = false) = 0;

	// common modules — runtime-side mutation API. Metadata's
	// AddCommonModule/RenameCommonModule/RemoveCommonModule forwards
	// here on each active runtime; mm's CreateMainModule also invokes
	// RuntimeRegisterCommonModule for every descriptor in metadata's
	// init-modules list. Not for direct caller use outside metadata
	// or CreateMainModule.
	bool RuntimeRegisterCommonModule(ibValueMetaObjectCommonModule* commonModule, bool compileNow = false);
	bool RuntimeRenameCommonModule(ibValueMetaObjectCommonModule* commonModule, const wxString& newName);
	bool RuntimeUnregisterCommonModule(ibValueMetaObjectCommonModule* commonModule);

	ibValueModuleUnit* FindCommonModule(const ibValueMetaObjectCommonModule* commonModule) const override;

	virtual std::vector<ibValuePtr<ibValueRuntimeModuleUnit>>& GetCommonModules() { return m_listCommonModuleManager; }

	// Per-session runtime — create ProcUnits for main + common modules
	// under the given session's m_procUnitMap. Compile state untouched
	// on `this`. Overridden by subclasses with additional modules
	// (external data processor, report). Default impl handles the
	// common-case main module + m_listCommonModuleManager fanout.
	virtual bool AttachRuntime(class ibSession* session);

	// Symmetric teardown — drop this session's ProcUnit entries.
	virtual void DetachRuntime(class ibSession* session);

protected:

	void Clear();

	bool m_initialized;

	// Serializes Init/DetachRuntime across sessions — the
	// compile/common-module state is process-shared so two concurrent
	// Init (rapid F5) or Init-racing-Exit (pagehide beacon for old
	// session while new one logs in) would corrupt m_listCommonModule*
	// iteration or the ProcUnit Execute of BeforeStart running on
	// bytecode whose parent PU ptrs are mid-reassignment.
	std::mutex m_runtimeMutex;

	//array of common modules
	std::vector<ibValuePtr<ibValueRuntimeModuleUnit>> m_listCommonModuleManager;

	friend class ibValueRuntimeModuleUnit;
};

class BACKEND_API ibValueModuleManagerRuntimeConfiguration :
	public ibValueModuleRuntimeManager, public ibRuntimeRoot {
	public:
	//system events:
	bool BeforeStart();
	void OnStart();
	bool BeforeExit();
	void OnExit();
public:

	ibValueModuleManagerRuntimeConfiguration(
		ibMetaData* metaData,
		ibValueMetaObjectConfiguration* metaObject);

	// GetRoot override — we are the root, return ourselves as the
	// ibRuntimeRoot interface pointer.
	const ibRuntimeRoot* GetRoot() const override {
		return this;
	}

	//Create common module
	virtual bool CreateMainModule();

	//destroy common module
	virtual bool DestroyMainModule();

	//start common module
	virtual bool StartMainModule(bool force = false);

	//exit common module
	virtual bool ExitMainModule(bool force = false);

};

//*********************************************************************************************************
//*   ibValueModuleManagerDesigner — LIGHTWEIGHT designer holder                                        *
//*                                                                                                     *
//*   Derives the lightweight base (NOT the runtime manager): no ProcUnit, no runtime common-module     *
//*   units, no Attach. Lives inside ibCompileValueCache so the code editor reads the "Manager"         *
//*   singleton + ctor-context (Catalogs / Documents / Enums) + global consts from a holder that        *
//*   tracks the current designer state, decoupled from per-session runtime managers. Common modules    *
//*   are surfaced by the editor from the metadata storage + live text parsing, NOT from here, so no    *
//*   fragile runtime unit ever enters the editor's read path. Created in RunDatabase / released in      *
//*   CloseDatabase (its object module is reset on reload — holding one in the ctor would dangle). Used   *
//*   by the configuration AND by external data processors / reports (same editor context). See          *
//*   project_module_manager_split.                                                                      *
//*********************************************************************************************************

class BACKEND_API ibValueModuleManagerDesigner : public ibValueModuleManager {
	public:
	// Configuration variant — takes the config common object and forwards its
	// object module to the base (out-of-line: GetObjectModule needs the complete
	// metaobject type).
	ibValueModuleManagerDesigner(ibMetaData* metaData, ibValueMetaObjectConfiguration* metaObject);
	// External data-processor / report variant — its metadata has no
	// configuration common object, only an object module. The ctor-context
	// (Catalogs / Documents / Manager) still comes from the active config via
	// metaData's GetListCtorsByType, so the editor sees the same names.
	ibValueModuleManagerDesigner(ibMetaData* metaData, const ibValueMetaObjectModule* objectModule);

	// Compile-side context only (Manager + Catalogs/Documents/Enums + globals).
	// Lightweight bodies, no runtime. Exports helper names at the end of
	// CreateMainModule so autocomplete resolves on the first lookup after load.
	bool CreateMainModule() override;
	bool DestroyMainModule() override;

	// Independent common-module registry — designer-only, NOT shared with the
	// runtime managers. Each registered common module gets its OWN compiled
	// lightweight unit (ibValueModuleUnit: a compile module, NO ProcUnit), so the
	// editor reads exported names from a real compiled value with a predictable,
	// designer-owned lifetime. Driven from ibValueMetaObjectCommonModule's
	// OnBeforeRun / OnBeforeClose / OnRename hooks.
	// runModule=true compiles the unit immediately (a module added live in the
	// designer); false defers compilation to CreateMainModule (bulk load path).
	bool AddCommonModule(ibValueMetaObjectCommonModule* commonModule, bool managerModule = false, bool runModule = false);
	bool RemoveCommonModule(ibValueMetaObjectCommonModule* commonModule);
	bool RenameCommonModule(ibValueMetaObjectCommonModule* commonModule, const wxString& newName);
	ibValueModuleUnit* FindCommonModule(const ibValueMetaObjectCommonModule* commonModule) const override;

	std::vector<ibValuePtr<ibValueModuleUnit>>& GetCommonModules() { return m_listCommonModule; }

	// External DP/Report holder delegates its globals to the configuration root
	// so the external module's editor sees the root's globals (Metadata + common
	// modules surfaced as names), which never enter this holder's own map. The
	// config-root holder (m_external == false) returns its own map. Mirrors the
	// Ext runtime managers' GetContextVariables/GetGlobalVariables delegation.
	std::map<wxString, ibValue*>& GetGlobalVariables() override;

private:
	bool m_populated = false;

	// true when built from the external-DP/Report ctor (object-module variant) —
	// drives GetGlobalVariables delegation to the configuration root.
	bool m_external = false;

	// Compiled lightweight units, one per registered common module.
	std::vector<ibValuePtr<ibValueModuleUnit>> m_listCommonModule;
};

#endif
