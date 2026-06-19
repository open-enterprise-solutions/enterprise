#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/partial/dataReport.h"
#include "backend/moduleManager/moduleManagerExt.h"

#define sign_dataReport 0x2355F6421261D

class BACKEND_API ibMetaDataReport :
	public ibMetaData {
public:

	ibMetaDataReport();
	ibMetaDataReport(ibMetaData* metaData, ibValueMetaObjectReport* report = nullptr);
	virtual ~ibMetaDataReport();

	virtual ibValueMetaObjectReport* GetReport() const; // out-of-line: m_commonObject is ibValuePtr
	virtual ibValueModuleRuntimeManagerExternalReport* GetManagerModule() const { return m_moduleManager; }

	virtual void SetVersion(const ibVersionID& version) { m_version = version; }
	virtual ibVersionID GetVersion() const { return m_version; }

	virtual wxString GetFileName() const { return m_fullPath; }

	//runtime support:
	virtual ibValue* CreateObjectRef(const ibClassID& clsid, ibValue** paParams = nullptr, const long lSizeArray = 0) const;
	virtual ibValue* CreateObjectRef(const wxString& className, ibValue** paParams = nullptr, const long lSizeArray = 0) const {
		return CreateObjectRef(
			GetIDObjectFromString(className), paParams, lSizeArray
		);
	}

	virtual bool IsRegisterCtor(const wxString& className) const;
	virtual bool IsRegisterCtor(const wxString& className, ibCtorObjectType objectType) const;
	virtual bool IsRegisterCtor(const wxString& className, ibCtorObjectType objectType, enum ibCtorObjectMetaType refType) const;

	virtual bool IsRegisterCtor(const ibClassID& clsid) const;

	virtual ibClassID GetIDObjectFromString(const wxString& className) const;
	virtual wxString GetNameObjectFromID(const ibClassID& clsid, bool upper = false) const;

	virtual ibCtorMetaValueType* GetTypeCtor(const ibClassID& clsid) const;
	virtual ibCtorMetaValueType* GetTypeCtor(const ibValueMetaObject* metaValue, enum ibCtorObjectMetaType refType) const;

	virtual ibCtorAbstractType* GetAvailableCtor(const wxString& className) const;
	virtual ibCtorAbstractType* GetAvailableCtor(const ibClassID& clsid) const;

	virtual std::vector<ibCtorMetaValueType*> GetListCtorsByType() const;
	virtual std::vector<ibCtorMetaValueType*> GetListCtorsByType(const ibClassID& clsid, ibCtorObjectMetaType refType) const;
	virtual std::vector<ibCtorMetaValueType*> GetListCtorsByType(enum ibCtorObjectMetaType refType) const;

	//Get owner metadata 
	virtual bool GetOwner(ibMetaData*& metaData) const;

	//factory version 
	virtual unsigned int GetFactoryCountChanges() const {
		return m_factoryCtorCountChanges +
			activeMetaData != nullptr ? activeMetaData->GetFactoryCountChanges() : 0;
	}

	//metaData 
	virtual bool LoadDatabase();
	virtual bool SaveDatabase();
	virtual bool ClearDatabase();

	//get language code 
	virtual wxString GetLangCode() const;

	//run/close
	virtual bool RunDatabase(int flags = defaultFlag);
	virtual bool CloseDatabase(int flags = defaultFlag);

	// Designer infrastructure (cache + manager) — built by the image ctor; cache in
	// designer mode, manager only for an external report.
	virtual std::unique_ptr<ibCompileValueCache> CreateDesignerCache() override;

	//load/save form file
	bool LoadFromFile(const wxString& strFileName);
	bool SaveToFile(const wxString& strFileName);

	virtual const ibValueMetaObject* GetCommonMetaObject() const; // out-of-line: m_commonObject is ibValuePtr
	virtual ibValueMetaObject* GetCommonMetaObject();

	//start/exit module
	virtual bool StartMainModule() { return m_moduleManager ? m_moduleManager->StartMainModule() : false; }
	virtual bool ExitMainModule(bool force = false) { return m_moduleManager ? m_moduleManager->ExitMainModule(force) : false; }

protected:

	//loader/saver/deleter: (header sign/version/guid read+written inside LoadCommonTree/SaveCommonTree)
	bool LoadCommonTree(ibValueMetaObjectReport* root, const ibClassID& clsid, ibReaderMemory& readerData, bool resetId = false);
	bool SaveCommonTree(const ibClassID& clsid, ibWriterMemory& writerData, int flags = defaultFlag);
	bool DeleteCommonTree(const ibClassID& clsid);

	// Build a detached external-report root for the start-from-file detached-root
	// swap (LoadFromFile). nullptr on failure; otherwise refcount 1, caller owns it.
	ibValueMetaObjectReport* BuildFreshRoot();

private:

	wxString m_fullPath;

	ibMetaData* m_ownerMeta; //owner for saving/loading
	ibValuePtr<ibValueModuleRuntimeManagerExternalReport> m_moduleManager; // owning handle; dtor runs DestroyMainModule (RAII)
	ibValuePtr<ibValueMetaObjectReport> m_commonObject; 	//common meta object — owning handle (ibValuePtr)
	// open flag now lives in ibMetaData's open-image (IsConfigOpen / SetConfigOpened)
	ibVersionID m_version;
};