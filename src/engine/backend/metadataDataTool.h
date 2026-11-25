#include "backend/metadataApplication.h"
#include "backend/metaCollection/partial/dataTool.h"
#include "backend/moduleManager/moduleManagerExt.h"

#define sign_dataTool 0x1345F6621261E

class BACKEND_API ibMetaDataDataTool :
	public ibMetaData {
public:

	ibMetaDataDataTool();
	ibMetaDataDataTool(ibMetaData* metaData, ibValueMetaObjectDataTool* srcDataTool);
	virtual ~ibMetaDataDataTool();

	virtual ibValueMetaObjectDataTool* GetDataTool() const; // out-of-line: m_rootObject is ibValuePtr
	virtual ibValueModuleRuntimeManagerExternalDataTool* GetManagerModule() const { return m_moduleManager; }

	virtual void SetVersion(const ibVersionID& version) { m_version = version; }
	virtual ibVersionID GetVersion() const { return m_version; }

	virtual ibString GetFileName() const { return m_fullPath; }

	//runtime support:
	virtual bool IsRegisterCtor(const ibString& className) const;
	virtual bool IsRegisterCtor(const ibString& className, ibCtorObjectType objectType) const;
	virtual bool IsRegisterCtor(const ibString& className, ibCtorObjectType objectType, enum ibCtorObjectMetaType refType) const;

	virtual bool IsRegisterCtor(const ibClassID& clsid) const;

	virtual ibClassID GetIDObjectFromString(const ibString& className) const;
	virtual ibString GetNameObjectFromID(const ibClassID& clsid, bool upper = false) const;

	virtual ibCtorMetaValueType* GetTypeCtor(const ibClassID& clsid) const;
	virtual ibCtorMetaValueType* GetTypeCtor(const ibValueMetaObject* metaValue, enum ibCtorObjectMetaType refType) const;

	virtual ibCtorAbstractType* GetAvailableCtor(const ibString& className) const;
	virtual ibCtorAbstractType* GetAvailableCtor(const ibClassID& clsid) const;

	virtual std::vector<ibCtorMetaValueType*> GetListCtorsByType() const;
	virtual std::vector<ibCtorMetaValueType*> GetListCtorsByType(const ibClassID& clsid, enum ibCtorObjectMetaType refType) const;
	virtual std::vector<ibCtorMetaValueType*> GetListCtorsByType(enum ibCtorObjectMetaType refType) const;

	//Get owner metadata 
	virtual bool GetOwner(ibMetaData*& metaData) const;

	//factory version 
	virtual unsigned int GetFactoryCountChanges() const {
		return m_factoryCtorCountChanges +
			(activeMetaData != nullptr ? activeMetaData->GetFactoryCountChanges() : 0);
	}

	//get language code 
	virtual ibString GetLangCode() const;

	//metaData 
	virtual bool LoadDatabase();
	virtual bool SaveDatabase();
	virtual bool ClearDatabase();

	//run/close
	virtual bool RunDatabase(const ibRuntimeRequest& request = ibRuntimeRequest());
	virtual bool CloseDatabase(const ibRuntimeRequest& request = ibRuntimeRequest());

	// Studio infrastructure (cache + manager) — built by the image ctor; cache in
	// studio mode, manager only for an external (.otl) data tool.
	virtual std::unique_ptr<ibCompileValueCache> CreateStudioCache() override;

	//load/save form file
	bool LoadFromFile(const ibString& strFileName);
	// …and from a file of the session's temporary storage, by its id — what a thin client handed the server.
	bool LoadFromTempFile(const ibString& id);
	bool SaveToFile(const ibString& strFileName);
	// …and into one — what a thin client takes down to where the person keeps it.
	bool SaveToTempFile(const ibString& id);

	virtual const ibValueMetaObject* GetRootMetaObject() const; // out-of-line: m_rootObject is ibValuePtr
	virtual ibValueMetaObject* GetRootMetaObject();

	//start/exit module
	virtual bool StartMainModule(const ibRuntimeRequest& request = ibRuntimeRequest()) { return m_moduleManager ? m_moduleManager->StartMainModule(request) : false; }
	virtual bool ExitMainModule(const ibRuntimeRequest& request = ibRuntimeRequest()) { return m_moduleManager ? m_moduleManager->ExitMainModule(request) : false; }

protected:

	//loader/saver/deleter: (header sign/version/guid read+written inside LoadRootTree/SaveRootTree)
	bool LoadRootTree(ibValueMetaObjectDataTool* root, const ibClassID& clsid, ibReaderMemory& readerData);
	bool SaveRootTree(const ibClassID& clsid, ibWriterMemory& writerData, int flags = defaultFlag);
	bool DeleteRootTree(const ibClassID& clsid);

	// Build a detached external data tool root for the start-from-file detached-root swap
	// (LoadFromFile). nullptr on failure; otherwise refcount 1, caller owns it.
	ibValueMetaObjectDataTool* BuildFreshRoot();

private:

	// The file's content in hand, loaded — the one load both roads come to, whatever the bytes were read from.
	bool LoadFromBuffer(const ibMemoryBuffer& buffer, const ibString& fullPath);
	// …and the content written, the one write both roads take, whatever the bytes go to.
	bool SaveToBuffer(ibWriterMemory& writerData, const ibString& fullPath);

	ibString m_fullPath;

	ibMetaData* m_ownerMeta; //owner for saving/loading
	ibValuePtr<ibValueModuleRuntimeManagerExternalDataTool> m_moduleManager; // owning handle; dtor runs DestroyMainModule (RAII)
	ibValuePtr<ibValueMetaObjectDataTool> m_rootObject; 	//root metaobject — owning handle (ibValuePtr)
	// open flag now lives in ibMetaData's open-image (IsOpen)
	ibVersionID m_version;
};