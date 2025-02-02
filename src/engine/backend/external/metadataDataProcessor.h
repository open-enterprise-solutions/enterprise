#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/partial/dataProcessor.h"

#define sign_dataProcessor 0x1345F6621261E

class BACKEND_API CMetaDataDataProcessor : public IMetaData
{
	wxString m_fullPath;

	IMetaData* m_ownerMeta; //owner for saving/loading
	CModuleManagerExternalDataProcessor* m_moduleManager;
	CMetaObjectDataProcessor* m_commonObject; 	//common meta object
	bool m_configOpened;

	version_identifier_t m_version;

public:

	CMetaDataDataProcessor();
	CMetaDataDataProcessor(IMetaData* metaData, CMetaObjectDataProcessor* srcDataProcessor = nullptr);
	virtual ~CMetaDataDataProcessor();

	virtual CMetaObjectDataProcessor* GetDataProcessor() const { return m_commonObject; }
	virtual CModuleManagerExternalDataProcessor* GetModuleManager() const { return m_moduleManager; }

	virtual void SetVersion(const version_identifier_t& version) { m_version = version; }
	virtual version_identifier_t GetVersion() const { return m_version; }

	virtual wxString GetFileName() const {
		return m_fullPath;
	}

	//runtime support:
	virtual CValue* CreateObjectRef(const class_identifier_t& clsid, CValue** paParams = nullptr, const long lSizeArray = 0);
	virtual CValue* CreateObjectRef(const wxString& className, CValue** paParams = nullptr, const long lSizeArray = 0) {
		return CreateObjectRef(
			GetIDObjectFromString(className), paParams, lSizeArray
		);
	}

	virtual bool IsRegisterCtor(const wxString& className) const;
	virtual bool IsRegisterCtor(const wxString& className, eCtorObjectType objectType) const;
	virtual bool IsRegisterCtor(const wxString& className, eCtorObjectType objectType, enum eCtorMetaType refType) const;

	virtual bool IsRegisterCtor(const class_identifier_t& clsid) const;

	virtual class_identifier_t GetIDObjectFromString(const wxString& clsName) const;
	virtual wxString GetNameObjectFromID(const class_identifier_t& clsid, bool upper = false) const;

	virtual IMetaValueTypeCtor* GetTypeCtor(const class_identifier_t& clsid) const;
	virtual IMetaValueTypeCtor* GetTypeCtor(const IMetaObject* metaValue, enum eCtorMetaType refType) const;

	virtual IAbstractTypeCtor* GetAvailableCtor(const class_identifier_t& clsid) const;
	virtual IAbstractTypeCtor* GetAvailableCtor(const wxString& className) const;

	virtual std::vector<IMetaValueTypeCtor*> GetListCtorsByType() const;
	virtual std::vector<IMetaValueTypeCtor*> GetListCtorsByType(const class_identifier_t& clsid, enum eCtorMetaType refType) const;
	virtual std::vector<IMetaValueTypeCtor*> GetListCtorsByType(enum eCtorMetaType refType) const;

	//metaData 
	virtual bool LoadConfiguration();
	virtual bool SaveConfiguration();
	virtual bool ClearConfiguration();

	//run/close 
	virtual bool RunConfiguration(int flags = defaultFlag);
	virtual bool CloseConfiguration(int flags = defaultFlag);

	//load/save form file
	bool LoadFromFile(const wxString& strFileName);
	bool SaveToFile(const wxString& strFileName);

	virtual IMetaObject* GetCommonMetaObject() const {
		return m_commonObject;
	}

	//start/exit module 
	virtual bool StartMainModule() { return m_moduleManager ? m_moduleManager->StartMainModule() : false; }
	virtual bool ExitMainModule(bool force = false) { return m_moduleManager ? m_moduleManager->ExitMainModule(force) : false; }

	//get metaObject 
	virtual IMetaObject* GetMetaObject(const meta_identifier_t& id);

protected:

	//header loader/saver 
	bool LoadHeader(CMemoryReader& readerData);
	bool SaveHeader(CMemoryWriter& writterData);

	//loader/saver/deleter: 
	bool LoadCommonMetadata(const class_identifier_t& clsid, CMemoryReader& readerData);
	bool LoadChildMetadata(const class_identifier_t& clsid, CMemoryReader& readerData, IMetaObject* parentObj);
	bool SaveCommonMetadata(const class_identifier_t& clsid, CMemoryWriter& writterData, bool saveToFile = false);
	bool SaveChildMetadata(const class_identifier_t& clsid, CMemoryWriter& writterData, IMetaObject* parentObj, bool saveToFile);
	bool DeleteCommonMetadata(const class_identifier_t& clsid);
	bool DeleteChildMetadata(const class_identifier_t& clsid, IMetaObject* parentObj);

	//run/close recursively:
	bool RunChildMetadata(IMetaObject* parentObj, int flags, bool before);
	bool CloseChildMetadata(IMetaObject* parentObj, int flags, bool before);
	bool ClearChildMetadata(IMetaObject* parentObj);
};